/*
 * Copyright (C) 2010-2024 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2024 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_SPINLOCK_H__
#define __HOOX_SPINLOCK_H__

#include "hooxdefs.h"

#define HOOX_SPINLOCK_INIT { NULL }

HX_BEGIN_DECLS

typedef struct _HooxSpinlock HooxSpinlock;

struct _HooxSpinlock
{
  hx_pointer data;
};


HOOX_API void hoox_spinlock_acquire (HooxSpinlock * spinlock);
HOOX_API void hoox_spinlock_release (HooxSpinlock * spinlock);

HX_END_DECLS

#endif
