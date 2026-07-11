/*
 * Copyright (C) 2017-2019 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_METAL_ARRAY_H__
#define __HOOX_METAL_ARRAY_H__

#include "hooxdefs.h"

typedef struct _HooxMetalArray HooxMetalArray;

struct _HooxMetalArray
{
  hx_pointer data;
  hx_uint length;
  hx_uint capacity;

  hx_uint element_size;
};

HX_BEGIN_DECLS

HOOX_API void hoox_metal_array_init (HooxMetalArray * array, hx_uint element_size);
HOOX_API void hoox_metal_array_free (HooxMetalArray * array);

HOOX_API hx_pointer hoox_metal_array_element_at (HooxMetalArray * self,
    hx_uint index_);
HOOX_API void hoox_metal_array_remove_at (HooxMetalArray * self, hx_uint index_);
HOOX_API void hoox_metal_array_remove_all (HooxMetalArray * self);
HOOX_API hx_pointer hoox_metal_array_append (HooxMetalArray * self);

HOOX_API void hoox_metal_array_ensure_capacity (HooxMetalArray * self,
    hx_uint capacity);

HX_END_DECLS

#endif
