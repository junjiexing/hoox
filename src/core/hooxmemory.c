/*
 * Copyright (C) 2010-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2021 Abdelrahman Eid <hot3eed@gmail.com>
 * Copyright (C) 2025 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxmemory.h"

#include "hooxcloak-priv.h"
#include "hooxcodesegment.h"
#include "hooxmemory-priv.h"
#include "hooxmetalarray.h"
#include "hooxprocess-priv.h"

#ifdef HAVE_PTRAUTH
# include <ptrauth.h>
#endif
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_ANDROID
# include "hooxandroid.h"
#endif
#ifdef HOOX_USE_DLMALLOC
# ifdef HAVE_DARWIN
#  define DARWIN                   1
# endif
# define MSPACES                   1
# define ONLY_MSPACES              1
# define USE_LOCKS                 1
# define FOOTERS                   0
# define INSECURE                  1
# define NO_MALLINFO               0
# define REALLOC_ZERO_BYTES_FREES  1
# ifdef HAVE_LIBC_MALLINFO
#  include <malloc.h>
#  define STRUCT_MALLINFO_DECLARED 1
# endif
# ifdef _MSC_VER
#  pragma warning (push)
#  pragma warning (disable: 4267 4702)
# endif
# ifdef _GNU_SOURCE
#  undef _GNU_SOURCE
# endif
# include "dlmalloc.c"
# ifdef _MSC_VER
#  pragma warning (pop)
# endif
#endif
#ifdef HAVE_DARWIN
# include <mach/mach.h>
#endif

#define HOOX_WINDOWS_THREAD_SUSPEND_MAX_RETRIES 100

typedef struct _HooxPatchCodeContext HooxPatchCodeContext;
typedef struct _HooxPageLump HooxPageLump;
typedef struct _HooxSuspendedThread HooxSuspendedThread;
typedef struct _HooxSuspendOperation HooxSuspendOperation;

#if defined (HAVE_LINUX) && defined (HOOX_POSIX_PATCH_PC_GUARD)
typedef struct _HooxPeerPark HooxPeerPark;
#endif


struct _HooxPatchCodeContext
{
  hx_size page_offset;
  HooxMemoryPatchApplyFunc func;
  hx_pointer user_data;
};

struct _HooxPageLump
{
  hx_pointer start;
  hx_pointer end;
  hx_pointer writable_start;
  hx_uint n_pages;
};

struct _HooxSuspendedThread
{
  HooxThreadId id;
#ifdef HAVE_WINDOWS
  hx_pointer handle;
#endif
};

struct _HooxSuspendOperation
{
  HooxThreadId current_thread_id;
  HooxMetalArray suspended_threads;
  hx_uint newly_suspended;

#ifdef HOOX_WINDOWS_PATCH_PC_GUARD
  const HooxPcGuardRange * guard_ranges;
  hx_uint num_guard_ranges;
  hx_uint guard_attempts_left;
#endif
#if defined (HAVE_LINUX) && defined (HOOX_POSIX_PATCH_PC_GUARD)
  HooxPeerPark * park;
#endif
};

static void hoox_apply_patch_code (hx_pointer mem, hx_pointer target_page,
    hx_uint n_pages, hx_pointer user_data);
static hx_boolean hoox_maybe_suspend_thread (const HooxThreadDetails * details,
    hx_pointer user_data);
#ifdef HAVE_WINDOWS
static hx_boolean hoox_suspend_all_peer_threads (HooxSuspendOperation * op);
#endif
static hx_boolean hoox_resume_suspended_threads (HooxSuspendOperation * op);
#if defined (HAVE_WINDOWS) && defined (HOOX_WINDOWS_PATCH_PC_GUARD)
static hx_boolean hoox_suspended_threads_clear_of_ranges (
    HooxSuspendOperation * op);
#endif


static hx_uint hoox_heap_ref_count = 0;
#ifdef HOOX_USE_DLMALLOC
static mspace hoox_mspace_main = NULL;
static mspace hoox_mspace_internal = NULL;
#endif
static hx_uint hoox_cached_page_size;

#ifdef HAVE_ANDROID
HX_LOCK_DEFINE_STATIC (hoox_softened_code_pages);
static HxHashTable * hoox_softened_code_pages;
#endif


void
hoox_internal_heap_ref (void)
{
  if (hoox_heap_ref_count++ > 0)
    return;

  _hoox_memory_backend_init ();

  hoox_cached_page_size = _hoox_memory_backend_query_page_size ();

  _hoox_cloak_init ();

#ifdef HOOX_USE_DLMALLOC
  hoox_mspace_main = create_mspace (0, TRUE);
  hoox_mspace_internal = create_mspace (0, TRUE);
#endif
}

void
hoox_internal_heap_unref (void)
{
  hx_assert (hoox_heap_ref_count != 0);
  if (--hoox_heap_ref_count > 0)
    return;

#ifdef HOOX_USE_DLMALLOC
  destroy_mspace (hoox_mspace_internal);
  hoox_mspace_internal = NULL;

  destroy_mspace (hoox_mspace_main);
  hoox_mspace_main = NULL;

  (void) DESTROY_LOCK (&malloc_global_mutex);
#endif

  _hoox_cloak_deinit ();

  _hoox_memory_backend_deinit ();
}

hx_pointer
hoox_sign_code_pointer (hx_pointer value)
{
#ifdef HAVE_PTRAUTH
  return ptrauth_sign_unauthenticated (value, ptrauth_key_asia, 0);
#else
  return value;
#endif
}

hx_pointer
hoox_strip_code_pointer (hx_pointer value)
{
#ifdef HAVE_PTRAUTH
  return ptrauth_strip (value, ptrauth_key_asia);
#else
  return value;
#endif
}

HooxAddress
hoox_sign_code_address (HooxAddress value)
{
#ifdef HAVE_PTRAUTH
  return HX_POINTER_TO_SIZE (ptrauth_sign_unauthenticated (
      HX_SIZE_TO_POINTER (value), ptrauth_key_asia, 0));
#else
  return value;
#endif
}

HooxPtrauthSupport
hoox_query_ptrauth_support (void)
{
#ifdef HAVE_PTRAUTH
  return HOOX_PTRAUTH_SUPPORTED;
#else
  return HOOX_PTRAUTH_UNSUPPORTED;
#endif
}

hx_uint
hoox_query_page_size (void)
{
  return hoox_cached_page_size;
}

hx_boolean
hoox_query_is_rwx_supported (void)
{
  return hoox_query_rwx_support () == HOOX_RWX_FULL;
}

#ifdef HX_OS_NONE
HX_GNUC_WEAK
#endif
HooxRwxSupport
hoox_query_rwx_support (void)
{
#if defined (HAVE_DARWIN) && !defined (HAVE_I386)
  return HOOX_RWX_NONE;
#else
  return HOOX_RWX_FULL;
#endif
}

/**
 * hoox_memory_patch_code:
 * @address: address to modify from
 * @size: number of bytes to modify
 * @apply: (scope call): function to apply the modifications
 *
 * Safely modifies @size bytes at @address. The supplied function @apply gets
 * called with a writable pointer where you must write the desired
 * modifications before returning. Do not make any assumptions about this being
 * the same location as @address, as some systems require modifications to be
 * written to a temporary location before being mapped into memory on top of the
 * original memory page (e.g. on iOS, where directly modifying in-memory code
 * may result in the process losing its HX_VALID status).
 *
 * Returns: whether the modifications were successfully applied
 */
#ifdef _MSC_VER
static __declspec (thread) hx_boolean hoox_external_thread_suspension;
#else
static _Thread_local hx_boolean hoox_external_thread_suspension;
#endif

void
hoox_memory_set_external_thread_suspension (hx_boolean enabled)
{
  hoox_external_thread_suspension = enabled;
}

hx_boolean
hoox_memory_patch_code (hx_pointer address,
                       hx_size size,
                       HooxMemoryPatchApplyFunc apply,
                       hx_pointer apply_data)
{
  hx_boolean result;
  hx_size page_size;
  hx_uint8 * start_page, * end_page;
  hx_size page_offset;
  HxPtrArray * page_addresses;
  HooxPatchCodeContext context;

  address = hoox_strip_code_pointer (address);

  page_size = hoox_query_page_size ();
  start_page = HX_SIZE_TO_POINTER (HX_POINTER_TO_SIZE (address) & ~(page_size - 1));
  end_page = HX_SIZE_TO_POINTER (
      (HX_POINTER_TO_SIZE (address) + size - 1) & ~(page_size - 1));
  page_offset = ((hx_uint8 *) address) - start_page;

  page_addresses =
      hx_ptr_array_sized_new ((hx_uint) (((end_page - start_page) / page_size) + 1));

  hx_ptr_array_add (page_addresses, start_page);

  if (end_page != start_page)
  {
    hx_uint8 * cur;

    for (cur = start_page + page_size;
        cur != end_page + page_size;
        cur += page_size)
    {
      hx_ptr_array_add (page_addresses, cur);
    }
  }

  context.page_offset = page_offset;
  context.func = apply;
  context.user_data = apply_data;

  result = hoox_memory_patch_code_pages (page_addresses, TRUE,
      hoox_apply_patch_code, &context);

  hx_ptr_array_unref (page_addresses);

  return result;
}

static void
hoox_apply_patch_code (hx_pointer mem,
                      hx_pointer target_page,
                      hx_uint n_pages,
                      hx_pointer user_data)
{
  HooxPatchCodeContext * context = user_data;

  context->func ((hx_uint8 *) mem + context->page_offset, context->user_data);
}

/**
 * hoox_memory_patch_code_pages: (skip)
 *
 * Safely modifies code pages at the given addresses.
 */
hx_boolean
hoox_memory_patch_code_pages (HxPtrArray * sorted_addresses,
                             hx_boolean coalesce,
                             HooxMemoryPatchPagesApplyFunc apply,
                             hx_pointer apply_data)
{
  /* The retry budget matters only to the guard variants: Windows retries the
   * suspend-and-scan, Linux retries a failed peer park (transient under load,
   * e.g. a CPU-starved thread missing its signal window). */
  return hoox_memory_patch_code_pages_guarded (sorted_addresses, coalesce,
      apply, apply_data, NULL, 0, 100);
}

/**
 * hoox_memory_patch_code_pages_guarded: (skip)
 *
 * Safely modifies code pages at the given addresses. When built with
 * HOOX_WINDOWS_PATCH_PC_GUARD, additionally ensures (on Windows, where peer
 * threads are suspended while the patch bytes are written) that no suspended
 * thread's instruction pointer sits inside any of the given guard ranges.
 * Threads parked inside a range would resume executing from the middle of the
 * patched bytes and crash, so they are resumed, given a moment to move on, and
 * the suspend-and-scan is retried up to @max_guard_attempts times before
 * giving up. Without HOOX_WINDOWS_PATCH_PC_GUARD the guard ranges are ignored.
 */
hx_boolean
hoox_memory_patch_code_pages_guarded (HxPtrArray * sorted_addresses,
                             hx_boolean coalesce,
                             HooxMemoryPatchPagesApplyFunc apply,
                             hx_pointer apply_data,
                             const HooxPcGuardRange * guard_ranges,
                             hx_uint num_guard_ranges,
                             hx_uint max_guard_attempts)
{
  hx_boolean result = TRUE;
  hx_size page_size;
  hx_uint i;
  hx_uint8 * apply_start = NULL, * apply_target_start = NULL;
  hx_uint apply_num_pages;
  hx_boolean rwx_supported;
  hx_boolean suspend_threads;

#if (!defined (HAVE_WINDOWS) || !defined (HOOX_WINDOWS_PATCH_PC_GUARD)) && \
    !(defined (HAVE_LINUX) && defined (HOOX_POSIX_PATCH_PC_GUARD))
  (void) guard_ranges;
  (void) num_guard_ranges;
  (void) max_guard_attempts;
#endif

  rwx_supported = hoox_query_is_rwx_supported ();
  suspend_threads = !rwx_supported;
#ifdef HAVE_WINDOWS
  /* Windows normally takes the RWX path, but multi-byte x86/x64 patches are
   * not atomic. Keep peer threads out of the target bytes while they change. */
  suspend_threads = TRUE;
#endif
#if defined (HAVE_LINUX) && defined (HOOX_POSIX_PATCH_PC_GUARD)
  /* Same on Linux: patch writes are plain multi-byte stores, so park every
   * peer thread in the park signal handler for the duration of the write. */
  suspend_threads = TRUE;
#endif
  if (hoox_external_thread_suspension)
    /* The caller keeps every peer frozen by an out-of-process mechanism
     * (e.g. ptrace); an in-process stop-the-world would at best be redundant
     * and at worst deadlock against threads that cannot run signal handlers. */
    suspend_threads = FALSE;
  page_size = hoox_query_page_size ();

  if (hoox_memory_can_remap_writable ())
  {
    HxArray * plumps;
    HooxPageLump * last;

    plumps = hx_array_new (FALSE, FALSE, sizeof (HooxPageLump));
    last = NULL;

    for (i = 0; i != sorted_addresses->len; i++)
    {
      hx_uint8 * target_page = hx_ptr_array_index (sorted_addresses, i);

      last = (plumps->len != 0)
          ? &hx_array_index (plumps, HooxPageLump, plumps->len - 1)
          : NULL;

      if (last == NULL || last->end != target_page)
      {
        HooxPageLump lump;

        if (last != NULL)
        {
          hx_pointer writable;

          writable = hoox_memory_try_remap_writable_pages (last->start,
              last->n_pages);
          if (writable == NULL)
          {
            result = FALSE;
            goto cleanup;
          }

          last->writable_start = writable;
        }

        lump.start = target_page;
        lump.end = target_page;
        lump.writable_start = NULL;
        lump.n_pages = 0;

        hx_array_append_val (plumps, lump);
      }

      last = &hx_array_index (plumps, HooxPageLump, plumps->len - 1);
      last->end = target_page + page_size;
      last->n_pages++;
    }

    if (plumps->len == 0)
      goto cleanup;

    last->writable_start =
        hoox_memory_try_remap_writable_pages (last->start, last->n_pages);
    if (last->writable_start == NULL)
    {
      result = FALSE;
      goto cleanup;
    }

    if (coalesce)
    {
      for (i = 0; i != plumps->len; i++)
      {
        const HooxPageLump * plump = &hx_array_index (plumps, HooxPageLump, i);

        apply (plump->writable_start, plump->start, plump->n_pages, apply_data);
      }
    }
    else
    {
      hx_uint plump_index = 0;

      for (i = 0; i != sorted_addresses->len; i++)
      {
        hx_uint8 * target_page;
        const HooxPageLump * plump;
        hx_size offset;

        target_page = hx_ptr_array_index (sorted_addresses, i);

        plump = &hx_array_index (plumps, HooxPageLump, plump_index);

        if (target_page >= (hx_uint8 *) plump->end)
        {
          plump_index++;
          hx_assert (plump_index != plumps->len);
          plump = &hx_array_index (plumps, HooxPageLump, plump_index);
        }

        hx_assert (target_page >= (hx_uint8 *) plump->start);
        hx_assert (target_page < (hx_uint8 *) plump->end);
        offset = target_page - (hx_uint8 *) plump->start;

        apply ((hx_uint8 *) plump->writable_start + offset, target_page, 1,
            apply_data);
      }
    }

    for (i = 0; i != sorted_addresses->len; i++)
    {
      hx_pointer target_page = hx_ptr_array_index (sorted_addresses, i);

      hoox_clear_cache (target_page, page_size);
    }

cleanup:
    for (i = 0; i != plumps->len; i++)
    {
      const HooxPageLump * plump = &hx_array_index (plumps, HooxPageLump, i);

      if (plump->writable_start != NULL)
      {
        hoox_memory_dispose_writable_pages (plump->writable_start,
            plump->n_pages);
      }
    }

    hx_array_unref (plumps);
  }
  else if (rwx_supported || !hoox_code_segment_is_supported ())
  {
    HooxPageProtection protection;
    HooxPageProtection * original_protections;
    HooxSuspendOperation suspend_op = { 0, };

    protection = rwx_supported ? HOOX_PAGE_RWX : HOOX_PAGE_RW;

    original_protections = hx_newa (HooxPageProtection, sorted_addresses->len);

    if (rwx_supported)
    {
#ifdef HAVE_LINUX
      _hoox_memory_query_protections (sorted_addresses, original_protections);
#else
      for (i = 0; i != sorted_addresses->len; i++)
      {
        hx_pointer target_page = hx_ptr_array_index (sorted_addresses, i);

        if (!hoox_memory_query_protection (target_page,
            &original_protections[i]))
          original_protections[i] = HOOX_PAGE_RX;
      }
#endif
    }

    if (suspend_threads)
    {
#ifdef HOOX_WINDOWS_PATCH_PC_GUARD
      suspend_op.guard_ranges = guard_ranges;
      suspend_op.num_guard_ranges = num_guard_ranges;
      suspend_op.guard_attempts_left = max_guard_attempts;
#endif

      hoox_metal_array_init (&suspend_op.suspended_threads,
          sizeof (HooxSuspendedThread));

      suspend_op.current_thread_id = hoox_process_get_current_thread_id ();
#ifdef HAVE_WINDOWS
      if (!hoox_suspend_all_peer_threads (&suspend_op))
      {
        result = FALSE;
        goto resume_threads;
      }

#ifdef HOOX_WINDOWS_PATCH_PC_GUARD
      /*
       * A suspended peer thread whose instruction pointer sits inside a range
       * we are about to overwrite would resume executing from the middle of
       * the patched bytes and crash. Resume all threads, give them a moment
       * to move past the ranges, then suspend and scan again, with a bounded
       * number of attempts before giving up.
       */
      while (!hoox_suspended_threads_clear_of_ranges (&suspend_op))
      {
        if (suspend_op.guard_attempts_left == 0)
        {
          result = FALSE;
          goto resume_threads;
        }
        suspend_op.guard_attempts_left--;

        if (!hoox_resume_suspended_threads (&suspend_op))
        {
          result = FALSE;
          goto resume_threads;
        }
        _hoox_windows_sleep_ms (1);

        if (!hoox_suspend_all_peer_threads (&suspend_op))
        {
          result = FALSE;
          goto resume_threads;
        }
      }
#endif
#elif defined (HAVE_LINUX) && defined (HOOX_POSIX_PATCH_PC_GUARD)
      suspend_op.park = NULL;
      for (;;)
      {
        suspend_op.park = hoox_peer_park_begin ();
        if (suspend_op.park != NULL)
        {
          if (hoox_peer_park_all_clear_of (suspend_op.park,
                (const HooxPeerParkRange *) guard_ranges, num_guard_ranges))
            break;

          /* A parked peer still sits inside the bytes we are about to write;
           * release, let it move on, and park again. */
          hoox_peer_park_end (suspend_op.park);
          suspend_op.park = NULL;
        }
        /* A failed park (NULL) is usually transient too: a CPU-starved peer
         * missed its signal window, or a concurrent patch held the park lock.
         * Both get the same bounded retry as a peer caught in a guard range;
         * a genuinely unparkable thread (signal blocked) still fails closed. */
        if (max_guard_attempts == 0)
        {
          result = FALSE;
          goto resume_threads;
        }
        max_guard_attempts--;
        hx_usleep (1000);
      }
#else
      suspend_op.newly_suspended = 0;
      _hoox_process_enumerate_threads (hoox_maybe_suspend_thread, &suspend_op,
          HOOX_THREAD_FLAGS_NONE);
#endif
    }

#if defined (HAVE_DARWIN) && defined (HAVE_ARM64)
    /*
     * Apple arm64 has no RWX: the in-place path below drops execute from the
     * target page during the write. If any code hoox runs while that page is
     * writable happens to live on it (self-hosting), the patching thread faults
     * on its next instruction fetch. The apply callback reaches a non-trivial
     * call graph (the interceptor backend's trampoline writers and address
     * helper), so there is no small fixed set of pages that safely describes
     * "what executes during the window" — a heuristic guarding only a few
     * anchor functions provably misses those helpers and self-faults on layouts
     * where a target shares a page with them.
     *
     * We therefore always route Apple-arm64 patching through the off-page stub,
     * which performs the whole flip/write/restore from a hoox-owned scratch page
     * that is never a patch target, so it is correct for any layout. Define
     * HOOX_DARWIN_INPLACE_PATCH to opt back into the (faster but layout-unsafe)
     * in-place path below — only sound when no hooked function can share a
     * 16 KiB page with hoox's own interceptor/patch code.
     */
#ifndef HOOX_DARWIN_INPLACE_PATCH
    if (!rwx_supported)
    {
      result = _hoox_darwin_arm64_patch_pages (sorted_addresses, coalesce,
          apply, apply_data, page_size);
      goto resume_threads;
    }
#endif
#endif

    for (i = 0; i != sorted_addresses->len; i++)
    {
      hx_pointer target_page = hx_ptr_array_index (sorted_addresses, i);

      if (!hoox_try_mprotect (target_page, page_size, protection))
      {
        result = FALSE;
        goto resume_threads;
      }
    }

    apply_start = NULL;
    apply_num_pages = 0;
    for (i = 0; i != sorted_addresses->len; i++)
    {
      hx_pointer target_page = hx_ptr_array_index (sorted_addresses, i);

      if (coalesce)
      {
        if (apply_start != 0)
        {
          if (target_page == apply_start + (page_size * apply_num_pages))
          {
            apply_num_pages++;
          }
          else
          {
            apply (apply_start, apply_target_start, apply_num_pages,
                apply_data);
            apply_start = 0;
          }
        }

        if (apply_start == 0)
        {
          apply_start = target_page;
          apply_target_start = target_page;
          apply_num_pages = 1;
        }
      }
      else
      {
        apply (target_page, target_page, 1, apply_data);
      }
    }

    if (apply_num_pages != 0)
      apply (apply_start, apply_target_start, apply_num_pages, apply_data);

    for (i = 0; i != sorted_addresses->len; i++)
    {
      hx_pointer target_page = hx_ptr_array_index (sorted_addresses, i);
      HooxPageProtection restored;

      restored = (rwx_supported &&
          (original_protections[i] & HOOX_PAGE_WRITE) != 0)
          ? HOOX_PAGE_RWX
          : HOOX_PAGE_RX;

      if (!hoox_try_mprotect (target_page, page_size, restored))
      {
        result = FALSE;
        goto resume_threads;
      }
    }

    for (i = 0; i != sorted_addresses->len; i++)
    {
      hx_pointer target_page = hx_ptr_array_index (sorted_addresses, i);

      hoox_clear_cache (target_page, page_size);
    }

resume_threads:
    if (suspend_threads)
    {
#if defined (HAVE_LINUX) && defined (HOOX_POSIX_PATCH_PC_GUARD)
      if (suspend_op.park != NULL)
        hoox_peer_park_end (suspend_op.park);
#else
      if (!hoox_resume_suspended_threads (&suspend_op))
        result = FALSE;
#endif

      hoox_metal_array_free (&suspend_op.suspended_threads);
    }
  }
  else
  {
    HooxCodeSegment * segment;
    hx_uint8 * source_page, * current_page;
    hx_size source_offset;

    segment = hoox_code_segment_new (sorted_addresses->len * page_size, NULL);

    source_page = hoox_code_segment_get_address (segment);

    current_page = source_page;
    for (i = 0; i != sorted_addresses->len; i++)
    {
      hx_uint8 * target_page = hx_ptr_array_index (sorted_addresses, i);

      memcpy (current_page, target_page, page_size);

      current_page += page_size;
    }

    apply_start = NULL;
    apply_num_pages = 0;
    for (i = 0; i != sorted_addresses->len; i++)
    {
      hx_uint8 * target_page = hx_ptr_array_index (sorted_addresses, i);

      if (coalesce)
      {
        if (apply_start != NULL)
        {
          if (target_page == apply_target_start + (page_size * apply_num_pages))
          {
            apply_num_pages++;
          }
          else
          {
            apply (apply_start, apply_target_start, apply_num_pages,
                apply_data);
            apply_start = NULL;
          }
        }

        if (apply_start == NULL)
        {
          apply_start = source_page;
          apply_target_start = target_page;
          apply_num_pages = 1;
        }
      }
      else
      {
        apply (source_page, target_page, 1, apply_data);
      }

      source_page += page_size;
    }

    if (apply_num_pages != 0)
      apply (apply_start, apply_target_start, apply_num_pages, apply_data);

    hoox_code_segment_realize (segment);

    source_offset = 0;
    for (i = 0; i != sorted_addresses->len; i++)
    {
      hx_pointer target_page = hx_ptr_array_index (sorted_addresses, i);

      hoox_code_segment_map (segment, source_offset, page_size, target_page);

      hoox_clear_cache (target_page, page_size);

      source_offset += page_size;
    }

    hoox_code_segment_free (segment);
  }

  return result;
}

static hx_boolean
hoox_maybe_suspend_thread (const HooxThreadDetails * details,
                          hx_pointer user_data)
{
  HooxSuspendOperation * op = user_data;
  HooxSuspendedThread * suspended;
  hx_uint i;

  if (details->id == op->current_thread_id)
    goto skip;

  for (i = 0; i != op->suspended_threads.length; i++)
  {
    HooxSuspendedThread * existing =
        hoox_metal_array_element_at (&op->suspended_threads, i);
    if (existing->id == details->id)
      goto skip;
  }

#ifdef HAVE_WINDOWS
  {
    hx_pointer thread_handle;

    if (!_hoox_windows_suspend_thread (details->id, &thread_handle))
      return FALSE;

    suspended = hoox_metal_array_append (&op->suspended_threads);
    suspended->id = details->id;
    suspended->handle = thread_handle;
  }
#else
  if (!hoox_thread_suspend (details->id, NULL))
    goto skip;

#ifdef HAVE_DARWIN
  mach_port_mod_refs (mach_task_self (), details->id, MACH_PORT_RIGHT_SEND, 1);
#endif
  suspended = hoox_metal_array_append (&op->suspended_threads);
  suspended->id = details->id;
#endif
  op->newly_suspended++;

skip:
  return TRUE;
}

#ifdef HAVE_WINDOWS
static hx_boolean
hoox_suspend_all_peer_threads (HooxSuspendOperation * op)
{
  hx_uint attempts_left = HOOX_WINDOWS_THREAD_SUSPEND_MAX_RETRIES;

  while (TRUE)
  {
    hx_boolean success;

    do
    {
      op->newly_suspended = 0;
      success = _hoox_windows_enumerate_threads (hoox_maybe_suspend_thread, op,
          HOOX_THREAD_FLAGS_NONE);
    }
    while (success && op->newly_suspended != 0);

    if (success)
      return TRUE;

    /* A thread may exit between the snapshot and SuspendThread. Restart the
     * whole operation so no successfully suspended peer is left behind. */
    if (!hoox_resume_suspended_threads (op))
      return FALSE;

    if (attempts_left == 0)
      return FALSE;
    attempts_left--;

    _hoox_windows_sleep_ms (1);
  }
}
#endif

static hx_boolean
hoox_resume_suspended_threads (HooxSuspendOperation * op)
{
  hx_boolean success = TRUE;
  hx_uint i;

  for (i = 0; i != op->suspended_threads.length; i++)
  {
    HooxSuspendedThread * suspended =
        hoox_metal_array_element_at (&op->suspended_threads, i);

#ifdef HAVE_WINDOWS
    if (!_hoox_windows_resume_thread (suspended->handle))
      success = FALSE;
    _hoox_windows_close_thread (suspended->handle);
#else
    if (!hoox_thread_resume (suspended->id, NULL))
      success = FALSE;
#ifdef HAVE_DARWIN
    mach_port_mod_refs (mach_task_self (), suspended->id,
        MACH_PORT_RIGHT_SEND, -1);
#endif
#endif
  }

  hoox_metal_array_remove_all (&op->suspended_threads);

  return success;
}

#if defined (HAVE_WINDOWS) && defined (HOOX_WINDOWS_PATCH_PC_GUARD)
static hx_boolean
hoox_suspended_threads_clear_of_ranges (HooxSuspendOperation * op)
{
  hx_uint i;

  if (op->num_guard_ranges == 0)
    return TRUE;

  for (i = 0; i != op->suspended_threads.length; i++)
  {
    HooxSuspendedThread * suspended;
    hx_pointer ip;
    hx_uint r;

    suspended = hoox_metal_array_element_at (&op->suspended_threads, i);

    if (!_hoox_windows_query_thread_ip (suspended->handle, &ip))
      return FALSE;

    for (r = 0; r != op->num_guard_ranges; r++)
    {
      if ((const hx_uint8 *) ip >= (const hx_uint8 *) op->guard_ranges[r].begin &&
          (const hx_uint8 *) ip < (const hx_uint8 *) op->guard_ranges[r].end)
        return FALSE;
    }
  }

  return TRUE;
}
#endif

/* hoox:test-only-begin */
hx_boolean
hoox_memory_mark_code (hx_pointer address,
                      hx_size size)
{
  hx_boolean success;

  if (hoox_code_segment_is_supported ())
  {
    hx_size page_size;
    hx_uint8 * start_page, * end_page;

    page_size = hoox_query_page_size ();
    start_page =
        HX_SIZE_TO_POINTER (HX_POINTER_TO_SIZE (address) & ~(page_size - 1));
    end_page = HX_SIZE_TO_POINTER (
        (HX_POINTER_TO_SIZE (address) + size - 1) & ~(page_size - 1));

    success = hoox_code_segment_mark (start_page,
        end_page - start_page + page_size, NULL);
  }
  else
  {
    success = hoox_try_mprotect (address, size, HOOX_PAGE_RX);
  }

  hoox_clear_cache (address, size);

  return success;
}
/* hoox:test-only-end */
void
hoox_ensure_code_readable (hx_constpointer address,
                          hx_size size)
{
  /*
   * We will make this more generic once it's needed on other OSes.
   */
#ifdef HAVE_ANDROID
  hx_size page_size;
  hx_constpointer start_page, end_page, cur_page;

  if (hoox_android_get_api_level () < 29)
    return;

  page_size = hoox_query_page_size ();
  start_page = HX_SIZE_TO_POINTER (
      HX_POINTER_TO_SIZE (address) & ~(page_size - 1));
  end_page = HX_SIZE_TO_POINTER (
      HX_POINTER_TO_SIZE (address + size - 1) & ~(page_size - 1)) + page_size;

  HX_LOCK (hoox_softened_code_pages);

  if (hoox_softened_code_pages == NULL)
    hoox_softened_code_pages = hx_hash_table_new (NULL, NULL);

  for (cur_page = start_page; cur_page != end_page; cur_page += page_size)
  {
    HooxPageProtection prot;

    if (hx_hash_table_contains (hoox_softened_code_pages, cur_page))
      continue;

    if (!hoox_memory_query_protection (cur_page, &prot))
      continue;

    if ((prot & HOOX_PAGE_READ) != 0)
    {
      hx_hash_table_add (hoox_softened_code_pages, (hx_pointer) cur_page);
      continue;
    }

    if (hoox_try_mprotect ((hx_pointer) cur_page, page_size,
        prot | HOOX_PAGE_READ))
      hx_hash_table_add (hoox_softened_code_pages, (hx_pointer) cur_page);
  }

  HX_UNLOCK (hoox_softened_code_pages);
#endif
}

void
hoox_mprotect (hx_pointer address,
              hx_size size,
              HooxPageProtection prot)
{
  hx_boolean success;

  success = hoox_try_mprotect (address, size, prot);
  if (!success)
    hx_abort ();
}

#ifdef HOOX_USE_DLMALLOC

hx_pointer
hoox_malloc (hx_size size)
{
  return mspace_malloc (hoox_mspace_main, size);
}

hx_pointer
hoox_malloc0 (hx_size size)
{
  return mspace_calloc (hoox_mspace_main, 1, size);
}

hx_pointer
hoox_calloc (hx_size count,
            hx_size size)
{
  return mspace_calloc (hoox_mspace_main, count, size);
}

void
hoox_free (hx_pointer mem)
{
  mspace_free (hoox_mspace_main, mem);
}

hx_pointer
hoox_internal_malloc (size_t size)
{
  return mspace_malloc (hoox_mspace_internal, size);
}

hx_pointer
hoox_internal_calloc (size_t count,
                     size_t size)
{
  return mspace_calloc (hoox_mspace_internal, count, size);
}

void
hoox_internal_free (hx_pointer mem)
{
  mspace_free (hoox_mspace_internal, mem);
}

#else

hx_pointer
hoox_malloc (hx_size size)
{
  return malloc (size);
}

hx_pointer
hoox_malloc0 (hx_size size)
{
  return calloc (1, size);
}

hx_pointer
hoox_calloc (hx_size count,
            hx_size size)
{
  return calloc (count, size);
}

void
hoox_free (hx_pointer mem)
{
  free (mem);
}

hx_pointer
hoox_internal_malloc (size_t size)
{
  return hoox_malloc (size);
}

hx_pointer
hoox_internal_calloc (size_t count,
                     size_t size)
{
  return hoox_calloc (count, size);
}

void
hoox_internal_free (hx_pointer mem)
{
  hoox_free (mem);
}

#endif

hx_pointer
hoox_alloc_n_pages (hx_uint n_pages,
                   HooxPageProtection prot)
{
  hx_pointer result;

  result = hoox_try_alloc_n_pages (n_pages, prot);
  hx_assert (result != NULL);

  return result;
}

/* hoox:test-only-begin */
hx_pointer
hoox_alloc_n_pages_near (hx_uint n_pages,
                        HooxPageProtection prot,
                        const HooxAddressSpec * spec)
{
  hx_pointer result;

  result = hoox_try_alloc_n_pages_near (n_pages, prot, spec);
  hx_assert (result != NULL);

  return result;
}
/* hoox:test-only-end */

hx_boolean
hoox_address_spec_is_satisfied_by (const HooxAddressSpec * spec,
                                  hx_constpointer address)
{
  hx_size distance;

  distance =
      ABS ((const hx_uint8 *) spec->near_address - (const hx_uint8 *) address);

  return distance <= spec->max_distance;
}
