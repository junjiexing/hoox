/*
 * Copyright (C) 2015-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * hoox — Darwin (macOS) memory backend.
 *
 * Provides the slice of the memory backend that the generic POSIX allocator
 * (hooxmemory-posix.c) does not cover: page-protection queries via the mach VM
 * map, the RWX mprotect helper, and the writable-remap / cache-clear stubs.
 * On macOS x86_64 the hook engine takes the RWX mprotect path
 * (hoox_query_rwx_support == HOOX_RWX_FULL), so page remapping is unused.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxmemory.h"

#include "hooxmemory-priv.h"

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined (HAVE_ARM) || defined (HAVE_ARM64)
# include <libkern/OSCacheControl.h>
#endif

#ifndef HAVE_LINUX
HX_GNUC_INTERNAL void _hoox_memory_query_protections (HxPtrArray * sorted_pages,
    HooxPageProtection * protections);
#endif

static HooxPageProtection hoox_page_protection_from_mach (vm_prot_t native_prot);

hx_boolean
hoox_memory_query_protection (hx_constpointer address,
                             HooxPageProtection * prot)
{
  mach_port_t task;
  mach_vm_address_t region_address;
  mach_vm_size_t region_size;
  vm_region_basic_info_data_64_t info;
  mach_msg_type_number_t info_count;
  mach_port_t object_name;
  kern_return_t kr;

  task = mach_task_self ();
  region_address = (mach_vm_address_t) (hx_uintptr) address;
  region_size = 0;
  info_count = VM_REGION_BASIC_INFO_COUNT_64;
  object_name = MACH_PORT_NULL;

  kr = mach_vm_region (task, &region_address, &region_size,
      VM_REGION_BASIC_INFO_64, (vm_region_info_t) &info, &info_count,
      &object_name);
  if (kr != KERN_SUCCESS)
    return FALSE;

  if (object_name != MACH_PORT_NULL)
    mach_port_deallocate (task, object_name);

  if ((mach_vm_address_t) (hx_uintptr) address < region_address)
    return FALSE;

  *prot = hoox_page_protection_from_mach (info.protection);

  return TRUE;
}

void
_hoox_memory_query_protections (HxPtrArray * sorted_pages,
                               HooxPageProtection * protections)
{
  hx_uint i;

  for (i = 0; i != sorted_pages->len; i++)
  {
    hx_constpointer page = hx_ptr_array_index (sorted_pages, i);
    HooxPageProtection prot;

    if (hoox_memory_query_protection (page, &prot))
      protections[i] = prot;
    else
      protections[i] = HOOX_PAGE_RX;
  }
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
  sys_icache_invalidate (address, size);
#else
  (void) address;
  (void) size;
#endif
}

static HooxPageProtection
hoox_page_protection_from_mach (vm_prot_t native_prot)
{
  HooxPageProtection prot = HOOX_PAGE_NO_ACCESS;

  if ((native_prot & VM_PROT_READ) == VM_PROT_READ)
    prot |= HOOX_PAGE_READ;
  if ((native_prot & VM_PROT_WRITE) == VM_PROT_WRITE)
    prot |= HOOX_PAGE_WRITE;
  if ((native_prot & VM_PROT_EXECUTE) == VM_PROT_EXECUTE)
    prot |= HOOX_PAGE_EXECUTE;

  return prot;
}
