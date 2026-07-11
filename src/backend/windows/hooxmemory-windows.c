/*
 * Copyright (C) 2008-2024 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2008 Christian Berentsen <jc.berentsen@gmail.com>
 * Copyright (C) 2025 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxmemory.h"

#include "hooxmemory-priv.h"
#include "hooxwindows.h"

#include <stdlib.h>

static hx_pointer hoox_virtual_alloc (hx_pointer address, hx_size size,
    DWORD allocation_type, DWORD page_protection);
static hx_boolean hoox_memory_get_protection (hx_constpointer address, hx_size len,
    HooxPageProtection * prot);

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
  SYSTEM_INFO si;

  GetSystemInfo (&si);

  return si.dwPageSize;
}

hx_boolean
hoox_memory_is_readable (hx_constpointer address,
                        hx_size len)
{
  HooxPageProtection prot;

  if (!hoox_memory_get_protection (address, len, &prot))
    return FALSE;

  return (prot & HOOX_PAGE_READ) != 0;
}

hx_boolean
hoox_memory_query_protection (hx_constpointer address,
                             HooxPageProtection * prot)
{
  return hoox_memory_get_protection (address, 1, prot);
}

hx_uint8 *
hoox_memory_read (hx_constpointer address,
                 hx_size len,
                 hx_size * n_bytes_read)
{
  hx_uint8 * result;
  hx_size offset;
  HANDLE self;
  hx_size page_size;

  result = hx_malloc (len);
  offset = 0;

  self = GetCurrentProcess ();
  page_size = hoox_query_page_size ();

  while (offset != len)
  {
    const hx_uint8 * chunk_address, * page_address;
    hx_size page_offset, chunk_size;
    SIZE_T n;
    BOOL success;

    chunk_address = (const hx_uint8 *) address + offset;
    page_address = HX_SIZE_TO_POINTER (
        HX_POINTER_TO_SIZE (chunk_address) & ~(page_size - 1));
    page_offset = chunk_address - page_address;
    chunk_size = MIN (len - offset, page_size - page_offset);

    success = ReadProcessMemory (self, chunk_address, result + offset,
        chunk_size, &n);
    if (!success)
      break;
    offset += n;
  }

  if (offset == 0)
  {
    hx_free (result);
    result = NULL;
  }

  if (n_bytes_read != NULL)
    *n_bytes_read = offset;

  return result;
}

hx_boolean
hoox_memory_write (hx_pointer address,
                  const hx_uint8 * bytes,
                  hx_size len)
{
  return WriteProcessMemory (GetCurrentProcess (), address, bytes, len, NULL);
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
  return NULL;
}

void
hoox_memory_dispose_writable_pages (hx_pointer first_page,
                                   hx_uint n_pages)
{
}

hx_boolean
hoox_try_mprotect (hx_pointer address,
                  hx_size size,
                  HooxPageProtection prot)
{
  DWORD win_prot, old_protect;

  win_prot = hoox_page_protection_to_windows (prot);

  return VirtualProtect (address, size, win_prot, &old_protect);
}

void
hoox_clear_cache (hx_pointer address,
                 hx_size size)
{
  FlushInstructionCache (GetCurrentProcess (), address, size);
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
  hx_pointer result;
  hx_size page_size, size;

  page_size = hoox_query_page_size ();
  size = n_pages * page_size;

  result = hoox_memory_allocate_near (spec, size, page_size, prot);
  if (result != NULL && prot == HOOX_PAGE_NO_ACCESS)
  {
    hoox_memory_recommit (result, size, prot);
  }

  return result;
}

void
hoox_query_page_allocation_range (hx_constpointer mem,
                                 hx_uint size,
                                 HooxMemoryRange * range)
{
  range->base_address = HOOX_ADDRESS (mem);
  range->size = size;
}

void
hoox_free_pages (hx_pointer mem)
{
  BOOL success HX_GNUC_UNUSED;

  success = VirtualFree (mem, 0, MEM_RELEASE);
  hx_assert (success);
}

hx_pointer
hoox_memory_allocate (hx_pointer address,
                     hx_size size,
                     hx_size alignment,
                     HooxPageProtection prot)
{
  DWORD allocation_type, win_prot;
  hx_pointer base, aligned_base;
  hx_size padded_size;
  hx_int retries = 3;

  allocation_type = (prot == HOOX_PAGE_NO_ACCESS)
      ? MEM_RESERVE
      : MEM_RESERVE | MEM_COMMIT;

  win_prot = hoox_page_protection_to_windows (prot);

  base = hoox_virtual_alloc (address, size, allocation_type, win_prot);
  if (base == NULL)
    return NULL;

  aligned_base = HOOX_ALIGN_POINTER (hx_pointer, base, alignment);
  if (aligned_base == base)
    return base;

  hoox_memory_free (base, size);
  base = NULL;
  aligned_base = NULL;
  address = NULL;

  padded_size = size + (alignment - hoox_query_page_size ());

  while (retries-- != 0)
  {
    base = hoox_virtual_alloc (address, padded_size, allocation_type, win_prot);
    if (base == NULL)
      return NULL;

    hoox_memory_free (base, padded_size);
    aligned_base = HOOX_ALIGN_POINTER (hx_pointer, base, alignment);
    base = VirtualAlloc (aligned_base, size, allocation_type, win_prot);
    if (base != NULL)
      break;
  }

  return base;
}

hx_pointer
hoox_memory_allocate_near (const HooxAddressSpec * spec,
                          hx_size size,
                          hx_size alignment,
                          HooxPageProtection prot)
{
  hx_pointer result = NULL;
  hx_size page_size, step_size;
  DWORD win_prot;
  hx_uint8 * low_address, * high_address;

  result = hoox_memory_allocate (NULL, size, alignment, prot);
  if (result == NULL)
    return NULL;
  if (spec == NULL || hoox_address_spec_is_satisfied_by (spec, result))
    return result;
  hoox_memory_free (result, size);

  page_size = hoox_query_page_size ();
  step_size = MAX (page_size, HOOX_ALIGN_SIZE (alignment, page_size));
  win_prot = hoox_page_protection_to_windows (prot);

  low_address = HX_SIZE_TO_POINTER (
      (HX_POINTER_TO_SIZE (spec->near_address) & ~(step_size - 1)));
  high_address = low_address;

  do
  {
    hx_size cur_distance;

    low_address -= step_size;
    high_address += step_size;
    cur_distance = (hx_size) high_address - (hx_size) spec->near_address;
    if (cur_distance > spec->max_distance)
      break;

    result = VirtualAlloc (low_address, size, MEM_COMMIT | MEM_RESERVE,
        win_prot);
    if (result == NULL)
    {
      result = VirtualAlloc (high_address, size, MEM_COMMIT | MEM_RESERVE,
          win_prot);
    }
  }
  while (result == NULL);

  return result;
}

static hx_pointer
hoox_virtual_alloc (hx_pointer address,
                   hx_size size,
                   DWORD allocation_type,
                   DWORD page_protection)
{
  hx_pointer result = NULL;

  if (address != NULL)
  {
    result = VirtualAlloc (address, size, allocation_type, page_protection);
  }

  if (result == NULL)
  {
    result = VirtualAlloc (NULL, size, allocation_type, page_protection);
  }

  return result;
}

hx_boolean
hoox_memory_free (hx_pointer address,
                 hx_size size)
{
  return VirtualFree (address, 0, MEM_RELEASE);
}

hx_boolean
hoox_memory_release (hx_pointer address,
                    hx_size size)
{
  return VirtualFree (address, size, MEM_DECOMMIT);
}

hx_boolean
hoox_memory_recommit (hx_pointer address,
                     hx_size size,
                     HooxPageProtection prot)
{
  return VirtualAlloc (address, size, MEM_COMMIT,
      hoox_page_protection_to_windows (prot)) != NULL;
}

hx_boolean
hoox_memory_discard (hx_pointer address,
                    hx_size size)
{
  static hx_boolean initialized = FALSE;
  static DWORD (WINAPI * discard_impl) (PVOID address, SIZE_T size);

  if (!initialized)
  {
    discard_impl = HOOX_POINTER_TO_FUNCPTR (DWORD (WINAPI *) (PVOID, SIZE_T),
        GetProcAddress (GetModuleHandleW (L"kernel32.dll"),
          "DiscardVirtualMemory"));
    initialized = TRUE;
  }

  if (discard_impl != NULL)
  {
    if (discard_impl (address, size) == ERROR_SUCCESS)
      return TRUE;
  }

  return VirtualAlloc (address, size, MEM_RESET, PAGE_READWRITE) != NULL;
}

hx_boolean
hoox_memory_decommit (hx_pointer address,
                     hx_size size)
{
  return VirtualFree (address, size, MEM_DECOMMIT);
}

static hx_boolean
hoox_memory_get_protection (hx_constpointer address,
                           hx_size len,
                           HooxPageProtection * prot)
{
  hx_boolean success = FALSE;
  MEMORY_BASIC_INFORMATION mbi;

  if (prot == NULL)
  {
    HooxPageProtection ignored_prot;

    return hoox_memory_get_protection (address, len, &ignored_prot);
  }

  *prot = HOOX_PAGE_NO_ACCESS;

  if (len > 1)
  {
    hx_size page_size, start_page, end_page, cur_page;

    page_size = hoox_query_page_size ();

    start_page = HX_POINTER_TO_SIZE (address) & ~(page_size - 1);
    end_page = (HX_POINTER_TO_SIZE (address) + len - 1) & ~(page_size - 1);

    success = hoox_memory_get_protection (HX_SIZE_TO_POINTER (start_page), 1,
        prot);

    for (cur_page = start_page + page_size;
        cur_page != end_page + page_size;
        cur_page += page_size)
    {
      HooxPageProtection cur_prot;

      if (hoox_memory_get_protection (HX_SIZE_TO_POINTER (cur_page), 1, &cur_prot))
      {
        success = TRUE;
        *prot &= cur_prot;
      }
      else
      {
        *prot = HOOX_PAGE_NO_ACCESS;
        break;
      }
    }

    return success;
  }

  success = VirtualQuery (address, &mbi, sizeof (mbi)) != 0;
  if (success)
    *prot = hoox_page_protection_from_windows (mbi.Protect);

  return success;
}

HooxPageProtection
hoox_page_protection_from_windows (DWORD native_prot)
{
  switch (native_prot & 0xff)
  {
    case PAGE_NOACCESS:
      return HOOX_PAGE_NO_ACCESS;
    case PAGE_READONLY:
      return HOOX_PAGE_READ;
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
      return HOOX_PAGE_RW;
    case PAGE_EXECUTE:
      return HOOX_PAGE_EXECUTE;
    case PAGE_EXECUTE_READ:
      return HOOX_PAGE_RX;
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
      return HOOX_PAGE_RWX;
  }

  hx_assert_not_reached ();
}

DWORD
hoox_page_protection_to_windows (HooxPageProtection prot)
{
  switch (prot)
  {
    case HOOX_PAGE_NO_ACCESS:
      return PAGE_NOACCESS;
    case HOOX_PAGE_READ:
      return PAGE_READONLY;
    case HOOX_PAGE_READ | HOOX_PAGE_WRITE:
      return PAGE_READWRITE;
    case HOOX_PAGE_READ | HOOX_PAGE_EXECUTE:
      return PAGE_EXECUTE_READ;
    case HOOX_PAGE_EXECUTE | HOOX_PAGE_READ | HOOX_PAGE_WRITE:
      return PAGE_EXECUTE_READWRITE;
  }

#ifndef HX_DISABLE_ASSERT
  hx_assert_not_reached ();
#else
  abort ();
#endif
}
