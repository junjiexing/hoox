/*
 * Copyright (C) 2008-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2008 Christian Berentsen <jc.berentsen@gmail.com>
 * Copyright (C) 2025 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_MEMORY_H__
#define __HOOX_MEMORY_H__

#include "hooxdefs.h"

#define HOOX_TYPE_MATCH_PATTERN (hoox_match_pattern_get_type ())
#define HOOX_TYPE_MEMORY_RANGE (hoox_memory_range_get_type ())
#define HOOX_MEMORY_RANGE_INCLUDES(r, a) ((a) >= (r)->base_address && \
    (a) < ((r)->base_address + (r)->size))

#define HOOX_PAGE_RW ((HooxPageProtection) (HOOX_PAGE_READ | HOOX_PAGE_WRITE))
#define HOOX_PAGE_RX ((HooxPageProtection) (HOOX_PAGE_READ | HOOX_PAGE_EXECUTE))
#define HOOX_PAGE_RWX ((HooxPageProtection) (HOOX_PAGE_READ | HOOX_PAGE_WRITE | \
    HOOX_PAGE_EXECUTE))

HX_BEGIN_DECLS

typedef hx_uint HooxPtrauthSupport;
typedef hx_uint HooxRwxSupport;
typedef hx_uint HooxMemoryOperation;
typedef hx_uint HooxPageProtection;
typedef struct _HooxAddressSpec HooxAddressSpec;
typedef struct _HooxRangeDetails HooxRangeDetails;
typedef struct _HooxMemoryRange HooxMemoryRange;
typedef struct _HooxFileMapping HooxFileMapping;
typedef struct _HooxMatchPattern HooxMatchPattern;
typedef struct _HooxPointerMatch HooxPointerMatch;

typedef hx_boolean (* HooxMemoryIsNearFunc) (hx_pointer memory, hx_pointer address);

enum _HooxPtrauthSupport
{
  HOOX_PTRAUTH_INVALID,
  HOOX_PTRAUTH_UNSUPPORTED,
  HOOX_PTRAUTH_SUPPORTED
};

enum _HooxRwxSupport
{
  HOOX_RWX_NONE,
  HOOX_RWX_ALLOCATIONS_ONLY,
  HOOX_RWX_FULL
};

enum _HooxMemoryOperation
{
  HOOX_MEMOP_INVALID,
  HOOX_MEMOP_READ,
  HOOX_MEMOP_WRITE,
  HOOX_MEMOP_EXECUTE
};

enum _HooxPageProtection
{
  HOOX_PAGE_NO_ACCESS = 0,
  HOOX_PAGE_READ      = (1 << 0),
  HOOX_PAGE_WRITE     = (1 << 1),
  HOOX_PAGE_EXECUTE   = (1 << 2),
};

struct _HooxAddressSpec
{
  hx_pointer near_address;
  hx_size max_distance;
};

struct _HooxRangeDetails
{
  const HooxMemoryRange * range;
  HooxPageProtection protection;
  const HooxFileMapping * file;
};

struct _HooxMemoryRange
{
  HooxAddress base_address;
  hx_size size;
};

struct _HooxFileMapping
{
  const hx_char * path;
  hx_uint64 offset;
  hx_size size;
};

struct _HooxPointerMatch
{
  HooxAddress address;
  hx_size value;
};

typedef hx_boolean (* HooxFoundRangeFunc) (const HooxRangeDetails * details,
    hx_pointer user_data);
typedef void (* HooxMemoryPatchApplyFunc) (hx_pointer mem, hx_pointer user_data);
typedef void (* HooxMemoryPatchPagesApplyFunc) (hx_pointer mem,
    hx_pointer target_page, hx_uint n_pages, hx_pointer user_data);
typedef hx_boolean (* HooxMemoryScanMatchFunc) (HooxAddress address, hx_size size,
    hx_pointer user_data);

HOOX_API void hoox_internal_heap_ref (void);
HOOX_API void hoox_internal_heap_unref (void);

HOOX_API hx_pointer hoox_sign_code_pointer (hx_pointer value);
HOOX_API hx_pointer hoox_strip_code_pointer (hx_pointer value);
HOOX_API HooxAddress hoox_sign_code_address (HooxAddress value);
HOOX_API HooxPtrauthSupport hoox_query_ptrauth_support (void);
HOOX_API hx_uint hoox_query_page_size (void);
HOOX_API hx_boolean hoox_query_is_rwx_supported (void);
HOOX_API HooxRwxSupport hoox_query_rwx_support (void);
HOOX_API hx_boolean hoox_memory_query_protection (hx_constpointer address,
    HooxPageProtection * prot);
HOOX_API hx_boolean hoox_memory_patch_code (hx_pointer address, hx_size size,
    HooxMemoryPatchApplyFunc apply, hx_pointer apply_data);
HOOX_API hx_boolean hoox_memory_patch_code_pages (HxPtrArray * sorted_addresses,
    hx_boolean coalesce, HooxMemoryPatchPagesApplyFunc apply,
    hx_pointer apply_data);
HOOX_API hx_boolean hoox_memory_can_remap_writable (void);
HOOX_API hx_pointer hoox_memory_try_remap_writable_pages (hx_pointer first_page,
    hx_uint n_pages);
HOOX_API void hoox_memory_dispose_writable_pages (hx_pointer first_page,
    hx_uint n_pages);
HOOX_API hx_boolean hoox_memory_mark_code (hx_pointer address, hx_size size);

HOOX_API void hoox_memory_scan (const HooxMemoryRange * range,
    const HooxMatchPattern * pattern, HooxMemoryScanMatchFunc func,
    hx_pointer user_data);
HOOX_API HxArray * hoox_memory_find_pointers (const HooxMemoryRange * ranges,
    hx_uint n_ranges, const hx_size * values, hx_uint n_values, hx_size mask);

HOOX_API HxType hoox_match_pattern_get_type (void) HX_GNUC_CONST;
HOOX_API HooxMatchPattern * hoox_match_pattern_new_from_string (
    const hx_char * pattern_str);
HOOX_API HooxMatchPattern * hoox_match_pattern_ref (HooxMatchPattern * pattern);
HOOX_API void hoox_match_pattern_unref (HooxMatchPattern * pattern);
HOOX_API hx_uint hoox_match_pattern_get_size (const HooxMatchPattern * pattern);
HOOX_API HxPtrArray * hoox_match_pattern_get_tokens (
    const HooxMatchPattern * pattern);

HOOX_API void hoox_ensure_code_readable (hx_constpointer address, hx_size size);

HOOX_API void hoox_mprotect (hx_pointer address, hx_size size,
    HooxPageProtection prot);
HOOX_API hx_boolean hoox_try_mprotect (hx_pointer address, hx_size size,
    HooxPageProtection prot);

HOOX_API void hoox_clear_cache (hx_pointer address, hx_size size);

#define hoox_new(struct_type, n_structs) \
    ((struct_type *) hoox_malloc (n_structs * sizeof (struct_type)))
#define hoox_new0(struct_type, n_structs) \
    ((struct_type *) hoox_malloc0 (n_structs * sizeof (struct_type)))

HOOX_API hx_uint hoox_peek_private_memory_usage (void);

HOOX_API hx_pointer hoox_malloc (hx_size size);
HOOX_API hx_pointer hoox_malloc0 (hx_size size);
HOOX_API hx_size hoox_malloc_usable_size (hx_constpointer mem);
HOOX_API hx_pointer hoox_calloc (hx_size count, hx_size size);
HOOX_API hx_pointer hoox_realloc (hx_pointer mem, hx_size size);
HOOX_API hx_pointer hoox_memalign (hx_size alignment, hx_size size);
HOOX_API hx_pointer hoox_memdup (hx_constpointer mem, hx_size byte_size);
HOOX_API void hoox_free (hx_pointer mem);

HOOX_API hx_pointer hoox_alloc_n_pages (hx_uint n_pages, HooxPageProtection prot);
HOOX_API hx_pointer hoox_try_alloc_n_pages (hx_uint n_pages, HooxPageProtection prot);
HOOX_API hx_pointer hoox_alloc_n_pages_near (hx_uint n_pages, HooxPageProtection prot,
    const HooxAddressSpec * spec);
HOOX_API hx_pointer hoox_try_alloc_n_pages_near (hx_uint n_pages,
    HooxPageProtection prot, const HooxAddressSpec * spec);
HOOX_API void hoox_query_page_allocation_range (hx_constpointer mem, hx_uint size,
    HooxMemoryRange * range);
HOOX_API void hoox_free_pages (hx_pointer mem);

HOOX_API hx_pointer hoox_memory_allocate (hx_pointer address, hx_size size,
    hx_size alignment, HooxPageProtection prot);
HOOX_API hx_pointer hoox_memory_allocate_near (const HooxAddressSpec * spec,
    hx_size size, hx_size alignment, HooxPageProtection prot);
HOOX_API hx_boolean hoox_memory_free (hx_pointer address, hx_size size);
HOOX_API hx_boolean hoox_memory_release (hx_pointer address, hx_size size);
HOOX_API hx_boolean hoox_memory_recommit (hx_pointer address, hx_size size,
    HooxPageProtection prot);

HOOX_API hx_boolean hoox_address_spec_is_satisfied_by (const HooxAddressSpec * spec,
    hx_constpointer address);

HOOX_API HxType hoox_memory_range_get_type (void) HX_GNUC_CONST;

HX_END_DECLS

#endif
