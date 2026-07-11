/*
 * Copyright (C) 2008-2010 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxreturnaddress.h"
#include "hooxsymbolutil.h"

#include <string.h>

hx_boolean
hoox_return_address_details_from_address (HooxReturnAddress address,
                                         HooxReturnAddressDetails * details)
{
  HooxDebugSymbolDetails sd;

  if (hoox_symbol_details_from_address (address, &sd))
  {
    details->address = address;

    strcpy (details->module_name, sd.module_name);
    strcpy (details->function_name, sd.symbol_name);
    strcpy (details->file_name, sd.file_name);
    details->line_number = sd.line_number;
    details->column = sd.column;

    return TRUE;
  }

  return FALSE;
}

hx_boolean
hoox_return_address_array_is_equal (const HooxReturnAddressArray * array1,
                                   const HooxReturnAddressArray * array2)
{
  hx_uint i;

  if (array1->len != array2->len)
    return FALSE;

  for (i = 0; i < array1->len; i++)
  {
    if (array1->items[i] != array2->items[i])
      return FALSE;
  }

  return TRUE;
}
