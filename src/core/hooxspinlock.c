/*
 * Copyright (C) 2010-2019 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2024 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxspinlock.h"

typedef struct _HooxSpinlockImpl HooxSpinlockImpl;

struct _HooxSpinlockImpl
{
  volatile hx_int is_held;
};

void
hoox_spinlock_init (HooxSpinlock * spinlock)
{
  HooxSpinlockImpl * self = (HooxSpinlockImpl *) spinlock;

  self->is_held = FALSE;
}

void
hoox_spinlock_acquire (HooxSpinlock * spinlock)
{
  HooxSpinlockImpl * self = (HooxSpinlockImpl *) spinlock;

  while (!hx_atomic_int_compare_and_exchange (&self->is_held, FALSE, TRUE))
    ;
}

hx_boolean
hoox_spinlock_try_acquire (HooxSpinlock * spinlock)
{
  HooxSpinlockImpl * self = (HooxSpinlockImpl *) spinlock;

  if (self->is_held)
    return FALSE;

  hoox_spinlock_acquire (spinlock);

  return TRUE;
}

void
hoox_spinlock_release (HooxSpinlock * spinlock)
{
  HooxSpinlockImpl * self = (HooxSpinlockImpl *) spinlock;

  hx_atomic_int_set (&self->is_held, FALSE);
}
