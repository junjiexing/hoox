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

typedef struct _HooxPatchCodeContext HooxPatchCodeContext;
typedef struct _HooxPageLump HooxPageLump;
typedef struct _HooxSuspendOperation HooxSuspendOperation;


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

struct _HooxSuspendOperation
{
  HooxThreadId current_thread_id;
  HooxMetalArray suspended_threads;
};

static void hoox_apply_patch_code (hx_pointer mem, hx_pointer target_page,
    hx_uint n_pages, hx_pointer user_data);
static hx_boolean hoox_maybe_suspend_thread (const HooxThreadDetails * details,
    hx_pointer user_data);


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
  hx_boolean result = TRUE;
  hx_size page_size;
  hx_uint i;
  hx_uint8 * apply_start = NULL, * apply_target_start = NULL;
  hx_uint apply_num_pages;
  hx_boolean rwx_supported;

  rwx_supported = hoox_query_is_rwx_supported ();
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
    else
    {
      hoox_metal_array_init (&suspend_op.suspended_threads,
          sizeof (HooxThreadId));

      suspend_op.current_thread_id = hoox_process_get_current_thread_id ();
      _hoox_process_enumerate_threads (hoox_maybe_suspend_thread, &suspend_op,
          HOOX_THREAD_FLAGS_NONE);
    }

#if defined (HAVE_DARWIN) && defined (HAVE_ARM64)
    /*
     * Apple arm64 has no RWX: the in-place path below drops execute from the
     * target page during the write, which self-faults only if hoox's own patch
     * code shares that 16 KiB page (self-hosting). Detect that collision — the
     * target page matching a patch-critical anchor that runs while the page is
     * writable — and, only then, write from an off-page stub instead. The
     * common separate-page case keeps the proven in-place path unchanged.
     */
    if (!rwx_supported)
    {
      const hx_pointer anchors[] = {
        (hx_pointer) hoox_memory_patch_code_pages,
        (hx_pointer) apply,
        (hx_pointer) hx_hash_table_lookup,
      };
      hx_boolean collides = FALSE;
      hx_uint a;

      for (i = 0; i != sorted_addresses->len && !collides; i++)
      {
        hx_size tp = HX_POINTER_TO_SIZE (hx_ptr_array_index (sorted_addresses, i))
            & ~(page_size - 1);
        for (a = 0; a != HX_N_ELEMENTS (anchors); a++)
        {
          if ((HX_POINTER_TO_SIZE (anchors[a]) & ~(page_size - 1)) == tp)
            collides = TRUE;
        }
      }

      if (collides)
      {
        result = _hoox_darwin_arm64_patch_pages (sorted_addresses, coalesce,
            apply, apply_data, page_size);
        goto resume_threads;
      }
    }
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
    if (!rwx_supported)
    {
      hx_uint num_suspended, i;

      num_suspended = suspend_op.suspended_threads.length;

      for (i = 0; i != num_suspended; i++)
      {
        HooxThreadId * raw_id = hoox_metal_array_element_at (
            &suspend_op.suspended_threads, i);

        hoox_thread_resume (*raw_id, NULL);
#ifdef HAVE_DARWIN
        mach_port_mod_refs (mach_task_self (), *raw_id,
            MACH_PORT_RIGHT_SEND, -1);
#endif
      }

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
  HooxThreadId * suspended_id;

  if (details->id == op->current_thread_id)
    goto skip;

  if (!hoox_thread_suspend (details->id, NULL))
    goto skip;

#ifdef HAVE_DARWIN
  mach_port_mod_refs (mach_task_self (), details->id, MACH_PORT_RIGHT_SEND, 1);
#endif
  suspended_id = hoox_metal_array_append (&op->suspended_threads);
  *suspended_id = details->id;

skip:
  return TRUE;
}

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
