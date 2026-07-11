/*
 * Copyright (C) 2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2025 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxmemory.h"

#include "hooxmemory-priv.h"
#include "hooxprocess-priv.h"

#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

typedef struct _HooxFindRangeProtContext HooxFindRangeProtContext;

struct _HooxFindRangeProtContext
{
  HooxAddress address;

  hx_boolean found;
  HooxPageProtection protection;
};

static hx_boolean hoox_store_protection_if_containing_address (
    const HooxRangeDetails * details, hx_pointer user_data);

hx_boolean
hoox_memory_query_protection (hx_constpointer address,
                             HooxPageProtection * prot)
{
  HooxFindRangeProtContext ctx;

  ctx.address = HOOX_ADDRESS (address);
  ctx.found = FALSE;
  ctx.protection = HOOX_PAGE_NO_ACCESS;

  _hoox_process_enumerate_ranges (HOOX_PAGE_NO_ACCESS,
      hoox_store_protection_if_containing_address, &ctx);

  if (ctx.found)
    *prot = ctx.protection;

  return ctx.found;
}

static hx_boolean
hoox_store_protection_if_containing_address (const HooxRangeDetails * details,
                                            hx_pointer user_data)
{
  HooxFindRangeProtContext * ctx = user_data;
  hx_boolean proceed = TRUE;

  if (HOOX_MEMORY_RANGE_INCLUDES (details->range, ctx->address))
  {
    ctx->found = TRUE;
    ctx->protection = details->protection;

    proceed = FALSE;
  }

  return proceed;
}

hx_boolean
hoox_memory_can_remap_writable (void)
{
  return FALSE;
}

hx_pointer
hoox_memory_try_remap_writable_pages (hx_pointer first_page,
                                     hx_uint n_pages)
{
  (void) first_page;
  (void) n_pages;
  return NULL;
}

void
hoox_memory_dispose_writable_pages (hx_pointer first_page,
                                   hx_uint n_pages)
{
  (void) first_page;
  (void) n_pages;
}

hx_boolean
hoox_try_mprotect (hx_pointer address,
                  hx_size size,
                  HooxPageProtection prot)
{
  hx_size page_size;
  hx_pointer aligned_address;
  hx_size aligned_size;
  hx_int posix_prot;
  hx_int result;

  hx_assert (size != 0);

  page_size = hoox_query_page_size ();
  aligned_address = HX_SIZE_TO_POINTER (
      HX_POINTER_TO_SIZE (address) & ~(page_size - 1));
  aligned_size =
      (1 + (((hx_uint8 *) address + size - 1 - (hx_uint8 *) aligned_address) /
      page_size)) * page_size;
  posix_prot = _hoox_page_protection_to_posix (prot);

  result = mprotect (aligned_address, aligned_size, posix_prot);

  return result == 0;
}

void
hoox_clear_cache (hx_pointer address,
                 hx_size size)
{
#if defined (HAVE_ARM) || defined (HAVE_ARM64)
  __builtin___clear_cache ((char *) address, (char *) address + size);
#else
  (void) address;
  (void) size;
#endif
}
