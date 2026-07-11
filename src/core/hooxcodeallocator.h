/*
 * Copyright (C) 2010-2021 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2025 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_CODE_ALLOCATOR_H__
#define __HOOX_CODE_ALLOCATOR_H__

#include "hooxmemory.h"

#define HOOX_TYPE_CODE_SLICE (hoox_code_slice_get_type ())
#define HOOX_TYPE_CODE_DEFLECTOR (hoox_code_deflector_get_type ())

HX_BEGIN_DECLS

typedef struct _HooxCodeAllocator HooxCodeAllocator;
typedef struct _HooxCodeSlice HooxCodeSlice;
typedef struct _HooxCodeDeflector HooxCodeDeflector;

struct _HooxCodeAllocator
{
  hx_size slice_size;
  hx_size pages_per_batch;
  hx_size slices_per_batch;
  hx_size pages_metadata_size;

  HxSList * uncommitted_pages;
  HxHashTable * dirty_pages;
  HxList * free_slices;

  HxSList * dispatchers;
};

struct _HooxCodeSlice
{
  hx_pointer data;
  hx_pointer pc;
  hx_uint size;

  /*< private >*/
  hx_int ref_count;
};

struct _HooxCodeDeflector
{
  hx_pointer return_address;
  hx_pointer target;
  hx_pointer trampoline;

  /*< private >*/
  hx_int ref_count;
};

HOOX_API void hoox_code_allocator_init (HooxCodeAllocator * allocator,
    hx_size slice_size);
HOOX_API void hoox_code_allocator_free (HooxCodeAllocator * allocator);

HOOX_API HooxCodeSlice * hoox_code_allocator_alloc_slice (HooxCodeAllocator * self);
HOOX_API HooxCodeSlice * hoox_code_allocator_try_alloc_slice_near (
    HooxCodeAllocator * self, const HooxAddressSpec * spec, hx_size alignment);
HOOX_API void hoox_code_allocator_commit (HooxCodeAllocator * self);
HOOX_API HxType hoox_code_slice_get_type (void) HX_GNUC_CONST;
HOOX_API void hoox_code_slice_unref (HooxCodeSlice * slice);

HOOX_API HooxCodeDeflector * hoox_code_allocator_alloc_deflector (
    HooxCodeAllocator * self, const HooxAddressSpec * caller,
    hx_pointer return_address, hx_pointer target, hx_boolean dedicated);
HOOX_API HxType hoox_code_deflector_get_type (void) HX_GNUC_CONST;
HOOX_API void hoox_code_deflector_unref (HooxCodeDeflector * deflector);

HX_END_DECLS

#endif
