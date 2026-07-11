/*
 * Copyright (C) 2008-2010 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_RETURN_ADDRESS_H__
#define __HOOX_RETURN_ADDRESS_H__

#include "hooxdefs.h"

typedef struct _HooxReturnAddressDetails HooxReturnAddressDetails;
typedef hx_pointer HooxReturnAddress;
typedef struct _HooxReturnAddressArray HooxReturnAddressArray;

struct _HooxReturnAddressDetails
{
  HooxReturnAddress address;
  hx_char module_name[HOOX_MAX_PATH + 1];
  hx_char function_name[HOOX_MAX_SYMBOL_NAME + 1];
  hx_char file_name[HOOX_MAX_PATH + 1];
  hx_uint line_number;
  hx_uint column;
};

struct _HooxReturnAddressArray
{
  hx_uint len;
  HooxReturnAddress items[HOOX_MAX_BACKTRACE_DEPTH];
};

HX_BEGIN_DECLS

HOOX_API hx_boolean hoox_return_address_details_from_address (
    HooxReturnAddress address, HooxReturnAddressDetails * details);

HOOX_API hx_boolean hoox_return_address_array_is_equal (
    const HooxReturnAddressArray * array1,
    const HooxReturnAddressArray * array2);

HX_END_DECLS

#endif
