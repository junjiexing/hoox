/*
 * Copyright (C) 2010-2014 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_SYS_INTERNALS_H__
#define __HOOX_SYS_INTERNALS_H__

#include "hxglib.h"

#ifdef HX_OS_WIN32

# if HX_SIZEOF_VOID_P == 4
#  define HOOX_TEB_OFFSET_SELF 0x0018
#  define HOOX_TEB_OFFSET_TID  0x0024
# else
#  define HOOX_TEB_OFFSET_SELF 0x0030
#  define HOOX_TEB_OFFSET_TID  0x0048
# endif

#endif

#endif
