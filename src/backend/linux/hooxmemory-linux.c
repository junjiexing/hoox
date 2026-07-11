/*
 * Copyright (C) 2008-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxmemory.h"

#include "hooxmemory-priv.h"
#include "hooxlinux-priv.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

static hx_boolean hoox_memory_get_protection (hx_constpointer address, hx_size n,
    hx_size * size, HooxPageProtection * prot);
static HooxPageProtection hoox_page_protection_from_perms (const hx_char * perms);

hx_boolean
hoox_memory_query_protection (hx_constpointer address,
                             HooxPageProtection * prot)
{
  hx_size size;

  if (!hoox_memory_get_protection (address, 1, &size, prot))
    return FALSE;

  return size >= 1;
}

void
_hoox_memory_query_protections (HxPtrArray * sorted_pages,
                               HooxPageProtection * protections)
{
  hx_uint i;
  HooxProcMapsIter iter;
  const hx_char * line;

  for (i = 0; i != sorted_pages->len; i++)
    protections[i] = HOOX_PAGE_RX;

  _hoox_proc_maps_iter_init_for_self (&iter);

  i = 0;
  while (i != sorted_pages->len && _hoox_proc_maps_iter_next (&iter, &line))
  {
    unsigned long long start_ull = 0, end_ull = 0;
    hx_uint8 * start, * end;
    hx_char perms[5] = { 0, };
    HooxPageProtection prot;

    sscanf (line, "%llx-%llx %4c ", &start_ull, &end_ull, perms);
    start = (hx_uint8 *) (hx_uintptr) start_ull;
    end = (hx_uint8 *) (hx_uintptr) end_ull;

    while (i != sorted_pages->len &&
        (hx_uint8 *) hx_ptr_array_index (sorted_pages, i) < start)
    {
      i++;
    }

    prot = hoox_page_protection_from_perms (perms);

    while (i != sorted_pages->len &&
        (hx_uint8 *) hx_ptr_array_index (sorted_pages, i) < end)
    {
      protections[i] = prot;
      i++;
    }
  }

  _hoox_proc_maps_iter_destroy (&iter);
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

static hx_boolean
hoox_memory_get_protection (hx_constpointer address,
                           hx_size n,
                           hx_size * size,
                           HooxPageProtection * prot)
{
  hx_boolean success;
  HooxProcMapsIter iter;
  const hx_char * line;

  if (size == NULL || prot == NULL)
  {
    hx_size ignored_size;
    HooxPageProtection ignored_prot;

    return hoox_memory_get_protection (address, n,
        (size != NULL) ? size : &ignored_size,
        (prot != NULL) ? prot : &ignored_prot);
  }

  if (n > 1)
  {
    hx_size page_size, start_page, end_page, cur_page;

    page_size = hoox_query_page_size ();

    start_page = HX_POINTER_TO_SIZE (address) & ~(page_size - 1);
    end_page = (HX_POINTER_TO_SIZE (address) + n - 1) & ~(page_size - 1);

    success = hoox_memory_get_protection (HX_SIZE_TO_POINTER (start_page), 1,
        NULL, prot);
    if (success)
    {
      *size = page_size - (HX_POINTER_TO_SIZE (address) - start_page);
      for (cur_page = start_page + page_size;
          cur_page != end_page + page_size;
          cur_page += page_size)
      {
        HooxPageProtection cur_prot;

        if (hoox_memory_get_protection (HX_SIZE_TO_POINTER (cur_page), 1, NULL,
            &cur_prot) && (cur_prot != HOOX_PAGE_NO_ACCESS ||
            *prot == HOOX_PAGE_NO_ACCESS))
        {
          *size += page_size;
          *prot &= cur_prot;
        }
        else
        {
          break;
        }
      }
      *size = MIN (*size, n);
    }

    return success;
  }

  success = FALSE;
  *size = 0;
  *prot = HOOX_PAGE_NO_ACCESS;

  _hoox_proc_maps_iter_init_for_self (&iter);

  while (_hoox_proc_maps_iter_next (&iter, &line))
  {
    unsigned long long start_ull = 0, end_ull = 0;
    hx_constpointer start, end;
    hx_char perms[5] = { 0, };

    sscanf (line, "%llx-%llx %4c ", &start_ull, &end_ull, perms);
    start = (hx_constpointer) (hx_uintptr) start_ull;
    end = (hx_constpointer) (hx_uintptr) end_ull;

    if (start > address)
      break;
    else if (address >= start &&
        (const hx_uint8 *) address + n - 1 < (const hx_uint8 *) end)
    {
      success = TRUE;
      *size = 1;
      *prot = hoox_page_protection_from_perms (perms);
      break;
    }
  }

  _hoox_proc_maps_iter_destroy (&iter);

  return success;
}

static HooxPageProtection
hoox_page_protection_from_perms (const hx_char * perms)
{
  HooxPageProtection prot = HOOX_PAGE_NO_ACCESS;

  if (perms[0] == 'r')
    prot |= HOOX_PAGE_READ;
  if (perms[1] == 'w')
    prot |= HOOX_PAGE_WRITE;
  if (perms[2] == 'x')
    prot |= HOOX_PAGE_EXECUTE;

  return prot;
}
