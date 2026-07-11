/*
 * Copyright (C) 2015-2021 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_LIBC_H__
#define __HOOX_LIBC_H__

#include "hooxdefs.h"

HX_BEGIN_DECLS

HOOX_API hx_pointer hoox_memset (hx_pointer dst, hx_int c, hx_size n);
HOOX_API hx_pointer hoox_memcpy (hx_pointer dst, hx_constpointer src, hx_size n);
HOOX_API hx_pointer hoox_memmove (hx_pointer dst, hx_constpointer src, hx_size n);

HX_END_DECLS

#endif
