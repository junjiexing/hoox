/*
 * Copyright (C) 2014-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2022-2025 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxinterceptor-priv.h"

#include "hooxarm64reader.h"
#include "hooxarm64relocator.h"
#include "hooxarm64writer.h"
#include "hooxcloak.h"
#include <string.h>
#include "hooxmemory.h"
#ifdef HAVE_DARWIN
# include "hoox/hooxdarwin.h"
# include "hooxdarwingrafter-priv.h"
#endif

#ifdef HAVE_DARWIN
# include <dlfcn.h>
# include <mach-o/dyld.h>
# include <mach-o/loader.h>
# include <stdlib.h>
#endif

#define HOOX_INTERCEPTOR_FULL_REDIRECT_SIZE 16

#define HOOX_ARM64_LOGICAL_PAGE_SIZE 4096

#define HOOX_FRAME_OFFSET_CPU_CONTEXT 0
#define HOOX_FRAME_OFFSET_NEXT_HOP \
    (HOOX_FRAME_OFFSET_CPU_CONTEXT + sizeof (HooxCpuContext))

#define HOOX_FCDATA(context) \
    ((HooxArm64FunctionContextData *) (context)->backend_data.storage)

typedef struct _HooxArm64FunctionContextData HooxArm64FunctionContextData;
typedef struct _HooxThunkSet HooxThunkSet;
typedef struct _HooxEmitThunksContext HooxEmitThunksContext;

struct _HooxInterceptorBackend
{
  HxRecMutex * mutex;
  HooxCodeAllocator * allocator;

  HooxArm64Writer writer;
  HooxArm64Relocator relocator;

  HxHashTable * thunks_by_scratch_reg;
};

struct _HooxThunkSet
{
  hx_pointer page;
  hx_pointer enter_thunk;
  hx_pointer leave_thunk;
};

struct _HooxEmitThunksContext
{
  HooxInterceptorBackend * backend;
  HooxThunkSet * thunks;
  hx_arm64_reg scratch_reg;
};

struct _HooxArm64FunctionContextData
{
  hx_uint redirect_code_size;
  hx_arm64_reg scratch_reg;
  hx_uint available_space;
};

HX_STATIC_ASSERT (sizeof (HooxArm64FunctionContextData)
    <= sizeof (HooxFunctionContextBackendData));

static hx_boolean hoox_interceptor_backend_write_custom_redirect (
    HooxInterceptorBackend * self, HooxFunctionContext * ctx, hx_pointer target);

static HooxThunkSet * hoox_interceptor_backend_get_thunks (
    HooxInterceptorBackend * self, hx_arm64_reg scratch_reg);
static HooxThunkSet * hoox_thunk_set_new (HooxInterceptorBackend * backend,
    hx_arm64_reg scratch_reg);
static void hoox_thunk_set_free (HooxThunkSet * thunks);

static void hoox_emit_thunks (hx_pointer mem, HooxEmitThunksContext * ctx);
static void hoox_emit_enter_thunk (HooxArm64Writer * aw, hx_arm64_reg scratch_reg);
static void hoox_emit_leave_thunk (HooxArm64Writer * aw, hx_arm64_reg scratch_reg);

static void hoox_emit_prolog (HooxArm64Writer * aw);
static void hoox_emit_epilog (HooxArm64Writer * aw, hx_arm64_reg scratch_reg);

HooxInterceptorBackend *
_hoox_interceptor_backend_create (HxRecMutex * mutex,
                                 HooxCodeAllocator * allocator)
{
  HooxInterceptorBackend * backend;

  backend = hx_slice_new0 (HooxInterceptorBackend);
  backend->mutex = mutex;
  backend->allocator = allocator;

  if (hoox_process_get_code_signing_policy () == HOOX_CODE_SIGNING_OPTIONAL)
  {
    hoox_arm64_writer_init (&backend->writer, NULL);
    hoox_arm64_relocator_init (&backend->relocator, NULL, &backend->writer);

    backend->thunks_by_scratch_reg = hx_hash_table_new_full (NULL, NULL, NULL,
        (HxDestroyNotify) hoox_thunk_set_free);
  }

  return backend;
}

void
_hoox_interceptor_backend_destroy (HooxInterceptorBackend * backend)
{
  if (backend->thunks_by_scratch_reg != NULL)
  {
    hx_hash_table_unref (backend->thunks_by_scratch_reg);

    hoox_arm64_relocator_clear (&backend->relocator);
    hoox_arm64_writer_clear (&backend->writer);
  }

  hx_slice_free (HooxInterceptorBackend, backend);
}

#ifdef HAVE_DARWIN

typedef struct _HooxImportTarget HooxImportTarget;
typedef struct _HooxImportEntry HooxImportEntry;
typedef struct _HooxClaimHookOperation HooxClaimHookOperation;
typedef struct _HooxGraftedSegmentPairDetails HooxGraftedSegmentPairDetails;

typedef hx_boolean (* HooxFoundGraftedSegmentPairFunc) (
    const HooxGraftedSegmentPairDetails * details, hx_pointer user_data);

struct _HooxImportTarget
{
  hx_pointer implementation;
  HooxFunctionContext * ctx;
  HxArray * entries;
};

struct _HooxImportEntry
{
  const struct mach_header_64 * mach_header;
  HooxGraftedImport * import;
};

struct _HooxClaimHookOperation
{
  HooxFunctionContext * ctx;
  hx_uint32 code_offset;

  hx_boolean success;
};

struct _HooxGraftedSegmentPairDetails
{
  const struct mach_header_64 * mach_header;

  HooxGraftedHeader * header;

  HooxGraftedHook * hooks;
  hx_uint32 num_hooks;

  HooxGraftedImport * imports;
  hx_uint32 num_imports;
};

extern void _hoox_interceptor_begin_invocation (void);
extern void _hoox_interceptor_end_invocation (void);

static void hoox_on_module_added (const struct mach_header * mh,
    intptr_t vmaddr_slide);
static void hoox_on_module_removed (const struct mach_header * mh,
    intptr_t vmaddr_slide);
static hx_boolean hoox_attach_segment_pair (
    const HooxGraftedSegmentPairDetails * details, hx_pointer user_data);
static hx_boolean hoox_detach_segment_pair (
    const HooxGraftedSegmentPairDetails * details, hx_pointer user_data);
static hx_boolean hoox_claim_hook_if_found_in_pair (
    const HooxGraftedSegmentPairDetails * details, hx_pointer user_data);

static HooxImportTarget * hoox_import_target_register (hx_pointer implementation);
static void hoox_import_target_link (HooxImportTarget * self,
    HooxFunctionContext * ctx);
static void hoox_import_target_free (HooxImportTarget * target);
static void hoox_import_target_maybe_activate (HooxImportTarget * self,
    const HooxImportEntry * entry);
static void hoox_import_target_activate (HooxImportTarget * self,
    const HooxImportEntry * entry);
static void hoox_import_target_deactivate (HooxImportTarget * self,
    const HooxImportEntry * entry);

static void hoox_enumerate_grafted_segment_pairs (hx_constpointer mach_header,
    HooxFoundGraftedSegmentPairFunc func, hx_pointer user_data);

static int hoox_compare_grafted_hook (const void * element_a,
    const void * element_b);

static hx_boolean hoox_is_system_module (const hx_char * path);

static HooxInterceptorBackend * hoox_interceptor_backend = NULL;
static HxHashTable * hoox_import_targets = NULL;

hx_boolean
_hoox_interceptor_backend_claim_grafted_trampoline (HooxInterceptorBackend * self,
                                                   HooxFunctionContext * ctx)
{
  HooxImportTarget * target;
  Dl_info info;
  HooxClaimHookOperation op;

  if (hoox_interceptor_backend == NULL)
  {
    hoox_interceptor_backend = self;
    hoox_import_targets = hx_hash_table_new_full (NULL, NULL, NULL,
        (HxDestroyNotify) hoox_import_target_free);

    _dyld_register_func_for_add_image (hoox_on_module_added);
    _dyld_register_func_for_remove_image (hoox_on_module_removed);
  }

  target = hx_hash_table_lookup (hoox_import_targets, ctx->function_address);
  if (target != NULL)
  {
    hoox_import_target_link (target, ctx);
    return TRUE;
  }

  if (dladdr (ctx->function_address, &info) == 0)
    return FALSE;

  op.ctx = ctx;
  op.code_offset = (hx_uint8 *) ctx->function_address - (hx_uint8 *) info.dli_fbase;

  op.success = FALSE;

  hoox_enumerate_grafted_segment_pairs (info.dli_fbase,
      hoox_claim_hook_if_found_in_pair, &op);

  if (!op.success && hoox_is_system_module (info.dli_fname))
  {
    target = hoox_import_target_register (ctx->function_address);
    hoox_import_target_link (target, ctx);
    return TRUE;
  }

  return op.success;
}

static void
hoox_on_module_added (const struct mach_header * mh,
                     intptr_t vmaddr_slide)
{
  hx_rec_mutex_lock (hoox_interceptor_backend->mutex);
  hoox_enumerate_grafted_segment_pairs (mh, hoox_attach_segment_pair, NULL);
  hx_rec_mutex_unlock (hoox_interceptor_backend->mutex);
}

static void
hoox_on_module_removed (const struct mach_header * mh,
                       intptr_t vmaddr_slide)
{
  hx_rec_mutex_lock (hoox_interceptor_backend->mutex);
  hoox_enumerate_grafted_segment_pairs (mh, hoox_detach_segment_pair, NULL);
  hx_rec_mutex_unlock (hoox_interceptor_backend->mutex);
}

static hx_boolean
hoox_attach_segment_pair (const HooxGraftedSegmentPairDetails * details,
                         hx_pointer user_data)
{
  const struct mach_header_64 * mach_header = details->mach_header;
  HooxGraftedHeader * header = details->header;
  HooxGraftedImport * imports = details->imports;
  hx_uint32 i;

  header->begin_invocation =
      HX_POINTER_TO_SIZE (_hoox_interceptor_begin_invocation);
  header->end_invocation =
      HX_POINTER_TO_SIZE (_hoox_interceptor_end_invocation);

  for (i = 0; i != header->num_imports; i++)
  {
    HooxGraftedImport * import = &imports[i];
    hx_pointer * slot, implementation;
    HooxImportTarget * target;
    HooxImportEntry entry;

    slot = (hx_pointer *) ((const hx_uint8 *) mach_header + import->slot_offset);
    implementation = *slot;

    target = hx_hash_table_lookup (hoox_import_targets, implementation);
    if (target == NULL)
      target = hoox_import_target_register (implementation);

    entry.mach_header = mach_header;
    entry.import = import;
    hx_array_append_val (target->entries, entry);

    hoox_import_target_maybe_activate (target, &entry);
  }

  return TRUE;
}

static hx_boolean
hoox_detach_segment_pair (const HooxGraftedSegmentPairDetails * details,
                         hx_pointer user_data)
{
  const struct mach_header_64 * mach_header = details->mach_header;
  HxHashTableIter iter;
  hx_pointer implementation;
  HooxImportTarget * target;
  HxQueue empty_targets = HX_QUEUE_INIT;
  HxList * cur;

  hx_hash_table_iter_init (&iter, hoox_import_targets);
  while (hx_hash_table_iter_next (&iter, &implementation, (hx_pointer *) &target))
  {
    HxArray * entries = target->entries;
    hx_int i;

    for (i = 0; i < entries->len; i++)
    {
      HooxImportEntry * entry = &hx_array_index (entries, HooxImportEntry, i);
      if (entry->mach_header == mach_header)
      {
        hx_array_remove_index_fast (entries, i);
        i--;
      }
    }

    if (target->ctx == NULL && entries->len == 0)
    {
      hx_queue_push_tail (&empty_targets, implementation);
    }
    else if (entries->len != 0)
    {
      hoox_import_target_maybe_activate (target,
          &hx_array_index (entries, HooxImportEntry, 0));
    }
  }

  for (cur = empty_targets.head; cur != NULL; cur = cur->next)
  {
    hx_hash_table_remove (hoox_import_targets, cur->data);
  }

  hx_queue_clear (&empty_targets);

  return TRUE;
}

static hx_boolean
hoox_claim_hook_if_found_in_pair (const HooxGraftedSegmentPairDetails * details,
                                 hx_pointer user_data)
{
  HooxClaimHookOperation * op = user_data;
  HooxFunctionContext * ctx = op->ctx;
  HooxGraftedHook key = { 0, };
  HooxGraftedHook * hook;
  hx_uint8 * trampoline;

  key.code_offset = op->code_offset;
  hook = bsearch (&key, details->hooks, details->header->num_hooks,
      sizeof (HooxGraftedHook), hoox_compare_grafted_hook);
  if (hook == NULL)
    return TRUE;

  hook->user_data = HX_POINTER_TO_SIZE (ctx);

  ctx->grafted_hook = hook;

  trampoline = (hx_uint8 *) details->mach_header + hook->trampoline_offset;
  ctx->on_enter_trampoline =
      trampoline + HOOX_GRAFTED_HOOK_ON_ENTER_OFFSET (hook);
  ctx->on_leave_trampoline =
      trampoline + HOOX_GRAFTED_HOOK_ON_LEAVE_OFFSET (hook);
  ctx->on_invoke_trampoline =
      trampoline + HOOX_GRAFTED_HOOK_ON_INVOKE_OFFSET (hook);

  op->success = TRUE;

  return FALSE;
}

static HooxImportTarget *
hoox_import_target_register (hx_pointer implementation)
{
  HooxImportTarget * target;

  target = hx_slice_new (HooxImportTarget);
  target->implementation = implementation;
  target->ctx = NULL;
  target->entries = hx_array_new (FALSE, FALSE, sizeof (HooxImportEntry));

  hx_hash_table_insert (hoox_import_targets, implementation, target);

  return target;
}

static void
hoox_import_target_link (HooxImportTarget * self,
                        HooxFunctionContext * ctx)
{
  self->ctx = ctx;
  ctx->import_target = self;
}

static void
hoox_import_target_free (HooxImportTarget * target)
{
  hx_array_free (target->entries, TRUE);

  hx_slice_free (HooxImportTarget, target);
}

static void
hoox_import_target_activate_all (HooxImportTarget * self)
{
  HxArray * entries = self->entries;
  hx_uint i;

  for (i = 0; i != entries->len; i++)
  {
    const HooxImportEntry * entry = &hx_array_index (entries, HooxImportEntry, i);
    hoox_import_target_activate (self, entry);
  }
}

static void
hoox_import_target_deactivate_all (HooxImportTarget * self)
{
  HxArray * entries = self->entries;
  hx_uint i;

  for (i = 0; i != entries->len; i++)
  {
    const HooxImportEntry * entry = &hx_array_index (entries, HooxImportEntry, i);
    hoox_import_target_deactivate (self, entry);
  }
}

static void
hoox_import_target_maybe_activate (HooxImportTarget * self,
                                  const HooxImportEntry * entry)
{
  HooxFunctionContext * ctx = self->ctx;

  if (ctx == NULL || !ctx->activated)
    return;

  hoox_import_target_activate (self, entry);
}

static void
hoox_import_target_activate (HooxImportTarget * self,
                            const HooxImportEntry * entry)
{
  HooxFunctionContext * ctx = self->ctx;
  HooxGraftedImport * import = entry->import;
  hx_pointer * slot;
  hx_uint8 * trampoline;
  mach_port_t self_task;
  HooxPageProtection prot;
  hx_boolean flip_needed;

  import->user_data = HX_POINTER_TO_SIZE (ctx);

  slot = (hx_pointer *) ((hx_uint8 *) entry->mach_header + import->slot_offset);

  trampoline = (hx_uint8 *) entry->mach_header + import->trampoline_offset;
  ctx->on_enter_trampoline =
      trampoline + HOOX_GRAFTED_IMPORT_ON_ENTER_OFFSET (import);
  ctx->on_leave_trampoline =
      trampoline + HOOX_GRAFTED_IMPORT_ON_LEAVE_OFFSET (import);
  ctx->on_invoke_trampoline = self->implementation;

  self_task = mach_task_self ();

  if (!hoox_darwin_query_protection (self_task, HOOX_ADDRESS (slot), &prot))
    return;

  flip_needed = (prot & HOOX_PAGE_WRITE) == 0;
  if (flip_needed)
  {
    if (!hoox_try_mprotect (slot, 4, prot | HOOX_PAGE_WRITE))
      return;
  }

  *slot = ctx->on_enter_trampoline;

  if (flip_needed)
    hoox_try_mprotect (slot, 4, prot);
}

static void
hoox_import_target_deactivate (HooxImportTarget * self,
                              const HooxImportEntry * entry)
{
  mach_port_t self_task;
  HooxPageProtection prot;
  hx_boolean flip_needed;
  hx_pointer * slot =
      (hx_pointer *) ((hx_uint8 *) entry->mach_header + entry->import->slot_offset);

  self_task = mach_task_self ();

  if (!hoox_darwin_query_protection (self_task, HOOX_ADDRESS (slot), &prot))
    return;

  flip_needed = (prot & HOOX_PAGE_WRITE) == 0;
  if (flip_needed)
  {
    if (!hoox_try_mprotect (slot, 4, prot | HOOX_PAGE_WRITE))
      return;
  }

  *slot = self->implementation;

  if (flip_needed)
    hoox_try_mprotect (slot, 4, prot);
}

static void
hoox_import_target_clear_user_data (HooxImportTarget * self)
{
  HxArray * entries = self->entries;
  hx_uint i;

  for (i = 0; i != entries->len; i++)
  {
    const HooxImportEntry * entry = &hx_array_index (entries, HooxImportEntry, i);
    entry->import->user_data = 0;
  }
}

static void
hoox_enumerate_grafted_segment_pairs (hx_constpointer mach_header,
                                     HooxFoundGraftedSegmentPairFunc func,
                                     hx_pointer user_data)
{
  const struct mach_header_64 * mh;
  hx_constpointer command;
  intptr_t slide;
  hx_uint i;

  mh = mach_header;
  command = mh + 1;
  slide = 0;
  for (i = 0; i != mh->ncmds; i++)
  {
    const struct load_command * lc = command;

    if (lc->cmd == LC_SEGMENT_64)
    {
      const struct segment_command_64 * sc = command;

      if (strcmp (sc->segname, "__TEXT") == 0)
      {
        slide = (hx_uint8 *) mach_header - (hx_uint8 *) sc->vmaddr;
      }
      else if (hx_str_has_prefix (sc->segname, "__FRIDA_DATA"))
      {
        HooxGraftedHeader * header = HX_SIZE_TO_POINTER (sc->vmaddr + slide);

        if (header->abi_version == HOOX_DARWIN_GRAFTER_ABI_VERSION)
        {
          HooxGraftedSegmentPairDetails d;

          d.mach_header = mh;

          d.header = header;

          d.hooks = (HooxGraftedHook *) (header + 1);
          d.num_hooks = header->num_hooks;

          d.imports = (HooxGraftedImport *) (d.hooks + header->num_hooks);
          d.num_imports = header->num_imports;

          if (!func (&d, user_data))
            return;
        }
      }
    }

    command = (const hx_uint8 *) command + lc->cmdsize;
  }
}

static int
hoox_compare_grafted_hook (const void * element_a,
                          const void * element_b)
{
  const HooxGraftedHook * a = element_a;
  const HooxGraftedHook * b = element_b;

  return (hx_ssize) a->code_offset - (hx_ssize) b->code_offset;
}

static hx_boolean
hoox_is_system_module (const hx_char * path)
{
  hx_boolean has_system_prefix;
  static hx_boolean api_initialized = FALSE;
  static bool (* dsc_contains_path) (const char * path) = NULL;

  has_system_prefix = hx_str_has_prefix (path, "/System/") ||
      hx_str_has_prefix (path, "/usr/lib/") ||
      hx_str_has_prefix (path, "/Developer/") ||
      hx_str_has_prefix (path, "/private/preboot/");
  if (has_system_prefix)
    return TRUE;

  if (!api_initialized)
  {
    dsc_contains_path =
        dlsym (RTLD_DEFAULT, "_dyld_shared_cache_contains_path");
    api_initialized = TRUE;
  }

  if (dsc_contains_path != NULL)
    return dsc_contains_path (path);

  return FALSE;
}

#else

hx_boolean
_hoox_interceptor_backend_claim_grafted_trampoline (HooxInterceptorBackend * self,
                                                   HooxFunctionContext * ctx)
{
  return FALSE;
}

#endif

static hx_boolean
hoox_interceptor_backend_prepare_trampoline (HooxInterceptorBackend * self,
                                            HooxFunctionContext * ctx,
                                            hx_boolean force,
                                            hx_boolean * need_deflector)
{
  HooxArm64FunctionContextData * data = HOOX_FCDATA (ctx);
  hx_pointer function_address = ctx->function_address;
  HooxRelocationScenario scenario =
      (ctx->scenario == HOOX_INTERCEPTOR_SCENARIO_OFFLINE)
      ? HOOX_SCENARIO_OFFLINE
      : HOOX_SCENARIO_ONLINE;
  hx_uint redirect_limit;

  *need_deflector = FALSE;

  data->scratch_reg = ctx->scratch_register;

  if (ctx->write_redirect != NULL)
  {
    hx_uint scan_bytes;

    scan_bytes = (ctx->redirect_space_hint != 0)
        ? ctx->redirect_space_hint
        : HOOX_INTERCEPTOR_MAX_REDIRECT_SIZE;
    hoox_arm64_relocator_can_relocate (function_address, scan_bytes, scenario,
        ctx->relocation_policy, &data->available_space, &data->scratch_reg);
    if (ctx->redirect_space_hint != 0 &&
        data->available_space > ctx->redirect_space_hint)
      data->available_space = ctx->redirect_space_hint;
    if (data->available_space == 0)
      return FALSE;

    if (data->scratch_reg == HX_ARM64_REG_INVALID)
    {
      data->scratch_reg = (ctx->scratch_register != HX_ARM64_REG_INVALID)
          ? ctx->scratch_register
          : HX_ARM64_REG_X16;
    }

    data->redirect_code_size = HOOX_INTERCEPTOR_FULL_REDIRECT_SIZE;
    ctx->trampoline_slice = hoox_code_allocator_alloc_slice (self->allocator);
    ctx->redirect_code = hx_malloc (data->available_space);

    return TRUE;
  }

  if (hoox_arm64_relocator_can_relocate (function_address,
        HOOX_INTERCEPTOR_FULL_REDIRECT_SIZE, scenario, ctx->relocation_policy,
        &redirect_limit, &data->scratch_reg))
  {
    data->redirect_code_size = HOOX_INTERCEPTOR_FULL_REDIRECT_SIZE;

    ctx->trampoline_slice = hoox_code_allocator_alloc_slice (self->allocator);
  }
  else if (force)
  {
    data->redirect_code_size = HOOX_INTERCEPTOR_FULL_REDIRECT_SIZE;

    ctx->trampoline_slice = hoox_code_allocator_alloc_slice (self->allocator);

    if (data->scratch_reg == HX_ARM64_REG_INVALID)
    {
      data->scratch_reg = (ctx->scratch_register != HX_ARM64_REG_INVALID)
          ? ctx->scratch_register
          : HX_ARM64_REG_X16;
    }

    return TRUE;
  }
  else if (ctx->type == HOOX_INTERCEPTOR_TYPE_FAST)
  {
    /*
     * For fast interceptors, we must patch the target function to jump
     * directly to the replacement function, instead of using a trampoline.
     * This requires the jump instruction at the target site to reach the
     * replacement function's address.
     *
     * However, if there are only 4 or 8 bytes of space available for patching,
     * we cannot always emit a jump instruction that can reliably reach the
     * replacement function (since the distance may be too far for short-range
     * instructions). Therefore, we cannot proceed in this case.
     */
    return FALSE;
  }
  else
  {
    HooxAddressSpec spec;
    hx_size alignment;

    if (redirect_limit >= 8)
    {
      data->redirect_code_size = 8;

      spec.near_address = HX_SIZE_TO_POINTER (
          HX_POINTER_TO_SIZE (function_address) &
          ~((hx_size) (HOOX_ARM64_LOGICAL_PAGE_SIZE - 1)));
      spec.max_distance = HOOX_ARM64_ADRP_MAX_DISTANCE;
      alignment = HOOX_ARM64_LOGICAL_PAGE_SIZE;
    }
    else if (redirect_limit >= 4)
    {
      data->redirect_code_size = 4;

      spec.near_address = function_address;
      spec.max_distance = HOOX_ARM64_B_MAX_DISTANCE;
      alignment = 0;
    }
    else
    {
      return FALSE;
    }

    ctx->trampoline_slice = hoox_code_allocator_try_alloc_slice_near (
        self->allocator, &spec, alignment);
    if (ctx->trampoline_slice == NULL)
    {
      ctx->trampoline_slice = hoox_code_allocator_alloc_slice (self->allocator);
      *need_deflector = TRUE;
    }
  }

  if (data->scratch_reg == HX_ARM64_REG_INVALID)
  {
    if (!force)
      goto no_scratch_reg;

    data->scratch_reg = (ctx->scratch_register != HX_ARM64_REG_INVALID)
        ? ctx->scratch_register
        : HX_ARM64_REG_X16;
  }

  return TRUE;

no_scratch_reg:
  {
    hoox_code_slice_unref (ctx->trampoline_slice);
    ctx->trampoline_slice = NULL;
    return FALSE;
  }
}

hx_boolean
_hoox_interceptor_backend_create_trampoline (HooxInterceptorBackend * self,
                                            HooxFunctionContext * ctx,
                                            hx_boolean force)
{
  HooxArm64Writer * aw = &self->writer;
  HooxArm64Relocator * ar = &self->relocator;
  hx_pointer function_address = ctx->function_address;
  HooxArm64FunctionContextData * data = HOOX_FCDATA (ctx);
  hx_boolean need_deflector;
  HooxThunkSet * thunks = NULL;
  hx_pointer deflector_target;
  HxString * signature;
  hx_boolean is_eligible_for_lr_rewriting;
  hx_uint reloc_bytes;

  if (!hoox_interceptor_backend_prepare_trampoline (self, ctx, force,
        &need_deflector))
    return FALSE;

  if (ctx->type != HOOX_INTERCEPTOR_TYPE_FAST)
    thunks = hoox_interceptor_backend_get_thunks (self, data->scratch_reg);

  hoox_arm64_writer_reset (aw, ctx->trampoline_slice->data);
  aw->pc = HOOX_ADDRESS (ctx->trampoline_slice->pc);

  if (ctx->type == HOOX_INTERCEPTOR_TYPE_FAST)
  {
    deflector_target = ctx->replacement_function;
  }
  else
  {
    ctx->on_enter_trampoline = hoox_sign_code_pointer (
        (hx_uint8 *) ctx->trampoline_slice->pc + hoox_arm64_writer_offset (aw));
    deflector_target = ctx->on_enter_trampoline;
  }

  if (need_deflector)
  {
    HooxAddressSpec caller;
    hx_pointer return_address;
    hx_boolean dedicated;

    caller.near_address =
        (hx_uint8 *) function_address + data->redirect_code_size - 4;
    caller.max_distance = HOOX_ARM64_B_MAX_DISTANCE;

    return_address = (hx_uint8 *) function_address + data->redirect_code_size;

    dedicated = data->redirect_code_size == 4;

    ctx->trampoline_deflector = hoox_code_allocator_alloc_deflector (
        self->allocator, &caller, return_address, deflector_target, dedicated);
    if (ctx->trampoline_deflector == NULL)
    {
      hoox_code_slice_unref (ctx->trampoline_slice);
      ctx->trampoline_slice = NULL;
      return FALSE;
    }

    hoox_arm64_writer_put_pop_reg_reg (aw, HX_ARM64_REG_X0, HX_ARM64_REG_LR);
  }

  if (ctx->type != HOOX_INTERCEPTOR_TYPE_FAST)
  {
    hx_arm64_reg scratch_reg = data->scratch_reg;

    hoox_arm64_writer_put_ldr_reg_address (aw, scratch_reg, HOOX_ADDRESS (ctx));
    hoox_arm64_writer_put_str_reg_reg_offset_mode (aw, scratch_reg,
        HX_ARM64_REG_SP, -16, HOOX_INDEX_PRE_ADJUST);
    hoox_arm64_writer_put_ldr_reg_address (aw, scratch_reg,
        HOOX_ADDRESS (hoox_sign_code_pointer (thunks->enter_thunk)));
    hoox_arm64_writer_put_br_reg (aw, scratch_reg);

    ctx->on_leave_trampoline =
        (hx_uint8 *) ctx->trampoline_slice->pc + hoox_arm64_writer_offset (aw);

    hoox_arm64_writer_put_ldr_reg_address (aw, scratch_reg, HOOX_ADDRESS (ctx));
    hoox_arm64_writer_put_str_reg_reg_offset_mode (aw, scratch_reg,
        HX_ARM64_REG_SP, -16, HOOX_INDEX_PRE_ADJUST);
    hoox_arm64_writer_put_ldr_reg_address (aw, scratch_reg,
        HOOX_ADDRESS (hoox_sign_code_pointer (thunks->leave_thunk)));
    hoox_arm64_writer_put_br_reg (aw, scratch_reg);

    hoox_arm64_writer_flush (aw);
    hx_assert (hoox_arm64_writer_offset (aw) <= ctx->trampoline_slice->size);
  }

  ctx->on_invoke_trampoline = hoox_sign_code_pointer (
      (hx_uint8 *) ctx->trampoline_slice->pc + hoox_arm64_writer_offset (aw));

  if (ctx->write_redirect != NULL &&
      !hoox_interceptor_backend_write_custom_redirect (self, ctx,
        deflector_target))
  {
    hoox_code_slice_unref (ctx->trampoline_slice);
    ctx->trampoline_slice = NULL;
    return FALSE;
  }

  hoox_arm64_relocator_reset (ar, function_address, aw);

  signature = hx_string_sized_new (16);

  do
  {
    const hx_insn * insn;

    reloc_bytes = hoox_arm64_relocator_read_one (ar, &insn);
    if (reloc_bytes == 0)
    {
      reloc_bytes = data->redirect_code_size;
      break;
    }

    if (signature->len != 0)
      hx_string_append_c (signature, ';');
    hx_string_append (signature, insn->mnemonic);
  }
  while (reloc_bytes < data->redirect_code_size);

  /*
   * Try to deal with minimal thunks that determine their caller and pass
   * it along to some inner function. This is important to support hooking
   * dlopen() on Android, where the dynamic linker uses the caller address
   * to decide on namespace and whether to allow the particular library to
   * be used by a particular caller.
   *
   * Because we potentially replace LR in order to trap the return, we end
   * up breaking dlopen() in such cases. We work around this by detecting
   * LR being read, and replace that instruction with a load of the actual
   * caller.
   *
   * This is however a bit risky done blindly, so we try to limit the
   * scope to the bare minimum. A potentially better longer term solution
   * is to analyze the function and patch each point of return, so we don't
   * have to replace LR on entry. That is however a bit complex, so we
   * opt for this simpler solution for now.
   */
  is_eligible_for_lr_rewriting = strcmp (signature->str, "mov;b") == 0 ||
      hx_str_has_prefix (signature->str, "stp;mov;mov;bl");

  hx_string_free (signature, TRUE);

  if (is_eligible_for_lr_rewriting)
  {
    const hx_insn * insn;

    while ((insn = hoox_arm64_relocator_peek_next_write_insn (ar)) != NULL)
    {
      const hx_arm64_op * source_op = &insn->detail->arm64.operands[1];

      if (insn->id == HX_ARM64_INS_MOV &&
          source_op->type == HX_ARM64_OP_REG &&
          source_op->reg == HX_ARM64_REG_LR)
      {
        hx_arm64_reg dst_reg = insn->detail->arm64.operands[0].reg;
        const hx_uint reg_size = sizeof (hx_pointer);
        const hx_uint reg_pair_size = 2 * reg_size;
        hx_uint dst_reg_index, dst_reg_slot_index, dst_reg_offset_in_frame;

        hoox_arm64_writer_put_push_all_x_registers (aw);

        hoox_arm64_writer_put_call_address_with_arguments (aw,
            HOOX_ADDRESS (_hoox_interceptor_translate_top_return_address), 1,
            HOOX_ARG_REGISTER, HX_ARM64_REG_LR);

        if (dst_reg >= HX_ARM64_REG_X0 && dst_reg <= HX_ARM64_REG_X28)
        {
          dst_reg_index = dst_reg - HX_ARM64_REG_X0;
        }
        else
        {
          hx_assert (dst_reg >= HX_ARM64_REG_X29 && dst_reg <= HX_ARM64_REG_X30);

          dst_reg_index = dst_reg - HX_ARM64_REG_X29;
        }

        dst_reg_slot_index = (dst_reg_index * reg_size) / reg_pair_size;

        dst_reg_offset_in_frame = (15 - dst_reg_slot_index) * reg_pair_size;
        if (dst_reg_index % 2 != 0)
          dst_reg_offset_in_frame += reg_size;

        hoox_arm64_writer_put_str_reg_reg_offset (aw, HX_ARM64_REG_X0, HX_ARM64_REG_SP,
            dst_reg_offset_in_frame);

        hoox_arm64_writer_put_pop_all_x_registers (aw);

        hoox_arm64_relocator_skip_one (ar);
      }
      else
      {
        hoox_arm64_relocator_write_one (ar);
      }
    }
  }
  else
  {
    hoox_arm64_relocator_write_all (ar);
  }

  if (!ar->eoi)
  {
    HooxAddress resume_at;

    resume_at = hoox_sign_code_address (
        HOOX_ADDRESS (function_address) + reloc_bytes);
    hoox_arm64_writer_put_ldr_reg_address (aw, data->scratch_reg, resume_at);
    hoox_arm64_writer_put_br_reg (aw, data->scratch_reg);
  }

  hoox_arm64_writer_flush (aw);
  hx_assert (hoox_arm64_writer_offset (aw) <= ctx->trampoline_slice->size);

  ctx->overwritten_prologue_len = reloc_bytes;
  ctx->overwritten_prologue = hx_malloc (reloc_bytes);
  memcpy (ctx->overwritten_prologue, function_address, reloc_bytes);

  return TRUE;
}

static hx_boolean
hoox_interceptor_backend_write_custom_redirect (HooxInterceptorBackend * self,
                                               HooxFunctionContext * ctx,
                                               hx_pointer target)
{
  HooxArm64FunctionContextData * data = HOOX_FCDATA (ctx);
  HooxRedirectWriteResult result;
  HooxArm64Writer rw;
  HooxRedirectWriteDetails details;

  hoox_arm64_writer_init (&rw, ctx->redirect_code);
  rw.pc = HOOX_ADDRESS (ctx->function_address);

  details.writer = &rw;
  details.target = target;
  details.scratch_register = data->scratch_reg;
  details.capacity = data->available_space;

  result = ctx->write_redirect (&details, ctx->write_redirect_data);

  hoox_arm64_writer_flush (&rw);
  data->redirect_code_size = hoox_arm64_writer_offset (&rw);
  hoox_arm64_writer_clear (&rw);

  hx_assert (data->redirect_code_size <= data->available_space);

  return result == HOOX_REDIRECT_WRITTEN;
}

void
_hoox_interceptor_backend_destroy_trampoline (HooxInterceptorBackend * self,
                                             HooxFunctionContext * ctx)
{
#ifdef HAVE_DARWIN
  if (ctx->grafted_hook != NULL)
  {
    HooxGraftedHook * func = ctx->grafted_hook;
    func->user_data = 0;
    return;
  }

  if (ctx->import_target != NULL)
  {
    hoox_import_target_clear_user_data (ctx->import_target);
    return;
  }
#endif

  hoox_code_slice_unref (ctx->trampoline_slice);
  hoox_code_deflector_unref (ctx->trampoline_deflector);
  ctx->trampoline_slice = NULL;
  ctx->trampoline_deflector = NULL;
}

void
_hoox_interceptor_backend_activate_trampoline (HooxInterceptorBackend * self,
                                              HooxFunctionContext * ctx,
                                              hx_pointer prologue)
{
  HooxArm64Writer * aw = &self->writer;
  HooxArm64FunctionContextData * data = HOOX_FCDATA (ctx);
  HooxAddress on_enter;

  if (ctx->type == HOOX_INTERCEPTOR_TYPE_FAST)
    on_enter = HOOX_ADDRESS (ctx->replacement_function);
  else
    on_enter = HOOX_ADDRESS (ctx->on_enter_trampoline);

#ifdef HAVE_DARWIN
  if (ctx->grafted_hook != NULL)
  {
    _hoox_grafted_hook_activate (ctx->grafted_hook);
    return;
  }

  if (ctx->import_target != NULL)
  {
    hoox_import_target_activate_all (ctx->import_target);
    return;
  }
#endif

  hoox_arm64_writer_reset (aw, prologue);
  aw->pc = HOOX_ADDRESS (ctx->function_address);

  if (ctx->write_redirect != NULL)
  {
    hoox_arm64_writer_put_bytes (aw, ctx->redirect_code,
        data->redirect_code_size);
  }
  else if (ctx->trampoline_deflector != NULL)
  {
    if (data->redirect_code_size == 8)
    {
      hoox_arm64_writer_put_push_reg_reg (aw, HX_ARM64_REG_X0, HX_ARM64_REG_LR);
      hoox_arm64_writer_put_bl_imm (aw,
          HOOX_ADDRESS (ctx->trampoline_deflector->trampoline));
    }
    else
    {
      hx_assert (data->redirect_code_size == 4);
      hoox_arm64_writer_put_b_imm (aw,
          HOOX_ADDRESS (ctx->trampoline_deflector->trampoline));
    }
  }
  else
  {
    switch (data->redirect_code_size)
    {
      case 4:
        hoox_arm64_writer_put_b_imm (aw, on_enter);
        break;
      case 8:
        hoox_arm64_writer_put_adrp_reg_address (aw, data->scratch_reg, on_enter);
        hoox_arm64_writer_put_br_reg_no_auth (aw, data->scratch_reg);
        break;
      case HOOX_INTERCEPTOR_FULL_REDIRECT_SIZE:
        hoox_arm64_writer_put_ldr_reg_address (aw, data->scratch_reg, on_enter);
        hoox_arm64_writer_put_br_reg (aw, data->scratch_reg);
        break;
      default:
        hx_assert_not_reached ();
    }
  }

  hoox_arm64_writer_flush (aw);
  hx_assert (hoox_arm64_writer_offset (aw) <= data->redirect_code_size);
}

void
_hoox_interceptor_backend_deactivate_trampoline (HooxInterceptorBackend * self,
                                                HooxFunctionContext * ctx,
                                                hx_pointer prologue)
{
#ifdef HAVE_DARWIN
  if (ctx->grafted_hook != NULL)
  {
    _hoox_grafted_hook_deactivate (ctx->grafted_hook);
    return;
  }

  if (ctx->import_target != NULL)
  {
    hoox_import_target_deactivate_all (ctx->import_target);
    return;
  }
#endif

  memcpy (prologue, ctx->overwritten_prologue,
      ctx->overwritten_prologue_len);
}

hx_pointer
_hoox_interceptor_backend_get_function_address (HooxFunctionContext * ctx)
{
  return ctx->function_address;
}

hx_pointer
_hoox_interceptor_backend_resolve_redirect (HooxInterceptorBackend * self,
                                           hx_pointer address)
{
  return hoox_arm64_reader_try_get_relative_jump_target (address);
}

hx_size
_hoox_interceptor_backend_detect_hook_size (hx_constpointer code,
                                           hx_csh capstone,
                                           hx_insn * insn)
{
  const hx_arm64 * arm64 = &insn->detail->arm64;
  const uint8_t * start, * cursor;
  size_t size;
  uint64_t addr;
  hx_arm64_reg expecting_branch_to_trampoline_in_reg = HX_ARM64_REG_INVALID;
  hx_size inline_data_size = 0;
  hx_boolean expecting_call_to_shared_deflector = FALSE;

  start = code;
  cursor = start;
  size = 16;
  addr = HX_POINTER_TO_SIZE (cursor);

  if (!hx_disasm_iter (capstone, &cursor, &size, &addr, insn))
    return 0;
  switch (insn->id)
  {
    case HX_ARM64_INS_B:
      return cursor - start;
    case HX_ARM64_INS_ADRP:
    case HX_ARM64_INS_LDR:
      expecting_branch_to_trampoline_in_reg = arm64->operands[0].reg;
      if (insn->id == HX_ARM64_INS_LDR)
        inline_data_size = 8;
      break;
    case HX_ARM64_INS_STP:
      expecting_call_to_shared_deflector =
          arm64->operands[0].reg == HX_ARM64_REG_X0 &&
          arm64->operands[1].reg == HX_ARM64_REG_LR;
      break;
    default:
      break;
  }

  if (!hx_disasm_iter (capstone, &cursor, &size, &addr, insn))
    return 0;
  switch (insn->id)
  {
    case HX_ARM64_INS_BR:
    case HX_ARM64_INS_BRAAZ:
      if (arm64->operands[0].reg == expecting_branch_to_trampoline_in_reg)
        return (cursor - start) + inline_data_size;
      break;
    case HX_ARM64_INS_BL:
      if (expecting_call_to_shared_deflector)
        return cursor - start;
      break;
    default:
      break;
  }

  return 0;
}

static HooxThunkSet *
hoox_interceptor_backend_get_thunks (HooxInterceptorBackend * self,
                                    hx_arm64_reg scratch_reg)
{
  HooxThunkSet * thunks;

  thunks = hx_hash_table_lookup (self->thunks_by_scratch_reg,
      HX_INT_TO_POINTER (scratch_reg));
  if (thunks == NULL)
  {
    thunks = hoox_thunk_set_new (self, scratch_reg);
    hx_hash_table_insert (self->thunks_by_scratch_reg,
        HX_INT_TO_POINTER (scratch_reg), thunks);
  }

  return thunks;
}

static HooxThunkSet *
hoox_thunk_set_new (HooxInterceptorBackend * backend,
                   hx_arm64_reg scratch_reg)
{
  HooxThunkSet * thunks;
  hx_size page_size, code_size;
  HooxPageProtection protection;
  HooxMemoryRange range;
  HooxEmitThunksContext ctx;

  thunks = hx_slice_new (HooxThunkSet);

  page_size = hoox_query_page_size ();
  code_size = page_size;

  protection = hoox_memory_can_remap_writable () ? HOOX_PAGE_RX : HOOX_PAGE_RW;

  thunks->page = hoox_memory_allocate (NULL, code_size, page_size, protection);

  range.base_address = HOOX_ADDRESS (thunks->page);
  range.size = code_size;
  hoox_cloak_add_range (&range);

  ctx.backend = backend;
  ctx.thunks = thunks;
  ctx.scratch_reg = scratch_reg;
  hoox_memory_patch_code (thunks->page, 1024,
      (HooxMemoryPatchApplyFunc) hoox_emit_thunks, &ctx);

  return thunks;
}

static void
hoox_thunk_set_free (HooxThunkSet * thunks)
{
  hoox_memory_free (thunks->page, hoox_query_page_size ());

  hx_slice_free (HooxThunkSet, thunks);
}

static void
hoox_emit_thunks (hx_pointer mem,
                 HooxEmitThunksContext * ctx)
{
  HooxThunkSet * thunks = ctx->thunks;
  HooxArm64Writer * aw = &ctx->backend->writer;
  hx_arm64_reg scratch_reg = ctx->scratch_reg;

  thunks->enter_thunk = thunks->page;
  hoox_arm64_writer_reset (aw, mem);
  aw->pc = HOOX_ADDRESS (thunks->enter_thunk);
  hoox_emit_enter_thunk (aw, scratch_reg);
  hoox_arm64_writer_flush (aw);

  thunks->leave_thunk =
      (hx_uint8 *) thunks->enter_thunk + hoox_arm64_writer_offset (aw);
  hoox_emit_leave_thunk (aw, scratch_reg);
  hoox_arm64_writer_flush (aw);
}

static void
hoox_emit_enter_thunk (HooxArm64Writer * aw,
                      hx_arm64_reg scratch_reg)
{
  hoox_arm64_writer_put_ldr_reg_reg_offset (aw, HX_ARM64_REG_X17, HX_ARM64_REG_SP, 0);

  hoox_emit_prolog (aw);

  hoox_arm64_writer_put_add_reg_reg_imm (aw, HX_ARM64_REG_X1, HX_ARM64_REG_SP,
      HOOX_FRAME_OFFSET_CPU_CONTEXT);
  hoox_arm64_writer_put_add_reg_reg_imm (aw, HX_ARM64_REG_X2, HX_ARM64_REG_SP,
      HOOX_FRAME_OFFSET_CPU_CONTEXT + HX_STRUCT_OFFSET (HooxCpuContext, lr));
  hoox_arm64_writer_put_add_reg_reg_imm (aw, HX_ARM64_REG_X3, HX_ARM64_REG_SP,
      HOOX_FRAME_OFFSET_NEXT_HOP);

  hoox_arm64_writer_put_call_address_with_arguments (aw,
      HOOX_ADDRESS (_hoox_function_context_begin_invocation), 4,
      HOOX_ARG_REGISTER, HX_ARM64_REG_X17,
      HOOX_ARG_REGISTER, HX_ARM64_REG_X1,
      HOOX_ARG_REGISTER, HX_ARM64_REG_X2,
      HOOX_ARG_REGISTER, HX_ARM64_REG_X3);

  hoox_emit_epilog (aw, scratch_reg);
}

static void
hoox_emit_leave_thunk (HooxArm64Writer * aw,
                      hx_arm64_reg scratch_reg)
{
  hoox_arm64_writer_put_ldr_reg_reg_offset (aw, HX_ARM64_REG_X17, HX_ARM64_REG_SP, 0);

  hoox_emit_prolog (aw);

  hoox_arm64_writer_put_add_reg_reg_imm (aw, HX_ARM64_REG_X1, HX_ARM64_REG_SP,
      HOOX_FRAME_OFFSET_CPU_CONTEXT);
  hoox_arm64_writer_put_add_reg_reg_imm (aw, HX_ARM64_REG_X2, HX_ARM64_REG_SP,
      HOOX_FRAME_OFFSET_NEXT_HOP);

  hoox_arm64_writer_put_call_address_with_arguments (aw,
      HOOX_ADDRESS (_hoox_function_context_end_invocation), 3,
      HOOX_ARG_REGISTER, HX_ARM64_REG_X17,
      HOOX_ARG_REGISTER, HX_ARM64_REG_X1,
      HOOX_ARG_REGISTER, HX_ARM64_REG_X2);

  hoox_emit_epilog (aw, scratch_reg);
}

static void
hoox_emit_prolog (HooxArm64Writer * aw)
{
  hx_int i;

  /*
   * Set up our stack frame, with the next_hop slot already pushed by the
   * caller's dispatch sequence (also holding the function context pointer
   * on entry):
   *
   * [in: function context / frame pointer chain entry, out: next_hop]
   * [in/out: cpu_context]
   */

#ifndef HX_OS_NONE
  /* Store vector registers */
  for (i = 30; i != -2; i -= 2)
    hoox_arm64_writer_put_push_reg_reg (aw, HX_ARM64_REG_Q0 + i, HX_ARM64_REG_Q1 + i);
#endif

  /* Store X1-X28, FP, and LR */
  hoox_arm64_writer_put_push_reg_reg (aw, HX_ARM64_REG_FP, HX_ARM64_REG_LR);
  for (i = 27; i != -1; i -= 2)
    hoox_arm64_writer_put_push_reg_reg (aw, HX_ARM64_REG_X0 + i, HX_ARM64_REG_X1 + i);

  /* Store NZCV and X0 */
  hoox_arm64_writer_put_mov_reg_nzcv (aw, HX_ARM64_REG_X1);
  hoox_arm64_writer_put_push_reg_reg (aw, HX_ARM64_REG_X1, HX_ARM64_REG_X0);

  /* PC placeholder and SP */
  hoox_arm64_writer_put_add_reg_reg_imm (aw, HX_ARM64_REG_X0,
      HX_ARM64_REG_SP, sizeof (HooxCpuContext) -
      HX_STRUCT_OFFSET (HooxCpuContext, nzcv) + 16);
  hoox_arm64_writer_put_push_reg_reg (aw, HX_ARM64_REG_XZR, HX_ARM64_REG_X0);

  /* Frame pointer chain entry */
  hoox_arm64_writer_put_str_reg_reg_offset (aw, HX_ARM64_REG_LR, HX_ARM64_REG_SP,
      sizeof (HooxCpuContext) + 8);
  hoox_arm64_writer_put_str_reg_reg_offset (aw, HX_ARM64_REG_FP, HX_ARM64_REG_SP,
      sizeof (HooxCpuContext) + 0);
  hoox_arm64_writer_put_add_reg_reg_imm (aw, HX_ARM64_REG_FP, HX_ARM64_REG_SP,
      sizeof (HooxCpuContext));
}

static void
hoox_emit_epilog (HooxArm64Writer * aw,
                 hx_arm64_reg scratch_reg)
{
  hx_uint i;

  /* Skip PC and SP */
  hoox_arm64_writer_put_add_reg_reg_imm (aw, HX_ARM64_REG_SP, HX_ARM64_REG_SP, 16);

  /* Restore NZCV and X0 */
  hoox_arm64_writer_put_pop_reg_reg (aw, HX_ARM64_REG_X1, HX_ARM64_REG_X0);
  hoox_arm64_writer_put_mov_nzcv_reg (aw, HX_ARM64_REG_X1);

  /* Restore X1-X28, FP, and LR */
  for (i = 1; i != 29; i += 2)
    hoox_arm64_writer_put_pop_reg_reg (aw, HX_ARM64_REG_X0 + i, HX_ARM64_REG_X1 + i);
  hoox_arm64_writer_put_pop_reg_reg (aw, HX_ARM64_REG_FP, HX_ARM64_REG_LR);

#ifndef HX_OS_NONE
  /* Restore vector registers */
  for (i = 0; i != 32; i += 2)
    hoox_arm64_writer_put_pop_reg_reg (aw, HX_ARM64_REG_Q0 + i, HX_ARM64_REG_Q1 + i);
#endif

  hoox_arm64_writer_put_ldr_reg_reg_offset_mode (aw, scratch_reg, HX_ARM64_REG_SP,
      16, HOOX_INDEX_POST_ADJUST);
#ifndef HAVE_PTRAUTH
  hoox_arm64_writer_put_ret_reg (aw, scratch_reg);
#else
  hoox_arm64_writer_put_br_reg (aw, scratch_reg);
#endif
}
