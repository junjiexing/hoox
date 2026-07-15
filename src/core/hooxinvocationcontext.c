/*
 * Copyright (C) 2008-2019 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxinvocationcontext.h"

#include "hooxinterceptor-priv.h"

HooxPointCut
hoox_invocation_context_get_point_cut (HooxInvocationContext * context)
{
  return context->backend->get_point_cut (context);
}

hx_pointer
hoox_invocation_context_get_nth_argument (HooxInvocationContext * context,
                                         hx_uint n)
{
  return hoox_cpu_context_get_nth_argument (context->cpu_context, n);
}

void
hoox_invocation_context_replace_nth_argument (HooxInvocationContext * context,
                                             hx_uint n,
                                             hx_pointer value)
{
  hoox_cpu_context_replace_nth_argument (context->cpu_context, n, value);
}

hx_pointer
hoox_invocation_context_get_return_value (HooxInvocationContext * context)
{
  return hoox_cpu_context_get_return_value (context->cpu_context);
}

void
hoox_invocation_context_replace_return_value (HooxInvocationContext * context,
                                             hx_pointer value)
{
  hoox_cpu_context_replace_return_value (context->cpu_context, value);
}

hx_pointer
hoox_invocation_context_get_return_address (HooxInvocationContext * context)
{
  return _hoox_interceptor_peek_top_caller_return_address ();
}

HooxThreadId
hoox_invocation_context_get_thread_id (HooxInvocationContext * context)
{
  return context->backend->get_thread_id (context);
}

hx_uint
hoox_invocation_context_get_depth (HooxInvocationContext * context)
{
  return context->backend->get_depth (context);
}

hx_pointer
hoox_invocation_context_get_listener_thread_data (
    HooxInvocationContext * context,
    hx_size required_size)
{
  return context->backend->get_listener_thread_data (context, required_size);
}

hx_pointer
hoox_invocation_context_get_listener_function_data (
    HooxInvocationContext * context)
{
  return context->backend->get_listener_function_data (context);
}

hx_pointer
hoox_invocation_context_get_listener_invocation_data (
    HooxInvocationContext * context,
    hx_size required_size)
{
  return context->backend->get_listener_invocation_data (context,
      required_size);
}

hx_pointer
hoox_invocation_context_get_replacement_data (HooxInvocationContext * context)
{
  return context->backend->get_replacement_data (context);
}
