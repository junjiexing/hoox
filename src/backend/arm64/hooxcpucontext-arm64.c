/*
 * Copyright (C) 2014-2015 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxdefs.h"

hx_pointer
hoox_cpu_context_get_nth_argument (HooxCpuContext * self,
                                  hx_uint n)
{
  if (n < 8)
  {
    return HX_SIZE_TO_POINTER (self->x[n]);
  }
  else
  {
    hx_pointer * stack_argument = HX_SIZE_TO_POINTER (self->sp);

    return stack_argument[n - 8];
  }
}

void
hoox_cpu_context_replace_nth_argument (HooxCpuContext * self,
                                      hx_uint n,
                                      hx_pointer value)
{
  if (n < 8)
  {
    self->x[n] = HX_POINTER_TO_SIZE (value);
  }
  else
  {
    hx_pointer * stack_argument = HX_SIZE_TO_POINTER (self->sp);

    stack_argument[n - 8] = value;
  }
}

hx_pointer
hoox_cpu_context_get_return_value (HooxCpuContext * self)
{
  return HX_SIZE_TO_POINTER (self->x[0]);
}

void
hoox_cpu_context_replace_return_value (HooxCpuContext * self,
                                      hx_pointer value)
{
  self->x[0] = HX_POINTER_TO_SIZE (value);
}
