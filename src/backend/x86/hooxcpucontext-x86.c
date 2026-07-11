/*
 * Copyright (C) 2008-2017 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxdefs.h"

hx_pointer
hoox_cpu_context_get_nth_argument (HooxCpuContext * self,
                                  hx_uint n)
{
  hx_pointer * stack_argument;

#if HX_SIZEOF_VOID_P == 4
  stack_argument = (hx_pointer *) (self->esp + 4);
  return stack_argument[n];
#else
  stack_argument = (hx_pointer *) (self->rsp + 8);
  switch (n)
  {
# if HOOX_NATIVE_ABI_IS_UNIX
    case 0:  return (hx_pointer) self->rdi;
    case 1:  return (hx_pointer) self->rsi;
    case 2:  return (hx_pointer) self->rdx;
    case 3:  return (hx_pointer) self->rcx;
    case 4:  return (hx_pointer) self->r8;
    case 5:  return (hx_pointer) self->r9;
    default: return            stack_argument[n - 6];
# else
    case 0:  return (hx_pointer) self->rcx;
    case 1:  return (hx_pointer) self->rdx;
    case 2:  return (hx_pointer) self->r8;
    case 3:  return (hx_pointer) self->r9;
    default: return            stack_argument[n];
# endif
  }
#endif
}

void
hoox_cpu_context_replace_nth_argument (HooxCpuContext * self,
                                      hx_uint n,
                                      hx_pointer value)
{
  hx_pointer * stack_argument;

#if HX_SIZEOF_VOID_P == 4
  stack_argument = (hx_pointer *) (self->esp + 4);
  stack_argument[n] = value;
#else
  stack_argument = (hx_pointer *) (self->rsp + 8);
  switch (n)
  {
# if HOOX_NATIVE_ABI_IS_UNIX
    case 0:  self->rdi             = (hx_uint64) value; break;
    case 1:  self->rsi             = (hx_uint64) value; break;
    case 2:  self->rdx             = (hx_uint64) value; break;
    case 3:  self->rcx             = (hx_uint64) value; break;
    case 4:  self->r8              = (hx_uint64) value; break;
    case 5:  self->r9              = (hx_uint64) value; break;
    default: stack_argument[n - 6] =           value; break;
# else
    case 0:  self->rcx             = (hx_uint64) value; break;
    case 1:  self->rdx             = (hx_uint64) value; break;
    case 2:  self->r8              = (hx_uint64) value; break;
    case 3:  self->r9              = (hx_uint64) value; break;
    default: stack_argument[n]     =           value; break;
# endif
  }
#endif
}

hx_pointer
hoox_cpu_context_get_return_value (HooxCpuContext * self)
{
#if HX_SIZEOF_VOID_P == 4
  return (hx_pointer) self->eax;
#else
  return (hx_pointer) self->rax;
#endif
}

void
hoox_cpu_context_replace_return_value (HooxCpuContext * self,
                                      hx_pointer value)
{
#if HX_SIZEOF_VOID_P == 4
  self->eax = (hx_uint32) value;
#else
  self->rax = (hx_uint64) value;
#endif
}
