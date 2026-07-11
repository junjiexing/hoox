/*
 * Copyright (C) 2009 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_FUNCTION_H__
#define __HOOX_FUNCTION_H__

HX_BEGIN_DECLS

typedef struct _HooxFunctionDetails  HooxFunctionDetails;

struct _HooxFunctionDetails
{
  const hx_char * name;
  hx_pointer address;
  hx_int num_arguments;
};

HX_END_DECLS

#endif
