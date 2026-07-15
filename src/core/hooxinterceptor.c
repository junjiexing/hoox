/*
 * Copyright (C) 2008-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2008 Christian Berentsen <jc.berentsen@gmail.com>
 * Copyright (C) 2024-2025 Francesco Tamagni <mrmacete@protonmail.ch>
 * Copyright (C) 2024 Yannis Juglaret <yjuglaret@mozilla.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxinterceptor.h"

#include "hooxcodesegment.h"
#include "hooxinterceptor-priv.h"
#include "hooxmemory.h"
#include "hooxmetalarray.h"
#include "hooxprocess-priv.h"
#include "hooxtls.h"

#include <string.h>
#ifdef HAVE_DARWIN
# include <mach/mach.h>
#endif

#define HOOX_INTERCEPTOR_CODE_SLICE_SIZE 256

#define HOOX_INTERCEPTOR_LOCK(o) hx_rec_mutex_lock (&(o)->mutex)
#define HOOX_INTERCEPTOR_UNLOCK(o) hx_rec_mutex_unlock (&(o)->mutex)

#if defined (HAVE_I386)
# define HOOX_INTERCEPTOR_CPU_CONTEXT_SP(c) \
    ((hx_pointer) HOOX_CPU_CONTEXT_XSP (c))
#else
# define HOOX_INTERCEPTOR_CPU_CONTEXT_SP(c) ((hx_pointer) (c)->sp)
#endif

typedef struct _HooxInterceptorTransaction HooxInterceptorTransaction;
typedef hx_uint HooxInstrumentationError;
typedef struct _HooxDestroyTask HooxDestroyTask;
typedef struct _HooxUpdateTask HooxUpdateTask;
typedef struct _ListenerEntry ListenerEntry;
typedef struct _InterceptorThreadContext InterceptorThreadContext;
typedef struct _HooxInvocationStackEntry HooxInvocationStackEntry;
typedef struct _ListenerDataSlot ListenerDataSlot;
typedef struct _ListenerInvocationState ListenerInvocationState;

typedef void (* HooxUpdateTaskFunc) (HooxInterceptor * self,
    HooxFunctionContext * ctx, hx_pointer prologue);

struct _HooxInterceptorTransaction
{
  hx_boolean is_dirty;
  hx_int level;
  HxQueue * pending_destroy_tasks;
  HxHashTable * pending_update_tasks;

  HooxInterceptor * interceptor;
};

struct _HooxInterceptor
{
  hx_int ref_count;

  HxRecMutex mutex;

  HxHashTable * function_by_address;

  HooxInterceptorBackend * backend;
  HooxCodeAllocator allocator;

  HooxInterceptorOptions options;

  volatile hx_size selected_thread_id;

  HooxInterceptorTransaction current_transaction;
};

enum _HooxInstrumentationError
{
  HOOX_INSTRUMENTATION_ERROR_NONE,
  HOOX_INSTRUMENTATION_ERROR_WRONG_SIGNATURE,
  HOOX_INSTRUMENTATION_ERROR_POLICY_VIOLATION,
  HOOX_INSTRUMENTATION_ERROR_WRONG_TYPE,
};

struct _HooxDestroyTask
{
  HooxFunctionContext * ctx;
  HxDestroyNotify notify;
  hx_pointer data;
};

struct _HooxUpdateTask
{
  HooxFunctionContext * ctx;
  HooxUpdateTaskFunc func;
};

struct _ListenerEntry
{
  const HooxInvocationListenerInterface * listener_interface;
  HooxInvocationListener * listener_instance;
  hx_pointer function_data;
  hx_boolean unignorable;
};

struct _InterceptorThreadContext
{
  hx_uint generation;

  HooxInvocationBackend listener_backend;
  HooxInvocationBackend replacement_backend;

  hx_int ignore_level;

  HooxInvocationStack * stack;

  HxArray * listener_data_slots;
};

struct _HooxInvocationStackEntry
{
  HooxFunctionContext * function_ctx;
  hx_pointer caller_ret_addr;
  hx_pointer stack_address;
  HooxInvocationContext invocation_context;
  HooxCpuContext cpu_context;
  HxPtrArray * listener_entries;
  hx_uint8 listener_invocation_data[HOOX_MAX_LISTENERS_PER_FUNCTION]
      [HOOX_MAX_LISTENER_DATA];
  hx_boolean calling_replacement;
  hx_boolean only_invoke_unignorable_listeners;
  hx_int original_system_error;
};

struct _ListenerDataSlot
{
  HooxInvocationListener * owner;
  hx_uint8 data[HOOX_MAX_LISTENER_DATA];
};

struct _ListenerInvocationState
{
  HooxPointCut point_cut;
  ListenerEntry * entry;
  InterceptorThreadContext * interceptor_ctx;
  hx_uint8 * invocation_data;
};

static HooxInterceptor * hoox_interceptor_new (void);
static void hoox_interceptor_finalize (HooxInterceptor * self);

static HooxReplaceReturn hoox_interceptor_replace_with_type (
    HooxInterceptor * self, HooxInterceptorType type, hx_pointer function_address,
    hx_pointer replacement_function, hx_pointer replacement_data,
    hx_pointer * original_function, const HooxInterceptorOptions * options);
static HooxFunctionContext * hoox_interceptor_instrument (HooxInterceptor * self,
    HooxInterceptorType type, hx_pointer function_address,
    const HooxInterceptorOptions * instrumentation,
    HooxInstrumentationError * error);
static void hoox_interceptor_activate (HooxInterceptor * self,
    HooxFunctionContext * ctx, hx_pointer prologue);
static void hoox_interceptor_deactivate (HooxInterceptor * self,
    HooxFunctionContext * ctx, hx_pointer prologue);

static void hoox_interceptor_transaction_init (
    HooxInterceptorTransaction * transaction, HooxInterceptor * interceptor);
static void hoox_interceptor_transaction_destroy (
    HooxInterceptorTransaction * transaction);
static void hoox_interceptor_transaction_begin (
    HooxInterceptorTransaction * self);
static void hoox_interceptor_transaction_end (HooxInterceptorTransaction * self);
static void hoox_apply_updates (hx_pointer source_page, hx_pointer target_page,
    hx_uint n_pages, hx_pointer user_data);
static void hoox_interceptor_transaction_schedule_destroy (
    HooxInterceptorTransaction * self, HooxFunctionContext * ctx,
    HxDestroyNotify notify, hx_pointer data);
static void hoox_interceptor_transaction_schedule_update (
    HooxInterceptorTransaction * self, HooxFunctionContext * ctx,
    HooxUpdateTaskFunc func);

static HooxFunctionContext * hoox_function_context_new (
    HooxInterceptor * interceptor, hx_pointer function_address,
    HooxInterceptorType type);
static void hoox_function_context_finalize (HooxFunctionContext * function_ctx);
static void hoox_function_context_destroy (HooxFunctionContext * function_ctx);
static void hoox_function_context_perform_destroy (
    HooxFunctionContext * function_ctx);
static hx_boolean hoox_function_context_is_empty (
    HooxFunctionContext * function_ctx);
static void hoox_function_context_add_listener (
    HooxFunctionContext * function_ctx, HooxInvocationListener * listener,
    hx_pointer function_data, hx_boolean unignorable);
static void hoox_function_context_remove_listener (
    HooxFunctionContext * function_ctx, HooxInvocationListener * listener);
static void listener_entry_free (ListenerEntry * entry);
static hx_boolean hoox_function_context_has_listener (
    HooxFunctionContext * function_ctx, HooxInvocationListener * listener);
static hx_uint hoox_function_context_count_listeners (
    HooxFunctionContext * function_ctx);
static ListenerEntry ** hoox_function_context_find_listener (
    HooxFunctionContext * function_ctx, HooxInvocationListener * listener);
static ListenerEntry ** hoox_function_context_find_taken_listener_slot (
    HooxFunctionContext * function_ctx);
static void hoox_function_context_fixup_cpu_context (
    HooxFunctionContext * function_ctx, HooxCpuContext * cpu_context);

static InterceptorThreadContext * get_interceptor_thread_context (void);
static void release_interceptor_thread_context (
    InterceptorThreadContext * context);
static void hoox_interceptor_recover_from_fork (hx_boolean in_child);
static void hoox_interceptor_prune_thread_contexts_after_fork (void);
static void hoox_interceptor_repair_usage_counters_after_fork (
    HooxInterceptor * interceptor, InterceptorThreadContext * current_context);
static InterceptorThreadContext * interceptor_thread_context_new (void);
static void interceptor_thread_context_destroy (
    InterceptorThreadContext * context);
static hx_pointer interceptor_thread_context_get_listener_data (
    InterceptorThreadContext * self, HooxInvocationListener * listener,
    hx_size required_size);
static void interceptor_thread_context_forget_listener_data (
    InterceptorThreadContext * self, HooxInvocationListener * listener);
static HooxInvocationStackEntry * hoox_invocation_stack_push (
    HooxInvocationStack * stack, HooxFunctionContext * function_ctx,
    hx_pointer caller_ret_addr, hx_pointer stack_address,
    hx_boolean only_invoke_unignorable_listeners);
static hx_pointer hoox_invocation_stack_pop (HooxInvocationStack * stack);
static void hoox_invocation_stack_reap_unwound (HooxInvocationStack * stack,
    hx_pointer live_stack_address);
static void hoox_invocation_stack_reap_unwound_above (
    HooxInvocationStack * stack, HooxFunctionContext * returning_ctx);
static hx_boolean hoox_invocation_stack_entry_was_unwound_past (
    const HooxInvocationStackEntry * entry, hx_pointer live_stack_address);
static void hoox_invocation_stack_entry_release_trampoline (
    const HooxInvocationStackEntry * entry);
static HooxInvocationStackEntry * hoox_invocation_stack_peek_top (
    HooxInvocationStack * stack);

static hx_pointer hoox_interceptor_resolve (HooxInterceptor * self,
    hx_pointer address);
static hx_boolean hoox_interceptor_has (HooxInterceptor * self,
    hx_pointer function_address);

static hx_pointer hoox_page_address_from_pointer (hx_pointer ptr);
static hx_int hoox_page_address_compare (hx_constpointer * a, hx_constpointer * b);

/**
 * HooxInterceptor:
 *
 * Intercepts execution through inline hooking.
 *
 * Three complementary mechanisms are offered:
 *
 * - *Attaching* a [iface@Hoox.InvocationListener] to a function, to be notified
 *   right before it is entered and right after it returns, while leaving the
 *   original function in place. This is the classic enter/leave hook.
 * - *Probing* a single point in the code with a listener that implements only
 *   `on_enter`, typically one from [func@Hoox.make_probe_listener]. Because the
 *   target is an arbitrary address rather than a function entry, a probe can be
 *   placed in the middle of a function to observe execution reaching a specific
 *   instruction. A call listener given a %NULL `on_leave` is not equivalent: it
 *   still traps the return and counts toward
 *   [method@Hoox.InvocationContext.get_depth].
 * - *Replacing* a function outright with your own implementation. Your
 *   replacement can still reach the original by calling the function's own
 *   address: the interceptor routes such a call to the original instead of
 *   recursing back into the replacement. (The lighter
 *   [method@Hoox.Interceptor.replace_fast] instead hands you a dedicated
 *   pointer for this.)
 *
 * A batch of changes can be grouped into a transaction so that they are
 * activated as a unit, which is both faster and atomic from the target's point
 * of view; see [method@Hoox.Interceptor.begin_transaction].
 *
 * ## Attaching a listener
 *
 * ```c
 * static void on_enter (HooxInvocationContext * ic, hx_pointer user_data);
 * static void on_leave (HooxInvocationContext * ic, hx_pointer user_data);
 *
 * void
 * instrument (void)
 * {
 *   hx_autoptr(HooxInterceptor) interceptor = hoox_interceptor_obtain ();
 *   HooxInvocationListener * listener =
 *       hoox_make_call_listener (on_enter, on_leave, NULL, NULL);
 *
 *   hoox_interceptor_begin_transaction (interceptor);
 *   hoox_interceptor_attach (interceptor,
 *       HX_SIZE_TO_POINTER (hoox_module_find_global_export_by_name ("open")),
 *       listener, NULL);
 *   hoox_interceptor_end_transaction (interceptor);
 * }
 * ```
 *
 * ## Replacing a function
 *
 * ```c
 * static int (* libc_open) (const char * path, int oflag, ...);
 *
 * static int
 * replacement_open (const char * path, int oflag, ...)
 * {
 *   hx_printerr ("open(\"%s\")\n", path);
 *   return libc_open (path, oflag); // reaches the original
 * }
 *
 * void
 * instrument (void)
 * {
 *   hx_autoptr(HooxInterceptor) interceptor = hoox_interceptor_obtain ();
 *
 *   libc_open = HX_SIZE_TO_POINTER (
 *       hoox_module_find_global_export_by_name ("open"));
 *
 *   hoox_interceptor_replace (interceptor, libc_open, replacement_open,
 *       NULL, NULL);
 * }
 * ```
 *
 * ## Ahead-of-time instrumentation
 *
 * Inline hooking normally rewrites code at runtime, but some platforms forbid
 * that: where code signing is strictly enforced (e.g. iOS), executable pages
 * cannot be patched on the fly. For these, trampolines can be *grafted* into a
 * Mach-O binary ahead of time with [class@Hoox.DarwinGrafter] — exposed as the
 * `hoox-graft` command-line tool — which reserves a trampoline at each code
 * offset you intend to hook.
 *
 * At runtime [method@Hoox.Interceptor.attach] and
 * [method@Hoox.Interceptor.replace] then claim the matching grafted trampoline
 * instead of patching code, so interception works without writable code. If a
 * target has no grafted trampoline while code signing requires one, the attach
 * or replace fails with `HOOX_ATTACH_POLICY_VIOLATION` /
 * `HOOX_REPLACE_POLICY_VIOLATION`.
 */

/**
 * HooxInterceptorScenario:
 * @HOOX_INTERCEPTOR_SCENARIO_DEFAULT: use the interceptor's configured default
 * @HOOX_INTERCEPTOR_SCENARIO_ONLINE: other threads may be executing the target
 *   code, so it must be instrumented conservatively
 * @HOOX_INTERCEPTOR_SCENARIO_OFFLINE: the target is quiescent, allowing more
 *   aggressive rewriting
 *
 * Whether other threads may be running the code being instrumented. When the
 * target is known to be quiescent — for example a process freshly created with
 * `spawn()` whose main thread is still suspended before `main()` —
 * %HOOX_INTERCEPTOR_SCENARIO_OFFLINE lets it rewrite more freely, e.g.
 * overwriting past a `CALL` since no other thread can already be inside the
 * call waiting to return. %HOOX_INTERCEPTOR_SCENARIO_ONLINE is the safe choice
 * for live processes where such concurrency is possible.
 */

static HxMutex _hoox_interceptor_lock;
static HooxInterceptor * _the_interceptor = NULL;

static HooxSpinlock hoox_interceptor_thread_context_lock = HOOX_SPINLOCK_INIT;
static HxHashTable * hoox_interceptor_thread_contexts;
static hx_uint hoox_interceptor_generation;
static HxPrivate hoox_interceptor_context_private =
    HX_PRIVATE_INIT ((HxDestroyNotify) release_interceptor_thread_context);
static HooxTlsKey hoox_interceptor_guard_key;
static HooxInterceptor * hoox_interceptor_fork_interceptor;
static hx_boolean hoox_interceptor_fork_prepared;

static HooxInvocationStack _hoox_interceptor_empty_stack = { NULL, 0 };

void
_hoox_interceptor_init (void)
{
  HxHashTable * contexts;

  contexts = hx_hash_table_new (NULL, NULL);

  hoox_spinlock_acquire (&hoox_interceptor_thread_context_lock);
  hoox_interceptor_generation++;
  if (hoox_interceptor_generation == 0)
    hoox_interceptor_generation++;
  hoox_interceptor_thread_contexts = contexts;
  hoox_spinlock_release (&hoox_interceptor_thread_context_lock);

  hoox_interceptor_guard_key = hoox_tls_key_new ();
}

void
_hoox_interceptor_deinit (void)
{
  HxHashTable * contexts;
  InterceptorThreadContext * current_context;

  hoox_tls_key_free (hoox_interceptor_guard_key);

  hoox_spinlock_acquire (&hoox_interceptor_thread_context_lock);
  contexts = hoox_interceptor_thread_contexts;
  hoox_interceptor_thread_contexts = NULL;
  hoox_spinlock_release (&hoox_interceptor_thread_context_lock);

  hx_hash_table_unref (contexts);

  current_context = hx_private_get (&hoox_interceptor_context_private);
  if (current_context != NULL)
  {
    hx_private_set (&hoox_interceptor_context_private, NULL);
    interceptor_thread_context_destroy (current_context);
  }
}

void
_hoox_interceptor_prepare_to_fork (void)
{
  HooxInterceptor * interceptor;

  hx_assert (!hoox_interceptor_fork_prepared);

  for (;;)
  {
    /* Pin the singleton before waiting for its lock. Keeping the global lock
     * while waiting would invert finalize's instance -> global lock order. */
    hx_mutex_lock (&_hoox_interceptor_lock);
    interceptor = hoox_interceptor_ref (_the_interceptor);
    hx_mutex_unlock (&_hoox_interceptor_lock);

    if (interceptor != NULL)
      HOOX_INTERCEPTOR_LOCK (interceptor);

    hx_mutex_lock (&_hoox_interceptor_lock);
    if (_the_interceptor == interceptor)
      break;

    hx_mutex_unlock (&_hoox_interceptor_lock);
    if (interceptor != NULL)
    {
      HOOX_INTERCEPTOR_UNLOCK (interceptor);
      hoox_interceptor_unref (interceptor);
    }
  }

  /* No thread-context mutation may cross the fork snapshot. */
  hoox_spinlock_acquire (&hoox_interceptor_thread_context_lock);

  hoox_interceptor_fork_interceptor = interceptor;
  hoox_interceptor_fork_prepared = TRUE;
}

void
_hoox_interceptor_recover_from_fork_in_parent (void)
{
  hoox_interceptor_recover_from_fork (FALSE);
}

void
_hoox_interceptor_recover_from_fork_in_child (void)
{
  hoox_interceptor_recover_from_fork (TRUE);
}

static void
hoox_interceptor_recover_from_fork (hx_boolean in_child)
{
  HooxInterceptor * interceptor;

  hx_assert (hoox_interceptor_fork_prepared);

  interceptor = hoox_interceptor_fork_interceptor;

  if (in_child)
  {
    InterceptorThreadContext * current_context;

    current_context = hx_private_get (&hoox_interceptor_context_private);
    hoox_interceptor_prune_thread_contexts_after_fork ();
    hoox_interceptor_repair_usage_counters_after_fork (interceptor,
        current_context);
  }

  hoox_interceptor_fork_interceptor = NULL;
  hoox_interceptor_fork_prepared = FALSE;

  hoox_spinlock_release (&hoox_interceptor_thread_context_lock);
  if (in_child)
  {
    hx_mutex_recover_from_fork_in_child (&_hoox_interceptor_lock);
    if (interceptor != NULL)
      hx_rec_mutex_recover_from_fork_in_child (&interceptor->mutex);
  }
  else
  {
    hx_mutex_unlock (&_hoox_interceptor_lock);
    if (interceptor != NULL)
      HOOX_INTERCEPTOR_UNLOCK (interceptor);
  }

  hoox_interceptor_unref (interceptor);
}

static void
hoox_interceptor_prune_thread_contexts_after_fork (void)
{
  InterceptorThreadContext * current_context;
  HxHashTableIter iter;
  hx_pointer key;

  if (hoox_interceptor_thread_contexts == NULL)
    return;

  current_context = hx_private_get (&hoox_interceptor_context_private);

  hx_hash_table_iter_init (&iter, hoox_interceptor_thread_contexts);
  while (hx_hash_table_iter_next (&iter, &key, NULL))
  {
    InterceptorThreadContext * context = key;

    if (context == current_context)
      continue;

    hx_hash_table_iter_remove (&iter);
    interceptor_thread_context_destroy (context);
  }
}

static void
hoox_interceptor_repair_usage_counters_after_fork (
    HooxInterceptor * interceptor,
    InterceptorThreadContext * current_context)
{
  HxHashTableIter iter;
  hx_pointer value;
  HxList * cur;
  hx_uint i;

  if (interceptor == NULL)
    return;

  /* Counts contributed by threads that disappeared at fork can otherwise keep
   * detached trampolines alive forever. Rebuild them from the sole surviving
   * invocation stack. */
  hx_hash_table_iter_init (&iter, interceptor->function_by_address);
  while (hx_hash_table_iter_next (&iter, NULL, &value))
  {
    HooxFunctionContext * function_ctx = value;

    hx_atomic_int_set (&function_ctx->trampoline_usage_counter, 0);
  }

  for (cur = interceptor->current_transaction.pending_destroy_tasks->head;
      cur != NULL;
      cur = cur->next)
  {
    HooxDestroyTask * task = cur->data;

    hx_atomic_int_set (&task->ctx->trampoline_usage_counter, 0);
  }

  if (current_context == NULL)
    return;

  for (i = 0; i != current_context->stack->len; i++)
  {
    HooxInvocationStackEntry * entry = &hx_array_index (
        current_context->stack, HooxInvocationStackEntry, i);

    hx_atomic_int_set (&entry->function_ctx->trampoline_usage_counter, 0);
  }
  for (i = 0; i != current_context->stack->len; i++)
  {
    HooxInvocationStackEntry * entry = &hx_array_index (
        current_context->stack, HooxInvocationStackEntry, i);

    hx_atomic_int_inc (&entry->function_ctx->trampoline_usage_counter);
  }
}

static void
hoox_interceptor_init (HooxInterceptor * self)
{
  hx_rec_mutex_init (&self->mutex);

  self->function_by_address = hx_hash_table_new_full (NULL, NULL, NULL,
      (HxDestroyNotify) hoox_function_context_destroy);

  hoox_code_allocator_init (&self->allocator, HOOX_INTERCEPTOR_CODE_SLICE_SIZE);

  self->options.scenario = HOOX_INTERCEPTOR_SCENARIO_ONLINE;
  self->options.relocation_policy = HOOX_RELOCATION_CHECKED;

  hoox_interceptor_transaction_init (&self->current_transaction, self);
}

static HooxInterceptor *
hoox_interceptor_new (void)
{
  HooxInterceptor * self = hx_new0 (HooxInterceptor, 1);
  self->ref_count = 1;
  hoox_interceptor_init (self);
  return self;
}

HooxInterceptor *
hoox_interceptor_ref (HooxInterceptor * self)
{
  if (self != NULL)
    hx_atomic_int_inc (&self->ref_count);
  return self;
}

void
hoox_interceptor_unref (HooxInterceptor * self)
{
  if (self == NULL)
    return;

  if (hx_atomic_int_dec_and_test (&self->ref_count))
    hoox_interceptor_finalize (self);
}

static void
hoox_interceptor_finalize (HooxInterceptor * self)
{
  /* Formerly the GObject dispose phase. */
  HOOX_INTERCEPTOR_LOCK (self);
  hoox_interceptor_transaction_begin (&self->current_transaction);
  self->current_transaction.is_dirty = TRUE;

  hx_hash_table_remove_all (self->function_by_address);

  hoox_interceptor_transaction_end (&self->current_transaction);
  HOOX_INTERCEPTOR_UNLOCK (self);

  /* Clear the singleton pointer. */
  hx_mutex_lock (&_hoox_interceptor_lock);
  if (_the_interceptor == self)
    _the_interceptor = NULL;
  hx_mutex_unlock (&_hoox_interceptor_lock);

  /* Formerly the GObject finalize phase. */
  hoox_interceptor_transaction_destroy (&self->current_transaction);

  if (self->backend != NULL)
    _hoox_interceptor_backend_destroy (self->backend);

  hx_rec_mutex_clear (&self->mutex);

  hx_hash_table_unref (self->function_by_address);

  hoox_code_allocator_free (&self->allocator);

  hx_free (self);
}

/**
 * hoox_interceptor_obtain:
 *
 * Obtains the interceptor singleton.
 *
 * Returns: (transfer full): the interceptor
 */
HooxInterceptor *
hoox_interceptor_obtain (void)
{
  HooxInterceptor * interceptor;

  hx_mutex_lock (&_hoox_interceptor_lock);

  if (_the_interceptor != NULL)
    interceptor = hoox_interceptor_ref (_the_interceptor);
  else
    interceptor = _the_interceptor = hoox_interceptor_new ();

  hx_mutex_unlock (&_hoox_interceptor_lock);

  return interceptor;
}

/**
 * hoox_interceptor_set_default_options:
 * @self: the interceptor
 * @options: (not nullable): the options to use as defaults
 *
 * Sets the instrumentation options applied when a subsequent attach or replace
 * is given no options of its own.
 */
void
hoox_interceptor_set_default_options (HooxInterceptor * self,
                                     const HooxInterceptorOptions * options)
{
  HooxInterceptorOptions * defaults = &self->options;

  *defaults = *options;

  if (defaults->scenario == HOOX_INTERCEPTOR_SCENARIO_DEFAULT)
    defaults->scenario = HOOX_INTERCEPTOR_SCENARIO_ONLINE;
  if (defaults->relocation_policy == HOOX_RELOCATION_DEFAULT)
    defaults->relocation_policy = HOOX_RELOCATION_CHECKED;
}

/**
 * hoox_interceptor_attach:
 * @self: the interceptor
 * @target: (not nullable): address to intercept
 * @listener: (transfer none): listener notified on enter and leave
 * @options: (nullable): attach options, or %NULL for the defaults
 *
 * Attaches @listener so that it is notified right before @target is entered and
 * right after it returns. The same listener may be attached to any number of
 * addresses, and multiple listeners may be attached to the same address. The
 * original code is left in place.
 *
 * @target need not be a function entry: a listener that implements
 * only `on_enter` acts as a probe and may be placed at an arbitrary
 * instruction to observe execution reaching that point.
 *
 * The change takes effect immediately unless a transaction is open.
 *
 * Returns: %HOOX_ATTACH_OK on success, or another [enum@Hoox.AttachReturn]
 *   describing why the function could not be instrumented
 */
HooxAttachReturn
hoox_interceptor_attach (HooxInterceptor * self,
                        hx_pointer target,
                        HooxInvocationListener * listener,
                        const HooxAttachOptions * options)
{
  HooxAttachReturn result = HOOX_ATTACH_OK;
  HooxAttachOptions default_options = { 0, };
  HooxFunctionContext * function_ctx;
  HooxInstrumentationError error;

  if (options == NULL)
    options = &default_options;

  hoox_interceptor_ignore_current_thread (self);
  HOOX_INTERCEPTOR_LOCK (self);
  hoox_interceptor_transaction_begin (&self->current_transaction);
  self->current_transaction.is_dirty = TRUE;

  target = hoox_interceptor_resolve (self, target);

  function_ctx = hoox_interceptor_instrument (self, HOOX_INTERCEPTOR_TYPE_DEFAULT,
      target, &options->instrumentation, &error);

  if (function_ctx == NULL)
    goto instrumentation_error;

  if (hoox_function_context_has_listener (function_ctx, listener))
    goto already_attached;

  if (hoox_function_context_count_listeners (function_ctx) >=
      HOOX_MAX_LISTENERS_PER_FUNCTION)
    goto too_many_listeners;

  hoox_function_context_add_listener (function_ctx, listener,
      options->listener_function_data,
      options->ignorability == HOOX_INVOCATION_UNIGNORABLE);

  goto beach;

instrumentation_error:
  {
    switch (error)
    {
      case HOOX_INSTRUMENTATION_ERROR_WRONG_SIGNATURE:
        result = HOOX_ATTACH_WRONG_SIGNATURE;
        break;
      case HOOX_INSTRUMENTATION_ERROR_POLICY_VIOLATION:
        result = HOOX_ATTACH_POLICY_VIOLATION;
        break;
      case HOOX_INSTRUMENTATION_ERROR_WRONG_TYPE:
        result = HOOX_ATTACH_WRONG_TYPE;
        break;
      default:
        hx_assert_not_reached ();
    }
    goto beach;
  }
already_attached:
  {
    result = HOOX_ATTACH_ALREADY_ATTACHED;
    goto beach;
  }
too_many_listeners:
  {
    result = HOOX_ATTACH_TOO_MANY_LISTENERS;
    goto beach;
  }
beach:
  {
    hoox_interceptor_transaction_end (&self->current_transaction);
    HOOX_INTERCEPTOR_UNLOCK (self);
    hoox_interceptor_unignore_current_thread (self);

    return result;
  }
}

/**
 * hoox_interceptor_detach:
 * @self: the interceptor
 * @listener: (transfer none): the listener to detach
 *
 * Detaches @listener from every function it is currently attached to, undoing
 * any [method@Hoox.Interceptor.attach] calls made with it. Functions left
 * without any listeners or replacement are restored to their original state.
 *
 * The change takes effect immediately unless a transaction is open.
 */
void
hoox_interceptor_detach (HooxInterceptor * self,
                        HooxInvocationListener * listener)
{
  HxHashTableIter iter;
  hx_pointer key, value;

  hoox_interceptor_ignore_current_thread (self);
  HOOX_INTERCEPTOR_LOCK (self);
  hoox_interceptor_transaction_begin (&self->current_transaction);
  self->current_transaction.is_dirty = TRUE;

  hx_hash_table_iter_init (&iter, self->function_by_address);
  while (hx_hash_table_iter_next (&iter, NULL, &value))
  {
    HooxFunctionContext * function_ctx = value;

    if (hoox_function_context_has_listener (function_ctx, listener))
    {
      hoox_function_context_remove_listener (function_ctx, listener);

      hoox_interceptor_transaction_schedule_destroy (&self->current_transaction,
          function_ctx, (HxDestroyNotify) hoox_invocation_listener_unref,
          hoox_invocation_listener_ref (listener));

      if (hoox_function_context_is_empty (function_ctx))
      {
        hx_hash_table_iter_remove (&iter);
      }
    }
  }

  hoox_spinlock_acquire (&hoox_interceptor_thread_context_lock);
  hx_hash_table_iter_init (&iter, hoox_interceptor_thread_contexts);
  while (hx_hash_table_iter_next (&iter, &key, NULL))
  {
    InterceptorThreadContext * thread_ctx = key;

    interceptor_thread_context_forget_listener_data (thread_ctx, listener);
  }
  hoox_spinlock_release (&hoox_interceptor_thread_context_lock);

  hoox_interceptor_transaction_end (&self->current_transaction);
  HOOX_INTERCEPTOR_UNLOCK (self);
  hoox_interceptor_unignore_current_thread (self);
}

/**
 * hoox_interceptor_replace:
 * @self: the interceptor
 * @function_address: (not nullable): address of the function to replace
 * @replacement_function: (not nullable): address of the replacement
 * @original_function: (out) (optional) (nullable): return location for a
 *   pointer through which the original function can be called, or %NULL
 * @options: (nullable): replace options, or %NULL for the defaults
 *
 * Replaces @function_address with @replacement_function, so that any call to
 * it ends up in the replacement instead. The replacement can still reach the
 * original by calling @function_address itself — the interceptor routes that
 * call to the original rather than recursing — or through @original_function
 * if a pointer is more convenient.
 *
 * Undo with [method@Hoox.Interceptor.revert]. The change takes effect
 * immediately unless a transaction is open.
 *
 * Returns: %HOOX_REPLACE_OK on success, or another [enum@Hoox.ReplaceReturn]
 *   describing why the function could not be instrumented
 */
HooxReplaceReturn
hoox_interceptor_replace (HooxInterceptor * self,
                         hx_pointer function_address,
                         hx_pointer replacement_function,
                         hx_pointer * original_function,
                         const HooxReplaceOptions * options)
{
  HooxReplaceOptions default_options = { 0, };

  if (options == NULL)
    options = &default_options;

  return hoox_interceptor_replace_with_type (self, HOOX_INTERCEPTOR_TYPE_DEFAULT,
      function_address, replacement_function, options->replacement_data,
      original_function, &options->instrumentation);
}

/**
 * hoox_interceptor_replace_fast:
 * @self: the interceptor
 * @function_address: (not nullable): address of the function to replace
 * @replacement_function: (not nullable): address of the replacement
 * @original_function: (out) (optional) (nullable): return location for a
 *   pointer through which the original function can still be called, or %NULL
 * @options: (nullable): instrumentation options, or %NULL for the defaults
 *
 * Like [method@Hoox.Interceptor.replace], but trades flexibility for speed by
 * patching @function_address to branch straight to @replacement_function with
 * no trampoline in between. A trampoline is only involved if you ask for
 * @original_function, which you must use to reach the original — unlike
 * [method@Hoox.Interceptor.replace], calling @function_address again would just
 * re-enter the replacement. A target replaced this way cannot also be attached
 * to; use [method@Hoox.Interceptor.replace] if you need that.
 *
 * Prefer this when the hook is on a hot path and the extra machinery of the
 * default replacement is not needed.
 *
 * Returns: %HOOX_REPLACE_OK on success, or another [enum@Hoox.ReplaceReturn]
 *   describing why the function could not be instrumented
 */
HooxReplaceReturn
hoox_interceptor_replace_fast (HooxInterceptor * self,
                              hx_pointer function_address,
                              hx_pointer replacement_function,
                              hx_pointer * original_function,
                              const HooxInterceptorOptions * options)
{
  HooxInterceptorOptions default_options = { 0, };

  if (options == NULL)
    options = &default_options;

  return hoox_interceptor_replace_with_type (self, HOOX_INTERCEPTOR_TYPE_FAST,
      function_address, replacement_function, NULL,
      original_function, options);
}

static HooxReplaceReturn
hoox_interceptor_replace_with_type (HooxInterceptor * self,
                                   HooxInterceptorType type,
                                   hx_pointer function_address,
                                   hx_pointer replacement_function,
                                   hx_pointer replacement_data,
                                   hx_pointer * original_function,
                                   const HooxInterceptorOptions * options)
{
  HooxReplaceReturn result = HOOX_REPLACE_OK;
  HooxFunctionContext * function_ctx;
  HooxInstrumentationError error;

  HOOX_INTERCEPTOR_LOCK (self);
  hoox_interceptor_transaction_begin (&self->current_transaction);
  self->current_transaction.is_dirty = TRUE;

  function_address = hoox_interceptor_resolve (self, function_address);

  function_ctx = hoox_interceptor_instrument (self, type, function_address,
      options, &error);

  if (function_ctx == NULL)
    goto instrumentation_error;

  if (function_ctx->replacement_function != NULL)
    goto already_replaced;

  function_ctx->replacement_data = replacement_data;
  function_ctx->replacement_function = replacement_function;

  if (original_function != NULL)
    *original_function = function_ctx->on_invoke_trampoline;

  goto beach;

instrumentation_error:
  {
    switch (error)
    {
      case HOOX_INSTRUMENTATION_ERROR_WRONG_SIGNATURE:
        result = HOOX_REPLACE_WRONG_SIGNATURE;
        break;
      case HOOX_INSTRUMENTATION_ERROR_POLICY_VIOLATION:
        result = HOOX_REPLACE_POLICY_VIOLATION;
        break;
      case HOOX_INSTRUMENTATION_ERROR_WRONG_TYPE:
        result = HOOX_REPLACE_WRONG_TYPE;
        break;
      default:
        hx_assert_not_reached ();
    }
    goto beach;
  }
already_replaced:
  {
    result = HOOX_REPLACE_ALREADY_REPLACED;
    goto beach;
  }
beach:
  {
    hoox_interceptor_transaction_end (&self->current_transaction);
    HOOX_INTERCEPTOR_UNLOCK (self);

    return result;
  }
}

/**
 * hoox_interceptor_revert:
 * @self: the interceptor
 * @target: (not nullable): address of the function to revert
 *
 * Reverts a previous [method@Hoox.Interceptor.replace] of @target, restoring the
 * original function. Has no effect if the function was not replaced.
 *
 * The change takes effect immediately unless a transaction is open.
 */
void
hoox_interceptor_revert (HooxInterceptor * self,
                        hx_pointer target)
{
  HooxFunctionContext * function_ctx;

  HOOX_INTERCEPTOR_LOCK (self);
  hoox_interceptor_transaction_begin (&self->current_transaction);
  self->current_transaction.is_dirty = TRUE;

  target = hoox_interceptor_resolve (self, target);

  function_ctx = (HooxFunctionContext *) hx_hash_table_lookup (
      self->function_by_address, target);
  if (function_ctx == NULL)
    goto beach;

  function_ctx->replacement_function = NULL;
  function_ctx->replacement_data = NULL;

  if (hoox_function_context_is_empty (function_ctx))
  {
    hx_hash_table_remove (self->function_by_address, target);
  }

beach:
  hoox_interceptor_transaction_end (&self->current_transaction);
  HOOX_INTERCEPTOR_UNLOCK (self);
}

/**
 * hoox_interceptor_begin_transaction:
 * @self: the interceptor
 *
 * Begins a transaction, deferring activation of any attach, replace and revert
 * operations until the matching [method@Hoox.Interceptor.end_transaction].
 * Batching changes this way is faster and lets a set of modifications be
 * applied as a unit. Transactions nest; only ending the outermost one applies
 * the changes.
 */
void
hoox_interceptor_begin_transaction (HooxInterceptor * self)
{
  HOOX_INTERCEPTOR_LOCK (self);
  hoox_interceptor_transaction_begin (&self->current_transaction);
  HOOX_INTERCEPTOR_UNLOCK (self);
}

/**
 * hoox_interceptor_end_transaction:
 * @self: the interceptor
 *
 * Ends a transaction started with [method@Hoox.Interceptor.begin_transaction].
 * Ending the outermost transaction activates all changes made since it began.
 */
void
hoox_interceptor_end_transaction (HooxInterceptor * self)
{
  HOOX_INTERCEPTOR_LOCK (self);
  hoox_interceptor_transaction_end (&self->current_transaction);
  HOOX_INTERCEPTOR_UNLOCK (self);
}

/**
 * hoox_interceptor_flush:
 * @self: the interceptor
 *
 * Completes any teardown still pending from earlier detaches and reverts. When
 * a listener is detached or a replacement reverted the hook stops firing
 * immediately, but the memory backing its instrumentation can only be released
 * once no thread is left executing inside it, so that step is deferred. Call
 * this to force a pass and learn whether it finished. Does nothing while a
 * transaction is open.
 *
 * Returns: %TRUE if no teardown remains pending, %FALSE if some instrumented
 *   code may still be executing
 */
hx_boolean
hoox_interceptor_flush (HooxInterceptor * self)
{
  hx_boolean flushed = FALSE;

  HOOX_INTERCEPTOR_LOCK (self);

  if (self->current_transaction.level == 0)
  {
    hoox_interceptor_transaction_begin (&self->current_transaction);
    hoox_interceptor_transaction_end (&self->current_transaction);

    flushed =
        hx_queue_is_empty (self->current_transaction.pending_destroy_tasks);
  }

  HOOX_INTERCEPTOR_UNLOCK (self);

  return flushed;
}

/**
 * hoox_interceptor_flush_function:
 * @self: the interceptor
 * @function_address: (not nullable): address of the function of interest
 *
 * Like [method@Hoox.Interceptor.flush], but reports specifically whether the
 * instrumentation for @function_address is no longer in use, so its memory can
 * be reclaimed.
 *
 * Returns: %TRUE if @function_address has no pending teardown left
 */
hx_boolean
hoox_interceptor_flush_function (HooxInterceptor * self,
                                hx_constpointer function_address)
{
  hx_boolean flushed = TRUE;

  HOOX_INTERCEPTOR_LOCK (self);

  if (self->current_transaction.level == 0)
  {
    hx_pointer target;
    HxList * cur;

    hoox_interceptor_transaction_begin (&self->current_transaction);
    hoox_interceptor_transaction_end (&self->current_transaction);

    target = hoox_interceptor_resolve (self, (hx_pointer) function_address);

    for (cur = self->current_transaction.pending_destroy_tasks->head;
        cur != NULL;
        cur = cur->next)
    {
      HooxDestroyTask * task = cur->data;

      if (task->ctx->function_address == target)
      {
        flushed = FALSE;
        break;
      }
    }
  }
  else
  {
    flushed = FALSE;
  }

  HOOX_INTERCEPTOR_UNLOCK (self);

  return flushed;
}

/**
 * hoox_interceptor_flush_listener:
 * @self: the interceptor
 * @listener: (transfer none): the listener of interest
 *
 * Like [method@Hoox.Interceptor.flush], but reports specifically whether
 * @listener is no longer referenced by any in-flight invocation, so it is safe
 * to release.
 *
 * Returns: %TRUE if @listener has no pending teardown left
 */
hx_boolean
hoox_interceptor_flush_listener (HooxInterceptor * self,
                                HooxInvocationListener * listener)
{
  hx_boolean flushed = TRUE;

  HOOX_INTERCEPTOR_LOCK (self);

  if (self->current_transaction.level == 0)
  {
    HxList * cur;

    hoox_interceptor_transaction_begin (&self->current_transaction);
    hoox_interceptor_transaction_end (&self->current_transaction);

    for (cur = self->current_transaction.pending_destroy_tasks->head;
        cur != NULL;
        cur = cur->next)
    {
      HooxDestroyTask * task = cur->data;

      if (task->data == listener)
      {
        flushed = FALSE;
        break;
      }
    }
  }
  else
  {
    flushed = FALSE;
  }

  HOOX_INTERCEPTOR_UNLOCK (self);

  return flushed;
}

/**
 * hoox_interceptor_get_current_invocation:
 *
 * Returns the current invocation context.
 *
 * Returns: (transfer none) (nullable): the invocation context, or
 *   %NULL if not in an intercepted call
 */
HooxInvocationContext *
hoox_interceptor_get_current_invocation (void)
{
  InterceptorThreadContext * interceptor_ctx;
  HooxInvocationStackEntry * entry;

  interceptor_ctx = get_interceptor_thread_context ();
  entry = hoox_invocation_stack_peek_top (interceptor_ctx->stack);
  if (entry == NULL)
    return NULL;

  return &entry->invocation_context;
}

/**
 * hoox_interceptor_get_live_replacement_invocation:
 * @replacement_function: the replacement function
 *
 * Returns the invocation context for the given replacement
 * function, if currently active.
 *
 * Returns: (transfer none) (nullable): the invocation context, or
 *   %NULL if not in the specified replacement
 */
HooxInvocationContext *
hoox_interceptor_get_live_replacement_invocation (hx_pointer replacement_function)
{
  InterceptorThreadContext * interceptor_ctx;
  HooxInvocationStackEntry * entry;

  interceptor_ctx = get_interceptor_thread_context ();
  entry = hoox_invocation_stack_peek_top (interceptor_ctx->stack);
  if (entry == NULL)
    return NULL;
  if (!entry->calling_replacement)
    return NULL;
  if (replacement_function != entry->function_ctx->replacement_function)
    return NULL;

  return &entry->invocation_context;
}

/**
 * hoox_interceptor_get_current_stack:
 *
 * Returns the invocation stack for the current thread.
 *
 * Returns: (transfer none): the invocation stack
 */
HooxInvocationStack *
hoox_interceptor_get_current_stack (void)
{
  InterceptorThreadContext * context;

  context = hx_private_get (&hoox_interceptor_context_private);
  if (context == NULL)
    return &_hoox_interceptor_empty_stack;

  return context->stack;
}

/**
 * hoox_interceptor_ignore_current_thread:
 * @self: the interceptor
 *
 * Temporarily stops the calling thread's calls into hooked code from
 * triggering listeners. The typical use is to bracket work done internally by
 * an injected payload — for example its own worker threads — so that a user's
 * hooks observe only the target process's activity, not the payload's own
 * calls into the functions it has hooked.
 *
 * Listeners marked unignorable still fire (see
 * [enum@Hoox.InvocationIgnorability]). Note that re-entrancy from within a
 * listener's own `on_enter`/`on_leave` is already prevented automatically, so
 * this is not needed for that.
 *
 * Nestable, and balanced by [method@Hoox.Interceptor.unignore_current_thread].
 */
void
hoox_interceptor_ignore_current_thread (HooxInterceptor * self)
{
  InterceptorThreadContext * interceptor_ctx;

  interceptor_ctx = get_interceptor_thread_context ();
  interceptor_ctx->ignore_level++;
}

/**
 * hoox_interceptor_unignore_current_thread:
 * @self: the interceptor
 *
 * Undoes one [method@Hoox.Interceptor.ignore_current_thread] on the calling
 * thread.
 */
void
hoox_interceptor_unignore_current_thread (HooxInterceptor * self)
{
  InterceptorThreadContext * interceptor_ctx;

  interceptor_ctx = get_interceptor_thread_context ();
  interceptor_ctx->ignore_level--;
}

/**
 * hoox_interceptor_maybe_unignore_current_thread:
 * @self: the interceptor
 *
 * Undoes one [method@Hoox.Interceptor.ignore_current_thread], but only if the
 * calling thread is currently being ignored.
 *
 * Returns: %TRUE if the thread was being ignored and is now one level less so
 */
hx_boolean
hoox_interceptor_maybe_unignore_current_thread (HooxInterceptor * self)
{
  InterceptorThreadContext * interceptor_ctx;

  interceptor_ctx = get_interceptor_thread_context ();
  if (interceptor_ctx->ignore_level <= 0)
    return FALSE;

  interceptor_ctx->ignore_level--;
  return TRUE;
}

/**
 * hoox_interceptor_ignore_other_threads:
 * @self: the interceptor
 *
 * Restricts interception to the calling thread: invocations on all other
 * threads stop triggering listeners until
 * [method@Hoox.Interceptor.unignore_other_threads] is called.
 */
void
hoox_interceptor_ignore_other_threads (HooxInterceptor * self)
{
  hx_atomic_size_set (&self->selected_thread_id,
      hoox_process_get_current_thread_id ());
}

/**
 * hoox_interceptor_unignore_other_threads:
 * @self: the interceptor
 *
 * Lifts a previous [method@Hoox.Interceptor.ignore_other_threads], resuming
 * interception on all threads. Must be called from the same thread that
 * ignored the others.
 */
void
hoox_interceptor_unignore_other_threads (HooxInterceptor * self)
{
  hx_assert (hx_atomic_size_get (&self->selected_thread_id) ==
      hoox_process_get_current_thread_id ());
  hx_atomic_size_set (&self->selected_thread_id, 0);
}

/**
 * hoox_invocation_stack_translate:
 * @self: the invocation stack
 * @return_address: a potentially hijacked return address
 *
 * Translates @return_address back to its real value. While a listener is
 * active the interceptor temporarily replaces on-stack return addresses with
 * its own trampoline; this resolves such an address to the caller's true
 * return address, leaving any unrelated address unchanged.
 *
 * Returns: the real return address
 */
hx_pointer
hoox_invocation_stack_translate (HooxInvocationStack * self,
                                hx_pointer return_address)
{
  hx_uint i;

  for (i = 0; i != self->len; i++)
  {
    HooxInvocationStackEntry * entry;

    entry = &hx_array_index (self, HooxInvocationStackEntry, i);
    if (entry->function_ctx->on_leave_trampoline == return_address)
      return entry->caller_ret_addr;
  }

  return return_address;
}

/**
 * hoox_interceptor_save:
 * @state: (out): return location for the saved state
 *
 * Records the calling thread's current invocation depth into @state, to be
 * restored later with [func@Hoox.Interceptor.restore]. Use this around a
 * non-local exit such as a `longjmp()` that would otherwise skip the
 * bookkeeping the interceptor does as intercepted calls return.
 */
void
hoox_interceptor_save (HooxInvocationState * state)
{
  *state = hoox_interceptor_get_current_stack ()->len;
}

/**
 * hoox_interceptor_restore:
 * @state: (in): the state previously saved with [func@Hoox.Interceptor.save]
 *
 * Unwinds the calling thread's invocation stack back to the depth recorded in
 * @state, releasing any entries skipped by a non-local exit.
 */
void
hoox_interceptor_restore (HooxInvocationState * state)
{
  HooxInvocationStack * stack;
  hx_uint old_depth, new_depth, i;

  stack = hoox_interceptor_get_current_stack ();

  old_depth = *state;
  new_depth = stack->len;
  if (new_depth == old_depth)
    return;

  for (i = old_depth; i != new_depth; i++)
  {
    HooxInvocationStackEntry * entry;

    entry = &hx_array_index (stack, HooxInvocationStackEntry, i);

    hx_atomic_int_dec_and_test (&entry->function_ctx->trampoline_usage_counter);
  }

  hx_array_set_size (stack, old_depth);
}

/**
 * hoox_interceptor_with_lock_held:
 * @self: the interceptor
 * @func: (scope call): function to call while holding the lock
 * @user_data: data to pass to @func
 *
 * Calls @func while holding the interceptor lock.
 */
void
hoox_interceptor_with_lock_held (HooxInterceptor * self,
                                HooxInterceptorLockedFunc func,
                                hx_pointer user_data)
{
  HOOX_INTERCEPTOR_LOCK (self);
  func (user_data);
  HOOX_INTERCEPTOR_UNLOCK (self);
}

/**
 * hoox_interceptor_is_locked:
 * @self: the interceptor
 *
 * Checks whether the interceptor lock is currently held, e.g. to decide
 * whether it is safe to make changes from a signal handler.
 *
 * Returns: %TRUE if the lock is held
 */
hx_boolean
hoox_interceptor_is_locked (HooxInterceptor * self)
{
  if (!hx_rec_mutex_trylock (&self->mutex))
    return TRUE;

  HOOX_INTERCEPTOR_UNLOCK (self);
  return FALSE;
}

hx_pointer
_hoox_interceptor_peek_top_caller_return_address (void)
{
  HooxInvocationStack * stack;
  HooxInvocationStackEntry * entry;

  stack = hoox_interceptor_get_current_stack ();
  if (stack->len == 0)
    return NULL;

  entry = &hx_array_index (stack, HooxInvocationStackEntry, stack->len - 1);

  return entry->caller_ret_addr;
}

hx_pointer
_hoox_interceptor_translate_top_return_address (hx_pointer return_address)
{
  HooxInvocationStack * stack;
  HooxInvocationStackEntry * entry;

  stack = hoox_interceptor_get_current_stack ();
  if (stack->len == 0)
    goto fallback;

  entry = &hx_array_index (stack, HooxInvocationStackEntry, stack->len - 1);
  if (entry->function_ctx->on_leave_trampoline != return_address)
    goto fallback;

  return entry->caller_ret_addr;

fallback:
  return return_address;
}

/* Apple arm64 performs the eventual code write from an off-page executable
 * stub, so self-hosted targets may safely share a page with hoox. The backend's
 * trampoline-preparation check rejects the one irreducible collision: a target
 * page containing mach_vm_protect(), which the stub must execute while that
 * target page is temporarily non-executable. */
static HooxFunctionContext *
hoox_interceptor_instrument (HooxInterceptor * self,
                            HooxInterceptorType type,
                            hx_pointer function_address,
                            const HooxInterceptorOptions * instrumentation,
                            HooxInstrumentationError * error)
{
  HooxFunctionContext * ctx;
  HooxInterceptorOptions effective;
  const HooxInterceptorOptions * defaults;
  hx_boolean force;

  *error = HOOX_INSTRUMENTATION_ERROR_NONE;

  ctx = (HooxFunctionContext *) hx_hash_table_lookup (self->function_by_address,
      function_address);

  if (ctx != NULL)
  {
    if (ctx->type != type)
    {
      *error = HOOX_INSTRUMENTATION_ERROR_WRONG_TYPE;
      return NULL;
    }
    return ctx;
  }


  if (self->backend == NULL)
  {
    self->backend =
        _hoox_interceptor_backend_create (&self->mutex, &self->allocator);
  }

  ctx = hoox_function_context_new (self, function_address, type);
  effective = *instrumentation;
  defaults = &self->options;
  if (effective.scratch_register == 0)
    effective.scratch_register = defaults->scratch_register;
  if (effective.scenario == HOOX_INTERCEPTOR_SCENARIO_DEFAULT)
    effective.scenario = defaults->scenario;
  if (effective.relocation_policy == HOOX_RELOCATION_DEFAULT)
    effective.relocation_policy = defaults->relocation_policy;
  if (effective.write_redirect == NULL)
  {
    effective.write_redirect = defaults->write_redirect;
    effective.write_redirect_data = defaults->write_redirect_data;
  }
  if (effective.redirect_space_hint == 0)
    effective.redirect_space_hint = defaults->redirect_space_hint;

  ctx->scratch_register = effective.scratch_register;
  ctx->scenario = effective.scenario;
  ctx->relocation_policy = effective.relocation_policy;
  ctx->write_redirect = effective.write_redirect;
  ctx->write_redirect_data = effective.write_redirect_data;
  ctx->redirect_space_hint = effective.redirect_space_hint;

  force = effective.relocation_policy == HOOX_RELOCATION_FORCED;

  if (hoox_process_get_code_signing_policy () == HOOX_CODE_SIGNING_REQUIRED)
  {
    if (!_hoox_interceptor_backend_claim_grafted_trampoline (self->backend, ctx))
      goto policy_violation;
  }
  else
  {
    if (!_hoox_interceptor_backend_create_trampoline (self->backend, ctx, force))
      goto wrong_signature;
  }

  hx_hash_table_insert (self->function_by_address, function_address, ctx);

  hoox_interceptor_transaction_schedule_update (&self->current_transaction, ctx,
      hoox_interceptor_activate);

  return ctx;

policy_violation:
  {
    *error = HOOX_INSTRUMENTATION_ERROR_POLICY_VIOLATION;
    goto propagate_error;
  }
wrong_signature:
  {
    *error = HOOX_INSTRUMENTATION_ERROR_WRONG_SIGNATURE;
    goto propagate_error;
  }
propagate_error:
  {
    hoox_function_context_finalize (ctx);

    return NULL;
  }
}

static void
hoox_interceptor_activate (HooxInterceptor * self,
                          HooxFunctionContext * ctx,
                          hx_pointer prologue)
{
  if (ctx->destroyed)
    return;

  hx_assert (!ctx->activated);
  ctx->activated = TRUE;

  _hoox_interceptor_backend_activate_trampoline (self->backend, ctx,
      prologue);
}

static void
hoox_interceptor_deactivate (HooxInterceptor * self,
                            HooxFunctionContext * ctx,
                            hx_pointer prologue)
{
  HooxInterceptorBackend * backend = self->backend;

  hx_assert (ctx->activated);
  ctx->activated = FALSE;

  _hoox_interceptor_backend_deactivate_trampoline (backend, ctx, prologue);
}

static void
hoox_interceptor_transaction_init (HooxInterceptorTransaction * transaction,
                                  HooxInterceptor * interceptor)
{
  transaction->is_dirty = FALSE;
  transaction->level = 0;
  transaction->pending_destroy_tasks = hx_queue_new ();
  transaction->pending_update_tasks = hx_hash_table_new_full (
      NULL, NULL, NULL, (HxDestroyNotify) hx_array_unref);

  transaction->interceptor = interceptor;
}

static void
hoox_interceptor_transaction_destroy (HooxInterceptorTransaction * transaction)
{
  HooxDestroyTask * task;

  hx_hash_table_unref (transaction->pending_update_tasks);

  while ((task = hx_queue_pop_head (transaction->pending_destroy_tasks)) != NULL)
  {
    task->notify (task->data);

    hx_slice_free (HooxDestroyTask, task);
  }
  hx_queue_free (transaction->pending_destroy_tasks);
}

static void
hoox_interceptor_transaction_begin (HooxInterceptorTransaction * self)
{
  self->level++;
}

static void
hoox_interceptor_transaction_end (HooxInterceptorTransaction * self)
{
  HooxInterceptor * interceptor = self->interceptor;
  HooxInterceptorTransaction transaction_copy;
  HxPtrArray * addresses;
  HxHashTableIter iter;
  hx_pointer address;

  self->level--;
  if (self->level > 0)
    return;

  if (!self->is_dirty)
    return;

  hoox_interceptor_ignore_current_thread (interceptor);

  hoox_code_allocator_commit (&interceptor->allocator);

  if (hx_queue_is_empty (self->pending_destroy_tasks) &&
      hx_hash_table_size (self->pending_update_tasks) == 0)
  {
    interceptor->current_transaction.is_dirty = FALSE;
    goto no_changes;
  }

  transaction_copy = interceptor->current_transaction;
  self = &transaction_copy;
  hoox_interceptor_transaction_init (&interceptor->current_transaction,
      interceptor);

  addresses =
      hx_ptr_array_sized_new (hx_hash_table_size (self->pending_update_tasks));
  hx_hash_table_iter_init (&iter, self->pending_update_tasks);
  while (hx_hash_table_iter_next (&iter, &address, NULL))
    hx_ptr_array_add (addresses, address);
  hx_ptr_array_sort (addresses, (HxCompareFunc) hoox_page_address_compare);

  if (hoox_process_get_code_signing_policy () == HOOX_CODE_SIGNING_REQUIRED)
  {
    hx_uint addr_index;

    for (addr_index = 0; addr_index != addresses->len; addr_index++)
    {
      hx_pointer target_page;
      HxArray * pending;
      hx_uint i;

      target_page = hx_ptr_array_index (addresses, addr_index);

      pending = hx_hash_table_lookup (self->pending_update_tasks, target_page);
      hx_assert (pending != NULL);

      for (i = 0; i != pending->len; i++)
      {
        HooxUpdateTask * update;

        update = &hx_array_index (pending, HooxUpdateTask, i);

        update->func (interceptor, update->ctx,
            _hoox_interceptor_backend_get_function_address (update->ctx));
      }
    }
  }
  else if (!hoox_memory_patch_code_pages (addresses, FALSE, hoox_apply_updates,
        self))
  {
    hx_abort ();
  }

  hx_ptr_array_unref (addresses);

  {
    HooxDestroyTask * task;

    while ((task = hx_queue_pop_head (self->pending_destroy_tasks)) != NULL)
    {
      if (task->ctx->trampoline_usage_counter == 0)
      {
        HOOX_INTERCEPTOR_UNLOCK (interceptor);
        task->notify (task->data);
        HOOX_INTERCEPTOR_LOCK (interceptor);

        hx_slice_free (HooxDestroyTask, task);
      }
      else
      {
        interceptor->current_transaction.is_dirty = TRUE;
        hx_queue_push_tail (
            interceptor->current_transaction.pending_destroy_tasks, task);
      }
    }
  }

  hoox_interceptor_transaction_destroy (self);

no_changes:
  hoox_interceptor_unignore_current_thread (interceptor);
}

static void
hoox_apply_updates (hx_pointer source_page,
                   hx_pointer target_page,
                   hx_uint n_pages,
                   hx_pointer user_data)
{
  HooxInterceptorTransaction * self = user_data;
  HxArray * pending;
  hx_uint i;

  pending = hx_hash_table_lookup (self->pending_update_tasks, target_page);
  hx_assert (pending != NULL);

  for (i = 0; i != pending->len; i++)
  {
    HooxUpdateTask * update;
    hx_size offset;

    update = &hx_array_index (pending, HooxUpdateTask, i);

    offset = (hx_uint8 *)
        _hoox_interceptor_backend_get_function_address (update->ctx) -
        (hx_uint8 *) target_page;

    update->func (self->interceptor, update->ctx,
        (hx_uint8 *) source_page + offset);
  }
}

static void
hoox_interceptor_transaction_schedule_destroy (HooxInterceptorTransaction * self,
                                              HooxFunctionContext * ctx,
                                              HxDestroyNotify notify,
                                              hx_pointer data)
{
  HooxDestroyTask * task;

  task = hx_slice_new (HooxDestroyTask);
  task->ctx = ctx;
  task->notify = notify;
  task->data = data;

  hx_queue_push_tail (self->pending_destroy_tasks, task);
}

static void
hoox_interceptor_transaction_schedule_update (HooxInterceptorTransaction * self,
                                             HooxFunctionContext * ctx,
                                             HooxUpdateTaskFunc func)
{
  hx_uint8 * function_address;
  hx_pointer start_page, end_page;
  HxArray * pending;
  HooxUpdateTask update;

  function_address = _hoox_interceptor_backend_get_function_address (ctx);

  start_page = hoox_page_address_from_pointer (function_address);
  end_page = hoox_page_address_from_pointer (function_address +
      ctx->overwritten_prologue_len - 1);

  pending = hx_hash_table_lookup (self->pending_update_tasks, start_page);
  if (pending == NULL)
  {
    pending = hx_array_new (FALSE, FALSE, sizeof (HooxUpdateTask));
    hx_hash_table_insert (self->pending_update_tasks, start_page, pending);
  }

  update.ctx = ctx;
  update.func = func;
  hx_array_append_val (pending, update);

  if (end_page != start_page)
  {
    pending = hx_hash_table_lookup (self->pending_update_tasks, end_page);
    if (pending == NULL)
    {
      pending = hx_array_new (FALSE, FALSE, sizeof (HooxUpdateTask));
      hx_hash_table_insert (self->pending_update_tasks, end_page, pending);
    }
  }
}

static HooxFunctionContext *
hoox_function_context_new (HooxInterceptor * interceptor,
                          hx_pointer function_address,
                          HooxInterceptorType type)
{
  HooxFunctionContext * ctx;

  ctx = hx_slice_new0 (HooxFunctionContext);
  ctx->function_address = function_address;
  ctx->type = type;
  ctx->listener_entries =
      hx_ptr_array_new_full (1, (HxDestroyNotify) listener_entry_free);
  ctx->interceptor = interceptor;

  return ctx;
}

static void
hoox_function_context_finalize (HooxFunctionContext * function_ctx)
{
  hx_assert (function_ctx->trampoline_slice == NULL);

  hx_ptr_array_unref (
      (HxPtrArray *) hx_atomic_pointer_get (&function_ctx->listener_entries));

  hx_free (function_ctx->overwritten_prologue);
  hx_free (function_ctx->redirect_code);

  hx_slice_free (HooxFunctionContext, function_ctx);
}

static void
hoox_function_context_destroy (HooxFunctionContext * function_ctx)
{
  HooxInterceptorTransaction * transaction =
      &function_ctx->interceptor->current_transaction;

  hx_assert (!function_ctx->destroyed);
  function_ctx->destroyed = TRUE;

  if (function_ctx->activated)
  {
    hoox_interceptor_transaction_schedule_update (transaction, function_ctx,
        hoox_interceptor_deactivate);
  }

  hoox_interceptor_transaction_schedule_destroy (transaction, function_ctx,
      (HxDestroyNotify) hoox_function_context_perform_destroy, function_ctx);
}

static void
hoox_function_context_perform_destroy (HooxFunctionContext * function_ctx)
{
  _hoox_interceptor_backend_destroy_trampoline (
      function_ctx->interceptor->backend, function_ctx);

  hoox_function_context_finalize (function_ctx);
}

static hx_boolean
hoox_function_context_is_empty (HooxFunctionContext * function_ctx)
{
  if (function_ctx->replacement_function != NULL)
    return FALSE;

  return hoox_function_context_find_taken_listener_slot (function_ctx) == NULL;
}

static void
hoox_function_context_add_listener (HooxFunctionContext * function_ctx,
                                   HooxInvocationListener * listener,
                                   hx_pointer function_data,
                                   hx_boolean unignorable)
{
  ListenerEntry * entry;
  HxPtrArray * old_entries, * new_entries;
  hx_uint i;

  entry = hx_slice_new (ListenerEntry);
  entry->listener_interface = listener->iface;
  entry->listener_instance = listener;
  entry->function_data = function_data;
  entry->unignorable = unignorable;

  old_entries =
      (HxPtrArray *) hx_atomic_pointer_get (&function_ctx->listener_entries);
  new_entries = hx_ptr_array_new_full (old_entries->len + 1,
      (HxDestroyNotify) listener_entry_free);
  for (i = 0; i != old_entries->len; i++)
  {
    ListenerEntry * old_entry = hx_ptr_array_index (old_entries, i);
    if (old_entry != NULL)
      hx_ptr_array_add (new_entries, hx_slice_dup (ListenerEntry, old_entry));
  }
  hx_ptr_array_add (new_entries, entry);

  hx_atomic_pointer_set (&function_ctx->listener_entries, new_entries);
  hoox_interceptor_transaction_schedule_destroy (
      &function_ctx->interceptor->current_transaction, function_ctx,
      (HxDestroyNotify) hx_ptr_array_unref, old_entries);

  if (entry->listener_interface->on_leave != NULL)
    function_ctx->has_on_leave_listener = TRUE;

  if (unignorable)
    function_ctx->has_unignorable_listener = TRUE;
}

static void
listener_entry_free (ListenerEntry * entry)
{
  hx_slice_free (ListenerEntry, entry);
}

static void
hoox_function_context_remove_listener (HooxFunctionContext * function_ctx,
                                      HooxInvocationListener * listener)
{
  hx_boolean removed = FALSE;
  hx_boolean has_on_leave_listener, has_unignorable_listener;
  HxPtrArray * old_entries, * new_entries;
  hx_uint i;

  has_on_leave_listener = FALSE;
  has_unignorable_listener = FALSE;
  old_entries =
      (HxPtrArray *) hx_atomic_pointer_get (&function_ctx->listener_entries);
  new_entries = hx_ptr_array_new_full (old_entries->len,
      (HxDestroyNotify) listener_entry_free);
  for (i = 0; i != old_entries->len; i++)
  {
    ListenerEntry * old_entry = hx_ptr_array_index (old_entries, i);
    ListenerEntry * new_entry;

    if (old_entry == NULL || old_entry->listener_instance == listener)
    {
      if (old_entry != NULL)
        removed = TRUE;
      hx_ptr_array_add (new_entries, NULL);
      continue;
    }

    new_entry = hx_slice_dup (ListenerEntry, old_entry);
    hx_ptr_array_add (new_entries, new_entry);

    if (new_entry->listener_interface->on_leave != NULL)
      has_on_leave_listener = TRUE;

    if (new_entry->unignorable)
      has_unignorable_listener = TRUE;
  }
  hx_assert (removed);

  hx_atomic_pointer_set (&function_ctx->listener_entries, new_entries);
  hoox_interceptor_transaction_schedule_destroy (
      &function_ctx->interceptor->current_transaction, function_ctx,
      (HxDestroyNotify) hx_ptr_array_unref, old_entries);

  function_ctx->has_on_leave_listener = has_on_leave_listener;
  function_ctx->has_unignorable_listener = has_unignorable_listener;
}

static hx_boolean
hoox_function_context_has_listener (HooxFunctionContext * function_ctx,
                                   HooxInvocationListener * listener)
{
  return hoox_function_context_find_listener (function_ctx, listener) != NULL;
}

/*
 * Count the live (non-NULL) listener entries. add_listener compacts holes on
 * every insert, so the array after an insert holds count + 1 entries; a detach
 * publishes a copy with the removed slot set to NULL. The attach path uses
 * this to keep the entry count within HOOX_MAX_LISTENERS_PER_FUNCTION, which
 * bounds the fixed
 * per-invocation listener_invocation_data[] array.
 */
static hx_uint
hoox_function_context_count_listeners (HooxFunctionContext * function_ctx)
{
  HxPtrArray * listener_entries;
  hx_uint i, count = 0;

  listener_entries =
      (HxPtrArray *) hx_atomic_pointer_get (&function_ctx->listener_entries);
  for (i = 0; i != listener_entries->len; i++)
  {
    if (hx_ptr_array_index (listener_entries, i) != NULL)
      count++;
  }

  return count;
}

static ListenerEntry **
hoox_function_context_find_listener (HooxFunctionContext * function_ctx,
                                    HooxInvocationListener * listener)
{
  HxPtrArray * listener_entries;
  hx_uint i;

  listener_entries =
      (HxPtrArray *) hx_atomic_pointer_get (&function_ctx->listener_entries);
  for (i = 0; i != listener_entries->len; i++)
  {
    ListenerEntry ** slot = (ListenerEntry **)
        &hx_ptr_array_index (listener_entries, i);
    if (*slot != NULL && (*slot)->listener_instance == listener)
      return slot;
  }

  return NULL;
}

static ListenerEntry **
hoox_function_context_find_taken_listener_slot (
    HooxFunctionContext * function_ctx)
{
  HxPtrArray * listener_entries;
  hx_uint i;

  listener_entries =
      (HxPtrArray *) hx_atomic_pointer_get (&function_ctx->listener_entries);
  for (i = 0; i != listener_entries->len; i++)
  {
    ListenerEntry ** slot = (ListenerEntry **)
        &hx_ptr_array_index (listener_entries, i);
    if (*slot != NULL)
      return slot;
  }

  return NULL;
}

hx_boolean
_hoox_function_context_begin_invocation (HooxFunctionContext * function_ctx,
                                        HooxCpuContext * cpu_context,
                                        hx_pointer * caller_ret_addr,
                                        hx_pointer * next_hop)
{
  HooxInterceptor * interceptor;
  InterceptorThreadContext * interceptor_ctx;
  HooxInvocationStack * stack;
  HooxInvocationStackEntry * stack_entry;
  HooxInvocationContext * invocation_ctx = NULL;
  hx_pointer stack_address;
  hx_int system_error;
  hx_boolean invoke_listeners = TRUE;
  hx_boolean only_invoke_unignorable_listeners = FALSE;
  hx_boolean will_trap_on_leave = FALSE;

  hx_atomic_int_inc (&function_ctx->trampoline_usage_counter);

  interceptor = function_ctx->interceptor;

#ifdef HAVE_WINDOWS
  system_error = hoox_thread_get_system_error ();
#endif

  if (hoox_tls_key_get_value (hoox_interceptor_guard_key) == interceptor)
  {
    *next_hop = function_ctx->on_invoke_trampoline;
    goto bypass;
  }
  hoox_tls_key_set_value (hoox_interceptor_guard_key, interceptor);

  interceptor_ctx = get_interceptor_thread_context ();
  stack = interceptor_ctx->stack;

  stack_entry = hoox_invocation_stack_peek_top (stack);
  if (stack_entry != NULL &&
      stack_entry->calling_replacement &&
      hoox_strip_code_pointer (HOOX_FUNCPTR_TO_POINTER (
          stack_entry->invocation_context.function)) ==
          function_ctx->function_address)
  {
    hoox_tls_key_set_value (hoox_interceptor_guard_key, NULL);
    *next_hop = function_ctx->on_invoke_trampoline;
    goto bypass;
  }

#ifndef HAVE_WINDOWS
  system_error = hoox_thread_get_system_error ();
#endif

  {
    HooxThreadId selected_thread_id;

    selected_thread_id = hx_atomic_size_get (&interceptor->selected_thread_id);
    if (selected_thread_id != 0)
    {
      invoke_listeners =
          hoox_process_get_current_thread_id () == selected_thread_id;
    }
  }

  if (invoke_listeners)
  {
    invoke_listeners = (interceptor_ctx->ignore_level <= 0);
  }

  if (!invoke_listeners && function_ctx->has_unignorable_listener)
  {
    invoke_listeners = TRUE;
    only_invoke_unignorable_listeners = TRUE;
  }

  stack_address = HOOX_INTERCEPTOR_CPU_CONTEXT_SP (cpu_context);
  hoox_invocation_stack_reap_unwound (stack, stack_address);

  will_trap_on_leave = function_ctx->replacement_function != NULL ||
      (invoke_listeners && function_ctx->has_on_leave_listener);
  if (will_trap_on_leave)
  {
    stack_entry = hoox_invocation_stack_push (stack, function_ctx,
        *caller_ret_addr, stack_address, only_invoke_unignorable_listeners);
    invocation_ctx = &stack_entry->invocation_context;
  }
  else if (invoke_listeners)
  {
    stack_entry = hoox_invocation_stack_push (stack, function_ctx,
        function_ctx->function_address, stack_address,
        only_invoke_unignorable_listeners);
    invocation_ctx = &stack_entry->invocation_context;
  }

  if (invocation_ctx != NULL)
  {
    invocation_ctx->system_error = system_error;
    stack_entry->listener_entries = (HxPtrArray *) hx_atomic_pointer_get (
        &function_ctx->listener_entries);
  }

  hoox_function_context_fixup_cpu_context (function_ctx, cpu_context);

  if (invoke_listeners)
  {
    HxPtrArray * listener_entries;
    hx_uint i;

    invocation_ctx->cpu_context = cpu_context;
    invocation_ctx->backend = &interceptor_ctx->listener_backend;

    listener_entries = stack_entry->listener_entries;
    for (i = 0; i != listener_entries->len; i++)
    {
      ListenerEntry * listener_entry;
      ListenerInvocationState state;

      listener_entry = hx_ptr_array_index (listener_entries, i);
      if (listener_entry == NULL)
        continue;

      if (only_invoke_unignorable_listeners && !listener_entry->unignorable)
        continue;

      state.point_cut = HOOX_POINT_ENTER;
      state.entry = listener_entry;
      state.interceptor_ctx = interceptor_ctx;
      state.invocation_data = stack_entry->listener_invocation_data[i];
      invocation_ctx->backend->data = &state;

      if (listener_entry->listener_interface->on_enter != NULL)
      {
        listener_entry->listener_interface->on_enter (
            listener_entry->listener_instance, invocation_ctx);
      }
    }

    system_error = invocation_ctx->system_error;
  }

  if (!will_trap_on_leave && invoke_listeners)
  {
    hoox_invocation_stack_pop (interceptor_ctx->stack);
  }

  hoox_thread_set_system_error (system_error);

  hoox_tls_key_set_value (hoox_interceptor_guard_key, NULL);

  if (will_trap_on_leave)
  {
    *caller_ret_addr = function_ctx->on_leave_trampoline;
  }

  if (function_ctx->replacement_function != NULL)
  {
    stack_entry->calling_replacement = TRUE;
    stack_entry->cpu_context = *cpu_context;
    stack_entry->original_system_error = system_error;
    invocation_ctx->cpu_context = &stack_entry->cpu_context;
    invocation_ctx->backend = &interceptor_ctx->replacement_backend;
    invocation_ctx->backend->data = function_ctx->replacement_data;

    *next_hop = function_ctx->replacement_function;
  }
  else
  {
    *next_hop = function_ctx->on_invoke_trampoline;
  }

bypass:
  if (!will_trap_on_leave)
  {
    hx_atomic_int_dec_and_test (&function_ctx->trampoline_usage_counter);
  }

  return will_trap_on_leave;
}

void
_hoox_function_context_end_invocation (HooxFunctionContext * function_ctx,
                                      HooxCpuContext * cpu_context,
                                      hx_pointer * next_hop)
{
  hx_int system_error;
  InterceptorThreadContext * interceptor_ctx;
  HooxInvocationStackEntry * stack_entry;
  HooxInvocationContext * invocation_ctx;
  HxPtrArray * listener_entries;
  hx_boolean only_invoke_unignorable_listeners;
  hx_uint i;

#ifdef HAVE_WINDOWS
  system_error = hoox_thread_get_system_error ();
#endif

  hoox_tls_key_set_value (hoox_interceptor_guard_key, function_ctx->interceptor);

#ifndef HAVE_WINDOWS
  system_error = hoox_thread_get_system_error ();
#endif

  interceptor_ctx = get_interceptor_thread_context ();

  hoox_invocation_stack_reap_unwound_above (interceptor_ctx->stack,
      function_ctx);

  stack_entry = hoox_invocation_stack_peek_top (interceptor_ctx->stack);
  *next_hop = hoox_sign_code_pointer (stack_entry->caller_ret_addr);

  invocation_ctx = &stack_entry->invocation_context;
  invocation_ctx->cpu_context = cpu_context;
  if (stack_entry->calling_replacement &&
      invocation_ctx->system_error != stack_entry->original_system_error)
  {
    system_error = invocation_ctx->system_error;
  }
  else
  {
    invocation_ctx->system_error = system_error;
  }
  invocation_ctx->backend = &interceptor_ctx->listener_backend;

  hoox_function_context_fixup_cpu_context (function_ctx, cpu_context);

  listener_entries = stack_entry->listener_entries;
  only_invoke_unignorable_listeners =
      stack_entry->only_invoke_unignorable_listeners;
  for (i = 0; i != listener_entries->len; i++)
  {
    ListenerEntry * listener_entry;
    ListenerInvocationState state;

    listener_entry = hx_ptr_array_index (listener_entries, i);
    if (listener_entry == NULL)
      continue;

    if (only_invoke_unignorable_listeners && !listener_entry->unignorable)
      continue;

    state.point_cut = HOOX_POINT_LEAVE;
    state.entry = listener_entry;
    state.interceptor_ctx = interceptor_ctx;
    state.invocation_data = stack_entry->listener_invocation_data[i];
    invocation_ctx->backend->data = &state;

    if (listener_entry->listener_interface->on_leave != NULL)
    {
      listener_entry->listener_interface->on_leave (
          listener_entry->listener_instance, invocation_ctx);
    }
  }

  hoox_thread_set_system_error (invocation_ctx->system_error);

  hoox_invocation_stack_pop (interceptor_ctx->stack);

  hoox_tls_key_set_value (hoox_interceptor_guard_key, NULL);

  hx_atomic_int_dec_and_test (&function_ctx->trampoline_usage_counter);
}

static void
hoox_function_context_fixup_cpu_context (HooxFunctionContext * function_ctx,
                                        HooxCpuContext * cpu_context)
{
  hx_size pc;

  pc = HX_POINTER_TO_SIZE (function_ctx->function_address);
#ifdef HAVE_ARM
  pc &= ~1;
#endif

#if defined (HAVE_I386)
# if HX_SIZEOF_VOID_P == 4
  cpu_context->eip = pc;
# else
  cpu_context->rip = pc;
# endif
#elif defined (HAVE_ARM)
  cpu_context->pc = pc;
#elif defined (HAVE_ARM64)
  cpu_context->pc = pc;
#else
# error Unsupported architecture
#endif
}

static InterceptorThreadContext *
get_interceptor_thread_context (void)
{
  InterceptorThreadContext * context;

  context = hx_private_get (&hoox_interceptor_context_private);
  if (context == NULL || context->generation != hoox_interceptor_generation)
  {
    if (context != NULL)
      interceptor_thread_context_destroy (context);

    context = interceptor_thread_context_new ();
    context->generation = hoox_interceptor_generation;

    hoox_spinlock_acquire (&hoox_interceptor_thread_context_lock);
    hx_hash_table_add (hoox_interceptor_thread_contexts, context);
    hoox_spinlock_release (&hoox_interceptor_thread_context_lock);

    hx_private_set (&hoox_interceptor_context_private, context);
  }

  return context;
}

static void
release_interceptor_thread_context (InterceptorThreadContext * context)
{
  hoox_spinlock_acquire (&hoox_interceptor_thread_context_lock);
  if (hoox_interceptor_thread_contexts != NULL)
    hx_hash_table_remove (hoox_interceptor_thread_contexts, context);
  hoox_spinlock_release (&hoox_interceptor_thread_context_lock);

  interceptor_thread_context_destroy (context);
}

static HooxPointCut
hoox_interceptor_invocation_get_listener_point_cut (
    HooxInvocationContext * context)
{
  return ((ListenerInvocationState *) context->backend->data)->point_cut;
}

static HooxPointCut
hoox_interceptor_invocation_get_replacement_point_cut (
    HooxInvocationContext * context)
{
  return HOOX_POINT_ENTER;
}

static HooxThreadId
hoox_interceptor_invocation_get_thread_id (HooxInvocationContext * context)
{
  return hoox_process_get_current_thread_id ();
}

static hx_uint
hoox_interceptor_invocation_get_depth (HooxInvocationContext * context)
{
  InterceptorThreadContext * interceptor_ctx =
      (InterceptorThreadContext *) context->backend->state;

  return interceptor_ctx->stack->len - 1;
}

static hx_pointer
hoox_interceptor_invocation_get_listener_thread_data (
    HooxInvocationContext * context,
    hx_size required_size)
{
  ListenerInvocationState * data =
      (ListenerInvocationState *) context->backend->data;

  return interceptor_thread_context_get_listener_data (data->interceptor_ctx,
      data->entry->listener_instance, required_size);
}

static hx_pointer
hoox_interceptor_invocation_get_listener_function_data (
    HooxInvocationContext * context)
{
  return ((ListenerInvocationState *)
      context->backend->data)->entry->function_data;
}

static hx_pointer
hoox_interceptor_invocation_get_listener_invocation_data (
    HooxInvocationContext * context,
    hx_size required_size)
{
  ListenerInvocationState * data;

  data = (ListenerInvocationState *) context->backend->data;

  if (required_size > HOOX_MAX_LISTENER_DATA)
    return NULL;

  return data->invocation_data;
}

static hx_pointer
hoox_interceptor_invocation_get_replacement_data (HooxInvocationContext * context)
{
  return context->backend->data;
}

static const HooxInvocationBackend
hoox_interceptor_listener_invocation_backend =
{
  hoox_interceptor_invocation_get_listener_point_cut,

  hoox_interceptor_invocation_get_thread_id,
  hoox_interceptor_invocation_get_depth,

  hoox_interceptor_invocation_get_listener_thread_data,
  hoox_interceptor_invocation_get_listener_function_data,
  hoox_interceptor_invocation_get_listener_invocation_data,

  NULL,

  NULL,
  NULL
};

static const HooxInvocationBackend
hoox_interceptor_replacement_invocation_backend =
{
  hoox_interceptor_invocation_get_replacement_point_cut,

  hoox_interceptor_invocation_get_thread_id,
  hoox_interceptor_invocation_get_depth,

  NULL,
  NULL,
  NULL,

  hoox_interceptor_invocation_get_replacement_data,

  NULL,
  NULL
};

static InterceptorThreadContext *
interceptor_thread_context_new (void)
{
  InterceptorThreadContext * context;

  context = hx_slice_new0 (InterceptorThreadContext);

  memcpy (&context->listener_backend,
      &hoox_interceptor_listener_invocation_backend,
      sizeof (HooxInvocationBackend));
  memcpy (&context->replacement_backend,
      &hoox_interceptor_replacement_invocation_backend,
      sizeof (HooxInvocationBackend));
  context->listener_backend.state = context;
  context->replacement_backend.state = context;

  context->ignore_level = 0;

  context->stack = hx_array_sized_new (FALSE, TRUE,
      sizeof (HooxInvocationStackEntry), HOOX_MAX_CALL_DEPTH);

  context->listener_data_slots = hx_array_sized_new (FALSE, TRUE,
      sizeof (ListenerDataSlot), HOOX_MAX_LISTENERS_PER_FUNCTION);

  return context;
}

static void
interceptor_thread_context_destroy (InterceptorThreadContext * context)
{
  HooxInvocationStack * stack = context->stack;
  hx_uint i;

  hx_array_free (context->listener_data_slots, TRUE);

  for (i = 0; i != stack->len; i++)
  {
    hoox_invocation_stack_entry_release_trampoline (
        &hx_array_index (stack, HooxInvocationStackEntry, i));
  }

  hx_array_free (stack, TRUE);

  hx_slice_free (InterceptorThreadContext, context);
}

static hx_pointer
interceptor_thread_context_get_listener_data (InterceptorThreadContext * self,
                                              HooxInvocationListener * listener,
                                              hx_size required_size)
{
  hx_uint i;
  ListenerDataSlot * available_slot = NULL;

  if (required_size > HOOX_MAX_LISTENER_DATA)
    return NULL;

  for (i = 0; i != self->listener_data_slots->len; i++)
  {
    ListenerDataSlot * slot;

    slot = &hx_array_index (self->listener_data_slots, ListenerDataSlot, i);
    if (slot->owner == listener)
      return slot->data;
    else if (slot->owner == NULL)
      available_slot = slot;
  }

  if (available_slot == NULL)
  {
    hx_array_set_size (self->listener_data_slots,
        self->listener_data_slots->len + 1);
    available_slot = &hx_array_index (self->listener_data_slots,
        ListenerDataSlot, self->listener_data_slots->len - 1);
  }
  else
  {
    memset (available_slot->data, 0, sizeof (available_slot->data));
  }

  available_slot->owner = listener;

  return available_slot->data;
}

static void
interceptor_thread_context_forget_listener_data (
    InterceptorThreadContext * self,
    HooxInvocationListener * listener)
{
  hx_uint i;

  for (i = 0; i != self->listener_data_slots->len; i++)
  {
    ListenerDataSlot * slot;

    slot = &hx_array_index (self->listener_data_slots, ListenerDataSlot, i);
    if (slot->owner == listener)
    {
      slot->owner = NULL;
      return;
    }
  }
}

static HooxInvocationStackEntry *
hoox_invocation_stack_push (HooxInvocationStack * stack,
                           HooxFunctionContext * function_ctx,
                           hx_pointer caller_ret_addr,
                           hx_pointer stack_address,
                           hx_boolean only_invoke_unignorable_listeners)
{
  HooxInvocationStackEntry * entry;
  HooxInvocationContext * ctx;

  hx_array_set_size (stack, stack->len + 1);
  entry = (HooxInvocationStackEntry *)
      &hx_array_index (stack, HooxInvocationStackEntry, stack->len - 1);
  entry->function_ctx = function_ctx;
  entry->caller_ret_addr = caller_ret_addr;
  entry->stack_address = stack_address;
  entry->only_invoke_unignorable_listeners = only_invoke_unignorable_listeners;

  ctx = &entry->invocation_context;
  ctx->function = hoox_sign_code_pointer (function_ctx->function_address);

  ctx->backend = NULL;

  return entry;
}

static hx_pointer
hoox_invocation_stack_pop (HooxInvocationStack * stack)
{
  HooxInvocationStackEntry * entry;
  hx_pointer caller_ret_addr;

  entry = (HooxInvocationStackEntry *)
      &hx_array_index (stack, HooxInvocationStackEntry, stack->len - 1);
  caller_ret_addr = entry->caller_ret_addr;
  hx_array_set_size (stack, stack->len - 1);

  return caller_ret_addr;
}

static void
hoox_invocation_stack_reap_unwound (HooxInvocationStack * stack,
                                   hx_pointer live_stack_address)
{
  while (stack->len != 0)
  {
    HooxInvocationStackEntry * entry;

    entry = (HooxInvocationStackEntry *)
        &hx_array_index (stack, HooxInvocationStackEntry, stack->len - 1);
    if (!hoox_invocation_stack_entry_was_unwound_past (entry,
        live_stack_address))
      break;

    hoox_invocation_stack_entry_release_trampoline (entry);
    hx_array_set_size (stack, stack->len - 1);
  }
}

static void
hoox_invocation_stack_reap_unwound_above (HooxInvocationStack * stack,
                                         HooxFunctionContext * returning_ctx)
{
  /*
   * Reap entries sitting above the frame we are about to return from, leaving
   * that frame on top. Calls nest last-in-first-out, and entries that don't
   * trap on leave are popped right away on enter, so any entry still stacked
   * above our frame belongs to a deeper call that was unwound past by a C++
   * exception or longjmp(), skipping its on-leave trampoline.
   *
   * We cannot lean on the leave-time stack pointer the way the on-enter path
   * does: a callee-clean calling convention such as x86 stdcall pops the
   * arguments on return, so the leave-time stack pointer sits above our own
   * recorded stack address, and a frame-pointer-omitting caller and callee
   * may even share one. Matching on the returning function context sidesteps
   * both pitfalls.
   */
  while (stack->len != 0)
  {
    HooxInvocationStackEntry * entry;

    entry = (HooxInvocationStackEntry *)
        &hx_array_index (stack, HooxInvocationStackEntry, stack->len - 1);
    if (entry->function_ctx == returning_ctx)
      break;

    hoox_invocation_stack_entry_release_trampoline (entry);
    hx_array_set_size (stack, stack->len - 1);
  }
}

static hx_boolean
hoox_invocation_stack_entry_was_unwound_past (
    const HooxInvocationStackEntry * entry,
    hx_pointer live_stack_address)
{
  return (hx_uint8 *) entry->stack_address < (hx_uint8 *) live_stack_address;
}

static void
hoox_invocation_stack_entry_release_trampoline (
    const HooxInvocationStackEntry * entry)
{
  hx_atomic_int_dec_and_test (&entry->function_ctx->trampoline_usage_counter);
}

static HooxInvocationStackEntry *
hoox_invocation_stack_peek_top (HooxInvocationStack * stack)
{
  if (stack->len == 0)
    return NULL;

  return &hx_array_index (stack, HooxInvocationStackEntry, stack->len - 1);
}

static hx_pointer
hoox_interceptor_resolve (HooxInterceptor * self,
                         hx_pointer address)
{
  address = hoox_strip_code_pointer (address);

  if (!hoox_interceptor_has (self, address))
  {
    const hx_size max_redirect_size = 16;
    hx_pointer target;

    hoox_ensure_code_readable (address, max_redirect_size);

    /* Avoid following grafted branches. */
    if (hoox_process_get_code_signing_policy () == HOOX_CODE_SIGNING_REQUIRED)
      return address;

    target = _hoox_interceptor_backend_resolve_redirect (self->backend,
        address);
    if (target != NULL)
      return hoox_interceptor_resolve (self, target);
  }

  return address;
}

static hx_boolean
hoox_interceptor_has (HooxInterceptor * self,
                     hx_pointer function_address)
{
  return hx_hash_table_lookup (self->function_by_address,
      function_address) != NULL;
}

static hx_pointer
hoox_page_address_from_pointer (hx_pointer ptr)
{
  return HX_SIZE_TO_POINTER (
      HX_POINTER_TO_SIZE (ptr) & ~((hx_size) hoox_query_page_size () - 1));
}

static hx_int
hoox_page_address_compare (hx_constpointer * a,
                          hx_constpointer * b)
{
  hx_ssize diff = (hx_ssize) HX_POINTER_TO_SIZE (*a) - (hx_ssize) HX_POINTER_TO_SIZE (*b);

  return diff < 0 ? -1 : (diff > 0 ? 1 : 0);
}
