/*
 * Copyright (C) 2010-2024 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2025 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxcodeallocator.h"

#include "hooxcloak.h"
#include "hooxcodesegment.h"
#include "hooxmemory.h"
#include "hooxprocess-priv.h"
#ifdef HAVE_ARM
# include "hooxarmwriter.h"
# include "hooxthumbwriter.h"
#endif
#ifdef HAVE_ARM64
# include "hooxarm64writer.h"
#endif
#ifdef HAVE_DARWIN
# include "backend-darwin/hooxdarwin-priv.h"
#endif

#include <string.h>

#define HOOX_CODE_SLICE_ELEMENT_FROM_SLICE(s) \
    ((HooxCodeSliceElement *) (((hx_uint8 *) (s)) - \
        HX_STRUCT_OFFSET (HooxCodeSliceElement, slice)))

#if HX_SIZEOF_VOID_P == 8
# define HOOX_CODE_DEFLECTOR_CAVE_SIZE 24
# define HOOX_MAX_CODE_DEFLECTOR_THUNK_SIZE 128
#else
# define HOOX_CODE_DEFLECTOR_CAVE_SIZE 8
# define HOOX_MAX_CODE_DEFLECTOR_THUNK_SIZE 64
#endif

typedef struct _HooxCodePages HooxCodePages;
typedef struct _HooxCodeSliceElement HooxCodeSliceElement;
typedef struct _HooxCodeDeflectorDispatcher HooxCodeDeflectorDispatcher;
typedef struct _HooxCodeDeflectorImpl HooxCodeDeflectorImpl;
typedef struct _HooxProbeRangeForCodeCaveContext HooxProbeRangeForCodeCaveContext;
typedef struct _HooxInsertDeflectorContext HooxInsertDeflectorContext;

struct _HooxCodeSliceElement
{
  HxList parent;
  HooxCodeSlice slice;
};

struct _HooxCodePages
{
  hx_int ref_count;

  HooxCodeSegment * segment;
  hx_pointer data;
  hx_pointer pc;
  hx_size size;

  HooxCodeAllocator * allocator;

  HooxCodeSliceElement elements[1];
};

struct _HooxCodeDeflectorDispatcher
{
  HxSList * callers;

  hx_pointer address;

  hx_pointer original_data;
  hx_size original_size;

  hx_pointer trampoline;
  hx_pointer thunk;
  hx_size thunk_size;
};

struct _HooxCodeDeflectorImpl
{
  HooxCodeDeflector parent;

  HooxCodeAllocator * allocator;
};

struct _HooxProbeRangeForCodeCaveContext
{
  const HooxAddressSpec * caller;

  HooxMemoryRange cave;
};

struct _HooxInsertDeflectorContext
{
  HooxAddress pc;
  hx_size max_size;
  hx_pointer return_address;
  hx_pointer dedicated_target;

  HooxCodeDeflectorDispatcher * dispatcher;
};

static HooxCodeSlice * hoox_code_allocator_try_alloc_batch_near (
    HooxCodeAllocator * self, const HooxAddressSpec * spec);

static void hoox_code_pages_unref (HooxCodePages * self);

static hx_boolean hoox_code_slice_is_near (const HooxCodeSlice * self,
    const HooxAddressSpec * spec);
static hx_boolean hoox_code_slice_is_aligned (const HooxCodeSlice * slice,
    hx_size alignment);

static HooxCodeDeflectorDispatcher * hoox_code_deflector_dispatcher_new (
    const HooxAddressSpec * caller, hx_pointer return_address,
    hx_pointer dedicated_target);
static void hoox_code_deflector_dispatcher_free (
    HooxCodeDeflectorDispatcher * dispatcher);
static void hoox_insert_deflector (hx_pointer cave,
    HooxInsertDeflectorContext * ctx);
static void hoox_write_thunk (hx_pointer thunk,
    HooxCodeDeflectorDispatcher * dispatcher);
static void hoox_remove_deflector (hx_pointer cave,
    HooxCodeDeflectorDispatcher * dispatcher);
static hx_pointer hoox_code_deflector_dispatcher_lookup (
    HooxCodeDeflectorDispatcher * self, hx_pointer return_address);

static hx_boolean hoox_probe_module_for_code_cave (HooxModule * module,
    hx_pointer user_data);

/* GObject boxed-type registrations dropped (no GLib type system). */

void
hoox_code_allocator_init (HooxCodeAllocator * allocator,
                         hx_size slice_size)
{
  allocator->slice_size = slice_size;
  allocator->pages_per_batch = 7;
  allocator->slices_per_batch =
      (allocator->pages_per_batch * hoox_query_page_size ()) / slice_size;
  allocator->pages_metadata_size = sizeof (HooxCodePages) +
      ((allocator->slices_per_batch - 1) * sizeof (HooxCodeSliceElement));

  allocator->uncommitted_pages = NULL;
  allocator->dirty_pages = hx_hash_table_new (NULL, NULL);
  allocator->free_slices = NULL;

  allocator->dispatchers = NULL;
}

void
hoox_code_allocator_free (HooxCodeAllocator * allocator)
{
  hx_slist_foreach (allocator->dispatchers,
      (HxFunc) hoox_code_deflector_dispatcher_free, NULL);
  hx_slist_free (allocator->dispatchers);
  allocator->dispatchers = NULL;

  hx_list_foreach (allocator->free_slices, (HxFunc) hoox_code_pages_unref, NULL);
  hx_hash_table_unref (allocator->dirty_pages);
  hx_slist_free (allocator->uncommitted_pages);
  allocator->uncommitted_pages = NULL;
  allocator->dirty_pages = NULL;
  allocator->free_slices = NULL;
}

HooxCodeSlice *
hoox_code_allocator_alloc_slice (HooxCodeAllocator * self)
{
  return hoox_code_allocator_try_alloc_slice_near (self, NULL, 0);
}

HooxCodeSlice *
hoox_code_allocator_try_alloc_slice_near (HooxCodeAllocator * self,
                                         const HooxAddressSpec * spec,
                                         hx_size alignment)
{
  HxList * cur;

  for (cur = self->free_slices; cur != NULL; cur = cur->next)
  {
    HooxCodeSliceElement * element = (HooxCodeSliceElement *) cur;
    HooxCodeSlice * slice = &element->slice;

    if (hoox_code_slice_is_near (slice, spec) &&
        hoox_code_slice_is_aligned (slice, alignment))
    {
      HooxCodePages * pages = element->parent.data;

      self->free_slices = hx_list_remove_link (self->free_slices, cur);

      hx_hash_table_add (self->dirty_pages, pages);

      return slice;
    }
  }

  return hoox_code_allocator_try_alloc_batch_near (self, spec);
}

void
hoox_code_allocator_commit (HooxCodeAllocator * self)
{
  hx_boolean rwx_supported, remap_supported;
  HxSList * cur;
  HxHashTableIter iter;
  hx_pointer key;

  rwx_supported = hoox_query_is_rwx_supported ();
  remap_supported = hoox_memory_can_remap_writable ();

  for (cur = self->uncommitted_pages; cur != NULL; cur = cur->next)
  {
    HooxCodePages * pages = cur->data;
    HooxCodeSegment * segment = pages->segment;

    if (segment != NULL)
    {
      hoox_code_segment_realize (segment);
      hoox_code_segment_map (segment, 0,
          hoox_code_segment_get_virtual_size (segment),
          hoox_code_segment_get_address (segment));
    }
    else if (!remap_supported)
    {
      hoox_mprotect (pages->data, pages->size, HOOX_PAGE_RX);
    }
  }
  hx_slist_free (self->uncommitted_pages);
  self->uncommitted_pages = NULL;

  hx_hash_table_iter_init (&iter, self->dirty_pages);
  while (hx_hash_table_iter_next (&iter, &key, NULL))
  {
    HooxCodePages * pages = key;

    hoox_clear_cache (pages->data, pages->size);
  }
  hx_hash_table_remove_all (self->dirty_pages);

  if (!rwx_supported)
  {
    hx_list_foreach (self->free_slices, (HxFunc) hoox_code_pages_unref, NULL);
    self->free_slices = NULL;
  }
}

static HooxCodeSlice *
hoox_code_allocator_try_alloc_batch_near (HooxCodeAllocator * self,
                                         const HooxAddressSpec * spec)
{
  HooxCodeSlice * result = NULL;
  hx_boolean rwx_supported, code_segment_supported, remap_supported;
  hx_size page_size, size_in_pages, size_in_bytes;
  HooxCodeSegment * segment;
  hx_pointer data, pc;
  HooxCodePages * pages;
  hx_uint i;

  rwx_supported = hoox_query_is_rwx_supported ();
  code_segment_supported = hoox_code_segment_is_supported ();
  remap_supported = hoox_memory_can_remap_writable ();

  page_size = hoox_query_page_size ();
  size_in_pages = self->pages_per_batch;
  size_in_bytes = size_in_pages * page_size;

  if (rwx_supported || !code_segment_supported)
  {
    HooxPageProtection protection;
    HooxMemoryRange range;

    if (rwx_supported)
      protection = HOOX_PAGE_RWX;
    else
      protection = remap_supported ? HOOX_PAGE_RX : HOOX_PAGE_RW;

    segment = NULL;
    if (spec != NULL)
    {
      data = hoox_try_alloc_n_pages_near ((hx_uint) size_in_pages, protection, spec);
      if (data == NULL)
        return NULL;
    }
    else
    {
      data = hoox_alloc_n_pages ((hx_uint) size_in_pages, protection);
    }

    hoox_query_page_allocation_range (data, (hx_uint) size_in_bytes, &range);
    hoox_cloak_add_range (&range);

    pc = data;
    if (remap_supported)
      data = hoox_memory_try_remap_writable_pages (data, (hx_uint) size_in_pages);
  }
  else
  {
    segment = hoox_code_segment_new (size_in_bytes, spec);
    if (segment == NULL)
      return NULL;
    data = hoox_code_segment_get_address (segment);
    pc = data;
  }

  pages = hx_slice_alloc (self->pages_metadata_size);
  pages->ref_count = (hx_int) self->slices_per_batch;

  pages->segment = segment;
  pages->data = data;
  pages->pc = pc;
  pages->size = size_in_bytes;

  pages->allocator = self;

  for (i = (hx_uint) self->slices_per_batch; i != 0; i--)
  {
    hx_uint slice_index = i - 1;
    HooxCodeSliceElement * element = &pages->elements[slice_index];
    HxList * link;
    HooxCodeSlice * slice;

    slice = &element->slice;
    slice->data = (hx_uint8 *) data + (slice_index * self->slice_size);
    slice->pc = (hx_uint8 *) pc + (slice_index * self->slice_size);
    slice->size = (hx_uint) self->slice_size;
    slice->ref_count = 1;

    link = &element->parent;
    link->data = pages;
    link->prev = NULL;
    if (slice_index == 0)
    {
      link->next = NULL;
      result = slice;
    }
    else
    {
      if (self->free_slices != NULL)
        self->free_slices->prev = link;
      link->next = self->free_slices;
      self->free_slices = link;
    }
  }

  if (!rwx_supported)
    self->uncommitted_pages = hx_slist_prepend (self->uncommitted_pages, pages);

  hx_hash_table_add (self->dirty_pages, pages);

  return result;
}

static void
hoox_code_pages_unref (HooxCodePages * self)
{
  self->ref_count--;
  if (self->ref_count == 0)
  {
    if (self->segment != NULL)
    {
      hoox_code_segment_free (self->segment);
    }
    else
    {
      HooxMemoryRange range;

      if (self->pc != self->data)
      {
        hx_uint size_in_pages;

        size_in_pages = (hx_uint) (self->size / hoox_query_page_size ());
        hoox_memory_dispose_writable_pages (self->data, size_in_pages);

        hoox_free_pages (self->pc);
      }
      else
      {
        hoox_free_pages (self->data);
      }

      hoox_query_page_allocation_range (self->pc, (hx_uint) self->size, &range);
      hoox_cloak_remove_range (&range);
    }

    hx_slice_free1 (self->allocator->pages_metadata_size, self);
  }
}

HooxCodeSlice *
hoox_code_slice_ref (HooxCodeSlice * slice)
{
  hx_atomic_int_inc (&slice->ref_count);

  return slice;
}

void
hoox_code_slice_unref (HooxCodeSlice * slice)
{
  HooxCodeSliceElement * element;
  HooxCodePages * pages;

  if (slice == NULL)
    return;

  if (!hx_atomic_int_dec_and_test (&slice->ref_count))
    return;

  element = HOOX_CODE_SLICE_ELEMENT_FROM_SLICE (slice);
  pages = element->parent.data;

  if (hoox_query_is_rwx_supported ())
  {
    HooxCodeAllocator * allocator = pages->allocator;
    HxList * link = &element->parent;

    if (allocator->free_slices != NULL)
      allocator->free_slices->prev = link;
    link->next = allocator->free_slices;
    allocator->free_slices = link;
  }
  else
  {
    hoox_code_pages_unref (pages);
  }
}

static hx_boolean
hoox_code_slice_is_near (const HooxCodeSlice * self,
                        const HooxAddressSpec * spec)
{
  hx_ssize near_address;
  hx_ssize slice_start, slice_end;
  hx_size distance_start, distance_end;

  if (spec == NULL)
    return TRUE;

  near_address = (hx_ssize) spec->near_address;

  slice_start = (hx_ssize) self->pc;
  slice_end = slice_start + self->size - 1;

  distance_start = ABS (near_address - slice_start);
  distance_end = ABS (near_address - slice_end);

  return distance_start <= spec->max_distance &&
      distance_end <= spec->max_distance;
}

static hx_boolean
hoox_code_slice_is_aligned (const HooxCodeSlice * slice,
                           hx_size alignment)
{
  if (alignment == 0)
    return TRUE;

  return HX_POINTER_TO_SIZE (slice->pc) % alignment == 0;
}

HooxCodeDeflector *
hoox_code_allocator_alloc_deflector (HooxCodeAllocator * self,
                                    const HooxAddressSpec * caller,
                                    hx_pointer return_address,
                                    hx_pointer target,
                                    hx_boolean dedicated)
{
  HooxCodeDeflectorDispatcher * dispatcher = NULL;
  HxSList * cur;
  HooxCodeDeflectorImpl * impl;
  HooxCodeDeflector * deflector;

  if (!dedicated)
  {
    for (cur = self->dispatchers; cur != NULL; cur = cur->next)
    {
      HooxCodeDeflectorDispatcher * d = cur->data;
      hx_size distance;

      distance = ABS ((hx_ssize) HX_POINTER_TO_SIZE (d->address) -
          (hx_ssize) caller->near_address);
      if (distance <= caller->max_distance)
      {
        dispatcher = d;
        break;
      }
    }
  }

  if (dispatcher == NULL)
  {
    dispatcher = hoox_code_deflector_dispatcher_new (caller, return_address,
        dedicated ? target : NULL);
    if (dispatcher == NULL)
      return NULL;
    self->dispatchers = hx_slist_prepend (self->dispatchers, dispatcher);
  }

  impl = hx_slice_new (HooxCodeDeflectorImpl);

  deflector = &impl->parent;
  deflector->return_address = return_address;
  deflector->target = target;
  deflector->trampoline = dispatcher->trampoline;
  deflector->ref_count = 1;

  impl->allocator = self;

  dispatcher->callers = hx_slist_prepend (dispatcher->callers, deflector);

  return deflector;
}

HooxCodeDeflector *
hoox_code_deflector_ref (HooxCodeDeflector * deflector)
{
  hx_atomic_int_inc (&deflector->ref_count);

  return deflector;
}

void
hoox_code_deflector_unref (HooxCodeDeflector * deflector)
{
  HooxCodeDeflectorImpl * impl = (HooxCodeDeflectorImpl *) deflector;
  HooxCodeAllocator * allocator;
  HxSList * cur;

  if (deflector == NULL)
    return;

  if (!hx_atomic_int_dec_and_test (&deflector->ref_count))
    return;

  allocator = impl->allocator;

  for (cur = allocator->dispatchers; cur != NULL; cur = cur->next)
  {
    HooxCodeDeflectorDispatcher * dispatcher = cur->data;
    HxSList * entry;

    entry = hx_slist_find (dispatcher->callers, deflector);
    if (entry != NULL)
    {
      hx_slice_free (HooxCodeDeflectorImpl, impl);

      dispatcher->callers = hx_slist_delete_link (dispatcher->callers, entry);
      if (dispatcher->callers == NULL)
      {
        hoox_code_deflector_dispatcher_free (dispatcher);
        allocator->dispatchers = hx_slist_remove (allocator->dispatchers,
            dispatcher);
      }

      return;
    }
  }

  hx_assert_not_reached ();
}

static HooxCodeDeflectorDispatcher *
hoox_code_deflector_dispatcher_new (const HooxAddressSpec * caller,
                                   hx_pointer return_address,
                                   hx_pointer dedicated_target)
{
#if defined (HAVE_DARWIN) || (defined (HAVE_ELF) && HX_SIZEOF_VOID_P == 4)
  HooxCodeDeflectorDispatcher * dispatcher;
  HooxProbeRangeForCodeCaveContext probe_ctx;
  HooxInsertDeflectorContext insert_ctx;
  hx_boolean remap_supported;

  remap_supported = hoox_memory_can_remap_writable ();

  probe_ctx.caller = caller;

  probe_ctx.cave.base_address = 0;
  probe_ctx.cave.size = 0;

  hoox_process_enumerate_modules (hoox_probe_module_for_code_cave, &probe_ctx);

  if (probe_ctx.cave.base_address == 0)
    return NULL;

  dispatcher = hx_slice_new0 (HooxCodeDeflectorDispatcher);

  dispatcher->address = HX_SIZE_TO_POINTER (probe_ctx.cave.base_address);

  dispatcher->original_data = hx_memdup (dispatcher->address,
      probe_ctx.cave.size);
  dispatcher->original_size = probe_ctx.cave.size;

  if (dedicated_target == NULL)
  {
    hx_size thunk_size;
    HooxMemoryRange range;
    HooxPageProtection protection;

    thunk_size = hoox_query_page_size ();
    protection = remap_supported ? HOOX_PAGE_RX : HOOX_PAGE_RW;

    dispatcher->thunk =
        hoox_memory_allocate (NULL, thunk_size, thunk_size, protection);
    dispatcher->thunk_size = thunk_size;

    hoox_memory_patch_code (dispatcher->thunk, HOOX_MAX_CODE_DEFLECTOR_THUNK_SIZE,
        (HooxMemoryPatchApplyFunc) hoox_write_thunk, dispatcher);

    range.base_address = HOOX_ADDRESS (dispatcher->thunk);
    range.size = thunk_size;
    hoox_cloak_add_range (&range);
  }

  insert_ctx.pc = HOOX_ADDRESS (dispatcher->address);
  insert_ctx.max_size = dispatcher->original_size;
  insert_ctx.return_address = return_address;
  insert_ctx.dedicated_target = dedicated_target;

  insert_ctx.dispatcher = dispatcher;

  hoox_memory_patch_code (dispatcher->address, dispatcher->original_size,
      (HooxMemoryPatchApplyFunc) hoox_insert_deflector, &insert_ctx);

  return dispatcher;
#else
  (void) hoox_insert_deflector;
  (void) hoox_write_thunk;
  (void) hoox_probe_module_for_code_cave;

  return NULL;
#endif
}

static void
hoox_code_deflector_dispatcher_free (HooxCodeDeflectorDispatcher * dispatcher)
{
  hoox_memory_patch_code (dispatcher->address, dispatcher->original_size,
      (HooxMemoryPatchApplyFunc) hoox_remove_deflector, dispatcher);

  if (dispatcher->thunk != NULL)
  {
    HooxMemoryRange range;

    hoox_memory_release (dispatcher->thunk, dispatcher->thunk_size);

    range.base_address = HOOX_ADDRESS (dispatcher->thunk);
    range.size = dispatcher->thunk_size;
    hoox_cloak_remove_range (&range);
  }

  hx_free (dispatcher->original_data);

  hx_slist_foreach (dispatcher->callers, (HxFunc) hoox_code_deflector_unref, NULL);
  hx_slist_free (dispatcher->callers);

  hx_slice_free (HooxCodeDeflectorDispatcher, dispatcher);
}

static void
hoox_insert_deflector (hx_pointer cave,
                      HooxInsertDeflectorContext * ctx)
{
# if defined (HAVE_ARM)
  HooxCodeDeflectorDispatcher * dispatcher = ctx->dispatcher;
  HooxThumbWriter tw;

  if (ctx->dedicated_target != NULL)
  {
    hx_boolean owner_is_arm;

    owner_is_arm = (HX_POINTER_TO_SIZE (ctx->return_address) & 1) == 0;
    if (owner_is_arm)
    {
      HooxArmWriter aw;

      hoox_arm_writer_init (&aw, cave);
      aw.cpu_features = hoox_query_cpu_features ();
      aw.pc = ctx->pc;
      hoox_arm_writer_put_ldr_reg_address (&aw, HX_ARM_REG_PC,
          HOOX_ADDRESS (ctx->dedicated_target));
      hoox_arm_writer_flush (&aw);
      hx_assert (hoox_arm_writer_offset (&aw) <= ctx->max_size);
      hoox_arm_writer_clear (&aw);

      dispatcher->trampoline = HX_SIZE_TO_POINTER (ctx->pc);

      return;
    }

    hoox_thumb_writer_init (&tw, cave);
    tw.pc = ctx->pc;
    hoox_thumb_writer_put_ldr_reg_address (&tw, HX_ARM_REG_PC,
        HOOX_ADDRESS (ctx->dedicated_target));
  }
  else
  {
    hoox_thumb_writer_init (&tw, cave);
    tw.pc = ctx->pc;
    hoox_thumb_writer_put_ldr_reg_address (&tw, HX_ARM_REG_PC,
        HOOX_ADDRESS (dispatcher->thunk) + 1);
  }

  hoox_thumb_writer_flush (&tw);
  hx_assert (hoox_thumb_writer_offset (&tw) <= ctx->max_size);
  hoox_thumb_writer_clear (&tw);

  dispatcher->trampoline = HX_SIZE_TO_POINTER (ctx->pc + 1);
# elif defined (HAVE_ARM64)
  HooxCodeDeflectorDispatcher * dispatcher = ctx->dispatcher;
  HooxArm64Writer aw;

  hoox_arm64_writer_init (&aw, cave);
  aw.pc = ctx->pc;

  if (ctx->dedicated_target != NULL)
  {
    hoox_arm64_writer_put_push_reg_reg (&aw, HX_ARM64_REG_X0, HX_ARM64_REG_LR);
    hoox_arm64_writer_put_ldr_reg_address (&aw, HX_ARM64_REG_X0,
        HOOX_ADDRESS (ctx->dedicated_target));
    hoox_arm64_writer_put_br_reg (&aw, HX_ARM64_REG_X0);
  }
  else
  {
    hoox_arm64_writer_put_ldr_reg_address (&aw, HX_ARM64_REG_X0,
        HOOX_ADDRESS (hoox_sign_code_pointer (dispatcher->thunk)));
    hoox_arm64_writer_put_br_reg (&aw, HX_ARM64_REG_X0);
  }

  hoox_arm64_writer_flush (&aw);
  hx_assert (hoox_arm64_writer_offset (&aw) <= ctx->max_size);
  hoox_arm64_writer_clear (&aw);

  dispatcher->trampoline = HX_SIZE_TO_POINTER (ctx->pc);
# else
  (void) hoox_code_deflector_dispatcher_lookup;
# endif
}

static void
hoox_write_thunk (hx_pointer thunk,
                 HooxCodeDeflectorDispatcher * dispatcher)
{
# if defined (HAVE_ARM)
  HooxThumbWriter tw;

  hoox_thumb_writer_init (&tw, thunk);
  tw.pc = HOOX_ADDRESS (dispatcher->thunk);

  hoox_thumb_writer_put_push_regs (&tw, 2, HX_ARM_REG_R9, HX_ARM_REG_R12);

  hoox_thumb_writer_put_call_address_with_arguments (&tw,
      HOOX_ADDRESS (hoox_code_deflector_dispatcher_lookup), 2,
      HOOX_ARG_ADDRESS, HOOX_ADDRESS (dispatcher),
      HOOX_ARG_REGISTER, HX_ARM_REG_LR);

  hoox_thumb_writer_put_pop_regs (&tw, 2, HX_ARM_REG_R9, HX_ARM_REG_R12);

  hoox_thumb_writer_put_bx_reg (&tw, HX_ARM_REG_R0);
  hoox_thumb_writer_clear (&tw);
# elif defined (HAVE_ARM64)
  HooxArm64Writer aw;

  hoox_arm64_writer_init (&aw, thunk);
  aw.pc = HOOX_ADDRESS (dispatcher->thunk);

  /* push {q0-q7} */
  hoox_arm64_writer_put_instruction (&aw, 0xadbf1fe6);
  hoox_arm64_writer_put_instruction (&aw, 0xadbf17e4);
  hoox_arm64_writer_put_instruction (&aw, 0xadbf0fe2);
  hoox_arm64_writer_put_instruction (&aw, 0xadbf07e0);

  hoox_arm64_writer_put_push_reg_reg (&aw, HX_ARM64_REG_X17, HX_ARM64_REG_X18);
  hoox_arm64_writer_put_push_reg_reg (&aw, HX_ARM64_REG_X15, HX_ARM64_REG_X16);
  hoox_arm64_writer_put_push_reg_reg (&aw, HX_ARM64_REG_X13, HX_ARM64_REG_X14);
  hoox_arm64_writer_put_push_reg_reg (&aw, HX_ARM64_REG_X11, HX_ARM64_REG_X12);
  hoox_arm64_writer_put_push_reg_reg (&aw, HX_ARM64_REG_X9, HX_ARM64_REG_X10);
  hoox_arm64_writer_put_push_reg_reg (&aw, HX_ARM64_REG_X7, HX_ARM64_REG_X8);
  hoox_arm64_writer_put_push_reg_reg (&aw, HX_ARM64_REG_X5, HX_ARM64_REG_X6);
  hoox_arm64_writer_put_push_reg_reg (&aw, HX_ARM64_REG_X3, HX_ARM64_REG_X4);
  hoox_arm64_writer_put_push_reg_reg (&aw, HX_ARM64_REG_X1, HX_ARM64_REG_X2);

  hoox_arm64_writer_put_call_address_with_arguments (&aw,
      HOOX_ADDRESS (hoox_code_deflector_dispatcher_lookup), 2,
      HOOX_ARG_ADDRESS, HOOX_ADDRESS (dispatcher),
      HOOX_ARG_REGISTER, HX_ARM64_REG_LR);

  hoox_arm64_writer_put_pop_reg_reg (&aw, HX_ARM64_REG_X1, HX_ARM64_REG_X2);
  hoox_arm64_writer_put_pop_reg_reg (&aw, HX_ARM64_REG_X3, HX_ARM64_REG_X4);
  hoox_arm64_writer_put_pop_reg_reg (&aw, HX_ARM64_REG_X5, HX_ARM64_REG_X6);
  hoox_arm64_writer_put_pop_reg_reg (&aw, HX_ARM64_REG_X7, HX_ARM64_REG_X8);
  hoox_arm64_writer_put_pop_reg_reg (&aw, HX_ARM64_REG_X9, HX_ARM64_REG_X10);
  hoox_arm64_writer_put_pop_reg_reg (&aw, HX_ARM64_REG_X11, HX_ARM64_REG_X12);
  hoox_arm64_writer_put_pop_reg_reg (&aw, HX_ARM64_REG_X13, HX_ARM64_REG_X14);
  hoox_arm64_writer_put_pop_reg_reg (&aw, HX_ARM64_REG_X15, HX_ARM64_REG_X16);
  hoox_arm64_writer_put_pop_reg_reg (&aw, HX_ARM64_REG_X17, HX_ARM64_REG_X18);

  /* pop {q0-q7} */
  hoox_arm64_writer_put_instruction (&aw, 0xacc107e0);
  hoox_arm64_writer_put_instruction (&aw, 0xacc10fe2);
  hoox_arm64_writer_put_instruction (&aw, 0xacc117e4);
  hoox_arm64_writer_put_instruction (&aw, 0xacc11fe6);

  hoox_arm64_writer_put_br_reg (&aw, HX_ARM64_REG_X0);
  hoox_arm64_writer_clear (&aw);
# else
  (void) hoox_code_deflector_dispatcher_lookup;
# endif
}

static void
hoox_remove_deflector (hx_pointer cave,
                      HooxCodeDeflectorDispatcher * dispatcher)
{
  memcpy (cave, dispatcher->original_data, dispatcher->original_size);
}

static hx_pointer
hoox_code_deflector_dispatcher_lookup (HooxCodeDeflectorDispatcher * self,
                                      hx_pointer return_address)
{
  HxSList * cur;

  for (cur = self->callers; cur != NULL; cur = cur->next)
  {
    HooxCodeDeflector * caller = cur->data;

    if (caller->return_address == return_address)
      return caller->target;
  }

  return NULL;
}

static hx_boolean
hoox_probe_module_for_code_cave (HooxModule * module,
                                hx_pointer user_data)
{
  HooxProbeRangeForCodeCaveContext * ctx = user_data;
  const HooxAddressSpec * caller = ctx->caller;
  const HooxMemoryRange * range;
  HooxAddress header_address, cave_address;
  hx_size distance;
  const hx_uint8 empty_cave[HOOX_CODE_DEFLECTOR_CAVE_SIZE] = { 0, };

  range = hoox_module_get_range (module);
  header_address = range->base_address;

#ifdef HAVE_DARWIN
  cave_address = header_address + 4096 - sizeof (empty_cave);
#else
  cave_address = header_address + 8;
#endif

  distance = ABS ((hx_ssize) cave_address - (hx_ssize) caller->near_address);
  if (distance > caller->max_distance)
    return TRUE;

  if (memcmp (HX_SIZE_TO_POINTER (cave_address), empty_cave,
      sizeof (empty_cave)) != 0)
  {
#ifdef HAVE_DARWIN
    hx_boolean found_empty_cave, nothing_in_front_of_cave;

    found_empty_cave = FALSE;
    nothing_in_front_of_cave = TRUE;

    do
    {
      cave_address -= sizeof (empty_cave);

      found_empty_cave = memcmp (HX_SIZE_TO_POINTER (cave_address), empty_cave,
          sizeof (empty_cave)) == 0;
    }
    while (!found_empty_cave && cave_address > header_address + 0x500);

    if (found_empty_cave)
    {
      hx_size offset;

      for (offset = sizeof (empty_cave);
          offset <= 2 * sizeof (empty_cave);
          offset += sizeof (empty_cave))
      {
        nothing_in_front_of_cave = memcmp (
            HX_SIZE_TO_POINTER (cave_address - offset), empty_cave,
            sizeof (empty_cave)) == 0;
      }
    }

    if (!(found_empty_cave && nothing_in_front_of_cave))
      return TRUE;
#else
    return TRUE;
#endif
  }

  ctx->cave.base_address = cave_address;
  ctx->cave.size = sizeof (empty_cave);
  return FALSE;
}
