/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxdefs.h"

hx_pointer
hoox_cpu_context_get_nth_argument (HooxCpuContext * self,
                                  hx_uint n)
{
  if (n < 4)
  {
    return (hx_pointer) self->r[n];
  }
  else
  {
    hx_pointer * stack_argument = (hx_pointer *) self->sp;

    return stack_argument[n - 4];
  }
}

void
hoox_cpu_context_replace_nth_argument (HooxCpuContext * self,
                                      hx_uint n,
                                      hx_pointer value)
{
  if (n < 4)
  {
    self->r[n] = (hx_uint32) value;
  }
  else
  {
    hx_pointer * stack_argument = (hx_pointer *) self->sp;

    stack_argument[n - 4] = value;
  }
}

hx_pointer
hoox_cpu_context_get_return_value (HooxCpuContext * self)
{
  return (hx_pointer) self->r[0];
}

void
hoox_cpu_context_replace_return_value (HooxCpuContext * self,
                                      hx_pointer value)
{
  self->r[0] = (hx_uint32) value;
}
