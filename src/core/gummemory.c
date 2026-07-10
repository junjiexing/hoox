/*
 * Copyright (C) 2010-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2021 Abdelrahman Eid <hot3eed@gmail.com>
 * Copyright (C) 2025 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gummemory.h"

#include "gumcloak-priv.h"
#include "gumcodesegment.h"
#include "gumlibc.h"
#include "gummemory-priv.h"
#include "gummetalarray.h"
#include "gumprocess-priv.h"

#ifdef HAVE_PTRAUTH
# include <ptrauth.h>
#endif
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_ANDROID
# include "gum/gumandroid.h"
#endif
#ifndef GUM_USE_SYSTEM_ALLOC
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
# include "backend-darwin/gumdarwin-priv.h"
# include "gum/gumdarwin.h"
#endif

#if defined (HAVE_I386) && GLIB_SIZEOF_VOID_P == 8
# include <emmintrin.h>
# define GUM_HAVE_POINTER_SCAN_SIMD
#elif defined (HAVE_ARM64)
# include <arm_neon.h>
# define GUM_HAVE_POINTER_SCAN_SIMD
#endif

#ifdef GUM_HAVE_POINTER_SCAN_SIMD
# if defined (HAVE_I386)
typedef __m128i GumScanVec;
#  define GUM_SCAN_VEC_SET1(value) _mm_set1_epi64x (value)
#  define GUM_SCAN_VEC_LOAD(p) _mm_loadu_si128 ((const __m128i *) (p))
#  define GUM_SCAN_VEC_AND(a, b) _mm_and_si128 (a, b)
#  define GUM_SCAN_VEC_OR(a, b) _mm_or_si128 (a, b)
# elif defined (HAVE_ARM64)
typedef uint64x2_t GumScanVec;
#  define GUM_SCAN_VEC_SET1(value) vdupq_n_u64 (value)
#  define GUM_SCAN_VEC_LOAD(p) vld1q_u64 ((const guint64 *) (p))
#  define GUM_SCAN_VEC_AND(a, b) vandq_u64 (a, b)
#  define GUM_SCAN_VEC_OR(a, b) vorrq_u64 (a, b)
# endif
#endif

#define GUM_POINTER_SCAN_TILE_WORDS \
    ((4 * 1024 * 1024) / sizeof (gpointer))
#define GUM_POINTER_SCAN_INLINE_LIMIT GUM_POINTER_SCAN_TILE_WORDS
#define GUM_POINTER_SCAN_MAX_WORKERS 4

typedef struct _GumPatchCodeContext GumPatchCodeContext;
typedef struct _GumPageLump GumPageLump;
typedef struct _GumSuspendOperation GumSuspendOperation;
typedef struct _GumPointerScan GumPointerScan;
typedef struct _GumPointerScanTile GumPointerScanTile;
typedef struct _GumPointerScanTask GumPointerScanTask;


struct _GumPatchCodeContext
{
  gsize page_offset;
  GumMemoryPatchApplyFunc func;
  gpointer user_data;
};

struct _GumPageLump
{
  gpointer start;
  gpointer end;
  gpointer writable_start;
  guint n_pages;
};

struct _GumSuspendOperation
{
  GumThreadId current_thread_id;
  GumMetalArray suspended_threads;
};

struct _GumPointerScan
{
  const gsize * values;
  guint n_values;
  gsize mask;
  GArray * tiles;
};

struct _GumPointerScanTile
{
  const gsize * words;
  gsize n_words;
};

struct _GumPointerScanTask
{
  GumPointerScan * scan;
  const GumPointerScanTile * tile;
  GArray * matches;
};

static void gum_apply_patch_code (gpointer mem, gpointer target_page,
    guint n_pages, gpointer user_data);
static gboolean gum_maybe_suspend_thread (const GumThreadDetails * details,
    gpointer user_data);


static guint gum_heap_ref_count = 0;
#ifndef GUM_USE_SYSTEM_ALLOC
static mspace gum_mspace_main = NULL;
static mspace gum_mspace_internal = NULL;
#endif
static guint gum_cached_page_size;

#ifdef HAVE_ANDROID
G_LOCK_DEFINE_STATIC (gum_softened_code_pages);
static GHashTable * gum_softened_code_pages;
#endif


void
gum_internal_heap_ref (void)
{
  if (gum_heap_ref_count++ > 0)
    return;

  _gum_memory_backend_init ();

  gum_cached_page_size = _gum_memory_backend_query_page_size ();

  _gum_cloak_init ();

#ifndef GUM_USE_SYSTEM_ALLOC
  gum_mspace_main = create_mspace (0, TRUE);
  gum_mspace_internal = create_mspace (0, TRUE);
#endif
}

void
gum_internal_heap_unref (void)
{
  g_assert (gum_heap_ref_count != 0);
  if (--gum_heap_ref_count > 0)
    return;

#ifndef GUM_USE_SYSTEM_ALLOC
  destroy_mspace (gum_mspace_internal);
  gum_mspace_internal = NULL;

  destroy_mspace (gum_mspace_main);
  gum_mspace_main = NULL;

  (void) DESTROY_LOCK (&malloc_global_mutex);
#endif

  _gum_cloak_deinit ();

  _gum_memory_backend_deinit ();
}

gpointer
gum_sign_code_pointer (gpointer value)
{
#ifdef HAVE_PTRAUTH
  return ptrauth_sign_unauthenticated (value, ptrauth_key_asia, 0);
#else
  return value;
#endif
}

gpointer
gum_strip_code_pointer (gpointer value)
{
#ifdef HAVE_PTRAUTH
  return ptrauth_strip (value, ptrauth_key_asia);
#else
  return value;
#endif
}

GumAddress
gum_sign_code_address (GumAddress value)
{
#ifdef HAVE_PTRAUTH
  return GPOINTER_TO_SIZE (ptrauth_sign_unauthenticated (
      GSIZE_TO_POINTER (value), ptrauth_key_asia, 0));
#else
  return value;
#endif
}

GumAddress
gum_strip_code_address (GumAddress value)
{
#ifdef HAVE_PTRAUTH
  return GPOINTER_TO_SIZE (ptrauth_strip (
      GSIZE_TO_POINTER (value), ptrauth_key_asia));
#else
  return value;
#endif
}

GumPtrauthSupport
gum_query_ptrauth_support (void)
{
#ifdef HAVE_PTRAUTH
  return GUM_PTRAUTH_SUPPORTED;
#else
  return GUM_PTRAUTH_UNSUPPORTED;
#endif
}

guint
gum_query_page_size (void)
{
  return gum_cached_page_size;
}

gboolean
gum_query_is_rwx_supported (void)
{
  return gum_query_rwx_support () == GUM_RWX_FULL;
}

#ifdef G_OS_NONE
G_GNUC_WEAK
#endif
GumRwxSupport
gum_query_rwx_support (void)
{
#if defined (HAVE_DARWIN) && !defined (HAVE_I386)
  return GUM_RWX_NONE;
#else
  return GUM_RWX_FULL;
#endif
}

/**
 * gum_memory_patch_code:
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
 * may result in the process losing its CS_VALID status).
 *
 * Returns: whether the modifications were successfully applied
 */
gboolean
gum_memory_patch_code (gpointer address,
                       gsize size,
                       GumMemoryPatchApplyFunc apply,
                       gpointer apply_data)
{
  gboolean result;
  gsize page_size;
  guint8 * start_page, * end_page;
  gsize page_offset;
  GPtrArray * page_addresses;
  GumPatchCodeContext context;

  address = gum_strip_code_pointer (address);

  page_size = gum_query_page_size ();
  start_page = GSIZE_TO_POINTER (GPOINTER_TO_SIZE (address) & ~(page_size - 1));
  end_page = GSIZE_TO_POINTER (
      (GPOINTER_TO_SIZE (address) + size - 1) & ~(page_size - 1));
  page_offset = ((guint8 *) address) - start_page;

  page_addresses =
      g_ptr_array_sized_new (((end_page - start_page) / page_size) + 1);

  g_ptr_array_add (page_addresses, start_page);

  if (end_page != start_page)
  {
    guint8 * cur;

    for (cur = start_page + page_size;
        cur != end_page + page_size;
        cur += page_size)
    {
      g_ptr_array_add (page_addresses, cur);
    }
  }

  context.page_offset = page_offset;
  context.func = apply;
  context.user_data = apply_data;

  result = gum_memory_patch_code_pages (page_addresses, TRUE,
      gum_apply_patch_code, &context);

  g_ptr_array_unref (page_addresses);

  return result;
}

static void
gum_apply_patch_code (gpointer mem,
                      gpointer target_page,
                      guint n_pages,
                      gpointer user_data)
{
  GumPatchCodeContext * context = user_data;

  context->func ((guint8 *) mem + context->page_offset, context->user_data);
}

/**
 * gum_memory_patch_code_pages: (skip)
 *
 * Safely modifies code pages at the given addresses.
 */
gboolean
gum_memory_patch_code_pages (GPtrArray * sorted_addresses,
                             gboolean coalesce,
                             GumMemoryPatchPagesApplyFunc apply,
                             gpointer apply_data)
{
  gboolean result = TRUE;
  gsize page_size;
  guint i;
  guint8 * apply_start, * apply_target_start;
  guint apply_num_pages;
  gboolean rwx_supported;

  rwx_supported = gum_query_is_rwx_supported ();
  page_size = gum_query_page_size ();

  if (gum_memory_can_remap_writable ())
  {
    GArray * plumps;
    GumPageLump * last;

#ifdef HAVE_DARWIN
    if (gum_darwin_is_debugger_mapping_enforced ())
    {
      GumPagePlanBuilder plan;
      gboolean success;

      _gum_page_plan_builder_init (&plan);

      for (i = 0; i != sorted_addresses->len; i++)
      {
        gpointer target_page = g_ptr_array_index (sorted_addresses, i);

        _gum_page_plan_builder_add_page (&plan, target_page);
      }

      success = _gum_page_plan_builder_post (&plan);

      _gum_page_plan_builder_free (&plan);

      if (!success)
        return FALSE;
    }
#endif

    plumps = g_array_new (FALSE, FALSE, sizeof (GumPageLump));
    last = NULL;

    for (i = 0; i != sorted_addresses->len; i++)
    {
      guint8 * target_page = g_ptr_array_index (sorted_addresses, i);

      last = (plumps->len != 0)
          ? &g_array_index (plumps, GumPageLump, plumps->len - 1)
          : NULL;

      if (last == NULL || last->end != target_page)
      {
        GumPageLump lump;

        if (last != NULL)
        {
          gpointer writable;

          writable = gum_memory_try_remap_writable_pages (last->start,
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

        g_array_append_val (plumps, lump);
      }

      last = &g_array_index (plumps, GumPageLump, plumps->len - 1);
      last->end = target_page + page_size;
      last->n_pages++;
    }

    if (plumps->len == 0)
      goto cleanup;

    last->writable_start =
        gum_memory_try_remap_writable_pages (last->start, last->n_pages);
    if (last->writable_start == NULL)
    {
      result = FALSE;
      goto cleanup;
    }

    if (coalesce)
    {
      for (i = 0; i != plumps->len; i++)
      {
        const GumPageLump * plump = &g_array_index (plumps, GumPageLump, i);

        apply (plump->writable_start, plump->start, plump->n_pages, apply_data);
      }
    }
    else
    {
      guint plump_index = 0;

      for (i = 0; i != sorted_addresses->len; i++)
      {
        guint8 * target_page;
        const GumPageLump * plump;
        gsize offset;

        target_page = g_ptr_array_index (sorted_addresses, i);

        plump = &g_array_index (plumps, GumPageLump, plump_index);

        if (target_page >= (guint8 *) plump->end)
        {
          plump_index++;
          g_assert (plump_index != plumps->len);
          plump = &g_array_index (plumps, GumPageLump, plump_index);
        }

        g_assert (target_page >= (guint8 *) plump->start);
        g_assert (target_page < (guint8 *) plump->end);
        offset = target_page - (guint8 *) plump->start;

        apply ((guint8 *) plump->writable_start + offset, target_page, 1,
            apply_data);
      }
    }

    for (i = 0; i != sorted_addresses->len; i++)
    {
      gpointer target_page = g_ptr_array_index (sorted_addresses, i);

      gum_clear_cache (target_page, page_size);
    }

cleanup:
    for (i = 0; i != plumps->len; i++)
    {
      const GumPageLump * plump = &g_array_index (plumps, GumPageLump, i);

      if (plump->writable_start != NULL)
      {
        gum_memory_dispose_writable_pages (plump->writable_start,
            plump->n_pages);
      }
    }

    g_array_unref (plumps);
  }
  else if (rwx_supported || !gum_code_segment_is_supported ())
  {
    GumPageProtection protection;
    GumPageProtection * original_protections;
    GumSuspendOperation suspend_op = { 0, };

    protection = rwx_supported ? GUM_PAGE_RWX : GUM_PAGE_RW;

    original_protections = g_newa (GumPageProtection, sorted_addresses->len);

    if (rwx_supported)
    {
#ifdef HAVE_LINUX
      _gum_memory_query_protections (sorted_addresses, original_protections);
#else
      for (i = 0; i != sorted_addresses->len; i++)
      {
        gpointer target_page = g_ptr_array_index (sorted_addresses, i);

        if (!gum_memory_query_protection (target_page,
            &original_protections[i]))
          original_protections[i] = GUM_PAGE_RX;
      }
#endif
    }
    else
    {
      gum_metal_array_init (&suspend_op.suspended_threads,
          sizeof (GumThreadId));

      suspend_op.current_thread_id = gum_process_get_current_thread_id ();
      _gum_process_enumerate_threads (gum_maybe_suspend_thread, &suspend_op,
          GUM_THREAD_FLAGS_NONE);
    }

    for (i = 0; i != sorted_addresses->len; i++)
    {
      gpointer target_page = g_ptr_array_index (sorted_addresses, i);

      if (!gum_try_mprotect (target_page, page_size, protection))
      {
        result = FALSE;
        goto resume_threads;
      }
    }

    apply_start = NULL;
    apply_num_pages = 0;
    for (i = 0; i != sorted_addresses->len; i++)
    {
      gpointer target_page = g_ptr_array_index (sorted_addresses, i);

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
      gpointer target_page = g_ptr_array_index (sorted_addresses, i);
      GumPageProtection restored;

      restored = (rwx_supported &&
          (original_protections[i] & GUM_PAGE_WRITE) != 0)
          ? GUM_PAGE_RWX
          : GUM_PAGE_RX;

      if (!gum_try_mprotect (target_page, page_size, restored))
      {
        result = FALSE;
        goto resume_threads;
      }
    }

    for (i = 0; i != sorted_addresses->len; i++)
    {
      gpointer target_page = g_ptr_array_index (sorted_addresses, i);

      gum_clear_cache (target_page, page_size);
    }

resume_threads:
    if (!rwx_supported)
    {
      guint num_suspended, i;

      num_suspended = suspend_op.suspended_threads.length;

      for (i = 0; i != num_suspended; i++)
      {
        GumThreadId * raw_id = gum_metal_array_element_at (
            &suspend_op.suspended_threads, i);

        gum_thread_resume (*raw_id, NULL);
#ifdef HAVE_DARWIN
        mach_port_mod_refs (mach_task_self (), *raw_id,
            MACH_PORT_RIGHT_SEND, -1);
#endif
      }

      gum_metal_array_free (&suspend_op.suspended_threads);
    }
  }
  else
  {
    GumCodeSegment * segment;
    guint8 * source_page, * current_page;
    gsize source_offset;

    segment = gum_code_segment_new (sorted_addresses->len * page_size, NULL);

    source_page = gum_code_segment_get_address (segment);

    current_page = source_page;
    for (i = 0; i != sorted_addresses->len; i++)
    {
      guint8 * target_page = g_ptr_array_index (sorted_addresses, i);

      memcpy (current_page, target_page, page_size);

      current_page += page_size;
    }

    apply_start = NULL;
    apply_num_pages = 0;
    for (i = 0; i != sorted_addresses->len; i++)
    {
      guint8 * target_page = g_ptr_array_index (sorted_addresses, i);

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

    gum_code_segment_realize (segment);

    source_offset = 0;
    for (i = 0; i != sorted_addresses->len; i++)
    {
      gpointer target_page = g_ptr_array_index (sorted_addresses, i);

      gum_code_segment_map (segment, source_offset, page_size, target_page);

      gum_clear_cache (target_page, page_size);

      source_offset += page_size;
    }

    gum_code_segment_free (segment);
  }

  return result;
}

static gboolean
gum_maybe_suspend_thread (const GumThreadDetails * details,
                          gpointer user_data)
{
  GumSuspendOperation * op = user_data;
  GumThreadId * suspended_id;

  if (details->id == op->current_thread_id)
    goto skip;

  if (!gum_thread_suspend (details->id, NULL))
    goto skip;

#ifdef HAVE_DARWIN
  mach_port_mod_refs (mach_task_self (), details->id, MACH_PORT_RIGHT_SEND, 1);
#endif
  suspended_id = gum_metal_array_append (&op->suspended_threads);
  *suspended_id = details->id;

skip:
  return TRUE;
}

gboolean
gum_memory_mark_code (gpointer address,
                      gsize size)
{
  gboolean success;

  if (gum_code_segment_is_supported ())
  {
    gsize page_size;
    guint8 * start_page, * end_page;

    page_size = gum_query_page_size ();
    start_page =
        GSIZE_TO_POINTER (GPOINTER_TO_SIZE (address) & ~(page_size - 1));
    end_page = GSIZE_TO_POINTER (
        (GPOINTER_TO_SIZE (address) + size - 1) & ~(page_size - 1));

    success = gum_code_segment_mark (start_page,
        end_page - start_page + page_size, NULL);
  }
  else
  {
    success = gum_try_mprotect (address, size, GUM_PAGE_RX);
  }

  gum_clear_cache (address, size);

  return success;
}

/**
 * gum_memory_scan:
 * @range: the #GumMemoryRange to scan
 * @pattern: the #GumMatchPattern to look for occurrences of
 * @func: (scope call): function to process each match
 * @user_data: data to pass to @func
 *
 * Scans @range for occurrences of @pattern, calling @func with each match.
 */
void
gum_ensure_code_readable (gconstpointer address,
                          gsize size)
{
  /*
   * We will make this more generic once it's needed on other OSes.
   */
#ifdef HAVE_ANDROID
  gsize page_size;
  gconstpointer start_page, end_page, cur_page;

  if (gum_android_get_api_level () < 29)
    return;

  page_size = gum_query_page_size ();
  start_page = GSIZE_TO_POINTER (
      GPOINTER_TO_SIZE (address) & ~(page_size - 1));
  end_page = GSIZE_TO_POINTER (
      GPOINTER_TO_SIZE (address + size - 1) & ~(page_size - 1)) + page_size;

  G_LOCK (gum_softened_code_pages);

  if (gum_softened_code_pages == NULL)
    gum_softened_code_pages = g_hash_table_new (NULL, NULL);

  for (cur_page = start_page; cur_page != end_page; cur_page += page_size)
  {
    GumPageProtection prot;

    if (g_hash_table_contains (gum_softened_code_pages, cur_page))
      continue;

    if (!gum_memory_query_protection (cur_page, &prot))
      continue;

    if ((prot & GUM_PAGE_READ) != 0)
    {
      g_hash_table_add (gum_softened_code_pages, (gpointer) cur_page);
      continue;
    }

    if (gum_try_mprotect ((gpointer) cur_page, page_size,
        prot | GUM_PAGE_READ))
      g_hash_table_add (gum_softened_code_pages, (gpointer) cur_page);
  }

  G_UNLOCK (gum_softened_code_pages);
#endif
}

void
gum_mprotect (gpointer address,
              gsize size,
              GumPageProtection prot)
{
  gboolean success;

  success = gum_try_mprotect (address, size, prot);
  if (!success)
    g_abort ();
}

#ifndef GUM_USE_SYSTEM_ALLOC

guint
gum_peek_private_memory_usage (void)
{
  guint total = 0;
  struct mallinfo info;

  info = mspace_mallinfo (gum_mspace_main);
  total += (guint) info.uordblks;

  info = mspace_mallinfo (gum_mspace_internal);
  total += (guint) info.uordblks;

  return total;
}

gpointer
gum_malloc (gsize size)
{
  return mspace_malloc (gum_mspace_main, size);
}

gpointer
gum_malloc0 (gsize size)
{
  return mspace_calloc (gum_mspace_main, 1, size);
}

gsize
gum_malloc_usable_size (gconstpointer mem)
{
  return mspace_usable_size (mem);
}

gpointer
gum_calloc (gsize count,
            gsize size)
{
  return mspace_calloc (gum_mspace_main, count, size);
}

gpointer
gum_realloc (gpointer mem,
             gsize size)
{
  return mspace_realloc (gum_mspace_main, mem, size);
}

gpointer
gum_memalign (gsize alignment,
              gsize size)
{
  return mspace_memalign (gum_mspace_main, alignment, size);
}

gpointer
gum_memdup (gconstpointer mem,
            gsize byte_size)
{
  gpointer result;

  result = mspace_malloc (gum_mspace_main, byte_size);
  memcpy (result, mem, byte_size);

  return result;
}

void
gum_free (gpointer mem)
{
  mspace_free (gum_mspace_main, mem);
}

gpointer
gum_internal_malloc (size_t size)
{
  return mspace_malloc (gum_mspace_internal, size);
}

gpointer
gum_internal_calloc (size_t count,
                     size_t size)
{
  return mspace_calloc (gum_mspace_internal, count, size);
}

gpointer
gum_internal_realloc (gpointer mem,
                      size_t size)
{
  return mspace_realloc (gum_mspace_internal, mem, size);
}

void
gum_internal_free (gpointer mem)
{
  mspace_free (gum_mspace_internal, mem);
}

#else

guint
gum_peek_private_memory_usage (void)
{
  return 0;
}

gpointer
gum_malloc (gsize size)
{
  return malloc (size);
}

gpointer
gum_malloc0 (gsize size)
{
  return calloc (1, size);
}

gsize
gum_malloc_usable_size (gconstpointer mem)
{
  return 0;
}

gpointer
gum_calloc (gsize count,
            gsize size)
{
  return calloc (count, size);
}

gpointer
gum_realloc (gpointer mem,
             gsize size)
{
  return realloc (mem, size);
}

gpointer
gum_memalign (gsize alignment,
              gsize size)
{
  /* TODO: Implement this. */
  g_assert_not_reached ();

  return NULL;
}

gpointer
gum_memdup (gconstpointer mem,
            gsize byte_size)
{
  gpointer result;

  result = malloc (byte_size);
  memcpy (result, mem, byte_size);

  return result;
}

void
gum_free (gpointer mem)
{
  free (mem);
}

gpointer
gum_internal_malloc (size_t size)
{
  return gum_malloc (size);
}

gpointer
gum_internal_calloc (size_t count,
                     size_t size)
{
  return gum_calloc (count, size);
}

gpointer
gum_internal_realloc (gpointer mem,
                      size_t size)
{
  return gum_realloc (mem, size);
}

void
gum_internal_free (gpointer mem)
{
  gum_free (mem);
}

#endif

gpointer
gum_alloc_n_pages (guint n_pages,
                   GumPageProtection prot)
{
  gpointer result;

  result = gum_try_alloc_n_pages (n_pages, prot);
  g_assert (result != NULL);

  return result;
}

gpointer
gum_alloc_n_pages_near (guint n_pages,
                        GumPageProtection prot,
                        const GumAddressSpec * spec)
{
  gpointer result;

  result = gum_try_alloc_n_pages_near (n_pages, prot, spec);
  g_assert (result != NULL);

  return result;
}

gboolean
gum_address_spec_is_satisfied_by (const GumAddressSpec * spec,
                                  gconstpointer address)
{
  gsize distance;

  distance =
      ABS ((const guint8 *) spec->near_address - (const guint8 *) address);

  return distance <= spec->max_distance;
}

GumMemoryRange *
gum_memory_range_copy (const GumMemoryRange * range)
{
  return g_slice_dup (GumMemoryRange, range);
}

void
gum_memory_range_free (GumMemoryRange * range)
{
  g_slice_free (GumMemoryRange, range);
}
