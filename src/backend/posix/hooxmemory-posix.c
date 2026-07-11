/*
 * Copyright (C) 2008-2024 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxmemory.h"

#include "hooxmemory-priv.h"
#include "hooxprocess-priv.h"

#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

typedef struct _HooxAllocNearContext HooxAllocNearContext;
typedef struct _HooxEnumerateFreeRangesContext HooxEnumerateFreeRangesContext;

struct _HooxAllocNearContext
{
  const HooxAddressSpec * spec;
  hx_size size;
  hx_size alignment;
  hx_size page_size;
  HooxPageProtection prot;

  hx_pointer result;
};

struct _HooxEnumerateFreeRangesContext
{
  HooxFoundRangeFunc func;
  hx_pointer user_data;
  HooxAddress prev_end;
};

static hx_pointer hoox_memory_allocate_internal (hx_pointer address,
    hx_size size, hx_size alignment, HooxPageProtection prot, hx_int extra_flags);
static hx_boolean hoox_try_alloc_in_range_if_near_enough (
    const HooxRangeDetails * details, hx_pointer user_data);
static hx_boolean hoox_try_suggest_allocation_base (const HooxMemoryRange * range,
    const HooxAllocNearContext * ctx, hx_pointer * allocation_base);
static hx_pointer hoox_allocate_page_aligned (hx_pointer address, hx_size size,
    hx_int prot, hx_int extra_flags);
static void hoox_enumerate_free_ranges (HooxFoundRangeFunc func,
    hx_pointer user_data);
static hx_boolean hoox_emit_free_range (const HooxRangeDetails * details,
    hx_pointer user_data);

void
_hoox_memory_backend_init (void)
{
}

void
_hoox_memory_backend_deinit (void)
{
}

hx_uint
_hoox_memory_backend_query_page_size (void)
{
  return (hx_uint) sysconf (_SC_PAGE_SIZE);
}

hx_pointer
hoox_try_alloc_n_pages (hx_uint n_pages,
                       HooxPageProtection prot)
{
  return hoox_try_alloc_n_pages_near (n_pages, prot, NULL);
}

hx_pointer
hoox_try_alloc_n_pages_near (hx_uint n_pages,
                            HooxPageProtection prot,
                            const HooxAddressSpec * spec)
{
  hx_uint8 * base;
  hx_size page_size, size;

  page_size = hoox_query_page_size ();
  size = (1 + n_pages) * page_size;

  base = hoox_memory_allocate_near (spec, size, page_size, prot);
  if (base == NULL)
    return NULL;

  if ((prot & HOOX_PAGE_WRITE) == 0)
    hoox_mprotect (base, page_size, HOOX_PAGE_RW);

  *((hx_size *) base) = size;

  hoox_mprotect (base, page_size, HOOX_PAGE_READ);

  return base + page_size;
}

void
hoox_query_page_allocation_range (hx_constpointer mem,
                                 hx_uint size,
                                 HooxMemoryRange * range)
{
  hx_size page_size = hoox_query_page_size ();

  range->base_address = HOOX_ADDRESS ((const hx_uint8 *) mem - page_size);
  range->size = size + page_size;
}

void
hoox_free_pages (hx_pointer mem)
{
  hx_uint8 * start;
  hx_size size;

  start = (hx_uint8 *) mem - hoox_query_page_size ();
  size = *((hx_size *) start);

  hoox_memory_release (start, size);
}

hx_pointer
hoox_memory_allocate (hx_pointer address,
                     hx_size size,
                     hx_size alignment,
                     HooxPageProtection prot)
{
  return hoox_memory_allocate_internal (address, size, alignment, prot, 0);
}

static hx_pointer
hoox_memory_allocate_internal (hx_pointer address,
                              hx_size size,
                              hx_size alignment,
                              HooxPageProtection prot,
                              hx_int extra_flags)
{
  hx_size page_size, allocation_size;
  hx_uint8 * base, * aligned_base;

  address = HOOX_ALIGN_POINTER (hx_pointer, address, alignment);

  page_size = hoox_query_page_size ();
  allocation_size = size + (alignment - page_size);
  allocation_size = HOOX_ALIGN_SIZE (allocation_size, page_size);

  base = hoox_allocate_page_aligned (address, allocation_size,
      _hoox_page_protection_to_posix (prot), extra_flags);
  if (base == NULL)
    return NULL;

  aligned_base = HOOX_ALIGN_POINTER (hx_uint8 *, base, alignment);

  if (aligned_base != base)
  {
    hx_size prefix_size = aligned_base - base;
    hoox_memory_free (base, prefix_size);
    allocation_size -= prefix_size;
  }

  if (allocation_size != size)
  {
    hx_size suffix_size = allocation_size - size;
    hoox_memory_free (aligned_base + size, suffix_size);
    allocation_size -= suffix_size;
  }

  hx_assert (allocation_size == size);

  return aligned_base;
}

hx_pointer
hoox_memory_allocate_near (const HooxAddressSpec * spec,
                          hx_size size,
                          hx_size alignment,
                          HooxPageProtection prot)
{
  hx_pointer suggested_base, received_base;
  HooxAllocNearContext ctx;

  suggested_base = (spec != NULL) ? spec->near_address : NULL;

  received_base = hoox_memory_allocate (suggested_base, size, alignment, prot);
  if (received_base == NULL)
    return NULL;
  if (spec == NULL || hoox_address_spec_is_satisfied_by (spec, received_base))
    return received_base;
  hoox_memory_free (received_base, size);

  ctx.spec = spec;
  ctx.size = size;
  ctx.alignment = alignment;
  ctx.page_size = hoox_query_page_size ();
  ctx.prot = prot;
  ctx.result = NULL;

  hoox_enumerate_free_ranges (hoox_try_alloc_in_range_if_near_enough, &ctx);

  return ctx.result;
}

static hx_boolean
hoox_try_alloc_in_range_if_near_enough (const HooxRangeDetails * details,
                                       hx_pointer user_data)
{
  HooxAllocNearContext * ctx = user_data;
  hx_pointer suggested_base, received_base;

  if (!hoox_try_suggest_allocation_base (details->range, ctx, &suggested_base))
    goto keep_looking;

  received_base = hoox_memory_allocate (suggested_base, ctx->size,
      ctx->alignment, ctx->prot);
  if (received_base == NULL)
    goto keep_looking;

  if (!hoox_address_spec_is_satisfied_by (ctx->spec, received_base))
  {
    hoox_memory_free (received_base, ctx->size);
    goto keep_looking;
  }

  ctx->result = received_base;
  return FALSE;

keep_looking:
  return TRUE;
}

static hx_boolean
hoox_try_suggest_allocation_base (const HooxMemoryRange * range,
                                 const HooxAllocNearContext * ctx,
                                 hx_pointer * allocation_base)
{
  const hx_size allocation_size = ctx->size + (ctx->alignment - ctx->page_size);
  hx_pointer base;
  hx_size mask;

  if (range->size < allocation_size)
    return FALSE;

  mask = ~(ctx->alignment - 1);

  base = HX_SIZE_TO_POINTER ((range->base_address + ctx->alignment - 1) & mask);
  if (!hoox_address_spec_is_satisfied_by (ctx->spec, base))
  {
    base = HX_SIZE_TO_POINTER ((range->base_address + range->size -
        allocation_size) & mask);
    if (!hoox_address_spec_is_satisfied_by (ctx->spec, base))
      return FALSE;
  }

  *allocation_base = base;
  return TRUE;
}

static hx_pointer
hoox_allocate_page_aligned (hx_pointer address,
                           hx_size size,
                           hx_int prot,
                           hx_int extra_flags)
{
  hx_pointer result;
  const hx_int base_flags = MAP_PRIVATE | MAP_ANONYMOUS | extra_flags;

  result = mmap (address, size, prot, base_flags, -1, 0);

  return (result != MAP_FAILED) ? result : NULL;
}

hx_boolean
hoox_memory_free (hx_pointer address,
                 hx_size size)
{
  return munmap (address, size) == 0;
}

hx_boolean
hoox_memory_release (hx_pointer address,
                    hx_size size)
{
  return hoox_memory_free (address, size);
}

hx_boolean
hoox_memory_recommit (hx_pointer address,
                     hx_size size,
                     HooxPageProtection prot)
{
  hoox_try_mprotect (address, size, prot);

  return TRUE;
}

hx_int
_hoox_page_protection_to_posix (HooxPageProtection prot)
{
  hx_int posix_prot = PROT_NONE;

  if ((prot & HOOX_PAGE_READ) != 0)
    posix_prot |= PROT_READ;
  if ((prot & HOOX_PAGE_WRITE) != 0)
    posix_prot |= PROT_WRITE;
  if ((prot & HOOX_PAGE_EXECUTE) != 0)
    posix_prot |= PROT_EXEC;

  return posix_prot;
}

static void
hoox_enumerate_free_ranges (HooxFoundRangeFunc func,
                           hx_pointer user_data)
{
  HooxEnumerateFreeRangesContext ctx = { func, user_data, 0 };

  _hoox_process_enumerate_ranges (HOOX_PAGE_NO_ACCESS, hoox_emit_free_range,
      &ctx);
}

static hx_boolean
hoox_emit_free_range (const HooxRangeDetails * details,
                     hx_pointer user_data)
{
  HooxEnumerateFreeRangesContext * ctx = user_data;
  const HooxMemoryRange * range = details->range;
  HooxAddress start = range->base_address;
  HooxAddress end = start + range->size;
  hx_boolean carry_on = TRUE;

  if (ctx->prev_end != 0)
  {
    HooxAddress gap_size;

    gap_size = start - ctx->prev_end;

    if (gap_size > 0)
    {
      HooxRangeDetails d;
      HooxMemoryRange r;

      d.range = &r;
      d.protection = HOOX_PAGE_NO_ACCESS;
      d.file = NULL;

      r.base_address = ctx->prev_end;
      r.size = gap_size;

      carry_on = ctx->func (&d, ctx->user_data);
    }
  }

  ctx->prev_end = end;

  return carry_on;
}
