/*
 * Copyright (C) 2008-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_INVOCATION_CONTEXT_H__
#define __HOOX_INVOCATION_CONTEXT_H__

#include "hooxprocess.h"

#include "hooxdefs.h"

#define HOOX_IC_GET_THREAD_DATA(context, data_type) \
    ((data_type *) hoox_invocation_context_get_listener_thread_data (context, \
        sizeof (data_type)))
#define HOOX_IC_GET_FUNC_DATA(context, data_type) \
    ((data_type) hoox_invocation_context_get_listener_function_data (context))
#define HOOX_IC_GET_INVOCATION_DATA(context, data_type) \
    ((data_type *) \
        hoox_invocation_context_get_listener_invocation_data (context,\
            sizeof (data_type)))

#define HOOX_IC_GET_REPLACEMENT_DATA(ctx, data_type) \
    ((data_type) hoox_invocation_context_get_replacement_data (ctx))

typedef struct _HooxInvocationBackend HooxInvocationBackend;
typedef struct _HooxInvocationContext HooxInvocationContext;
typedef hx_uint HooxPointCut;

struct _HooxInvocationBackend
{
  HooxPointCut (* get_point_cut) (HooxInvocationContext * context);

  HooxThreadId (* get_thread_id) (HooxInvocationContext * context);
  hx_uint (* get_depth) (HooxInvocationContext * context);

  hx_pointer (* get_listener_thread_data) (HooxInvocationContext * context,
      hx_size required_size);
  hx_pointer (* get_listener_function_data) (HooxInvocationContext * context);
  hx_pointer (* get_listener_invocation_data) (
      HooxInvocationContext * context, hx_size required_size);

  hx_pointer (* get_replacement_data) (HooxInvocationContext * context);

  hx_pointer state;
  hx_pointer data;
};

struct _HooxInvocationContext
{
  hx_pointer function;
  HooxCpuContext * cpu_context;
  hx_int system_error;

  /*< private */
  HooxInvocationBackend * backend;
};

enum _HooxPointCut
{
  HOOX_POINT_ENTER,
  HOOX_POINT_LEAVE
};

HX_BEGIN_DECLS

HOOX_API HooxPointCut hoox_invocation_context_get_point_cut (
    HooxInvocationContext * context);

HOOX_API hx_pointer hoox_invocation_context_get_nth_argument (
    HooxInvocationContext * context, hx_uint n);
HOOX_API void hoox_invocation_context_replace_nth_argument (
    HooxInvocationContext * context, hx_uint n, hx_pointer value);
HOOX_API hx_pointer hoox_invocation_context_get_return_value (
    HooxInvocationContext * context);
HOOX_API void hoox_invocation_context_replace_return_value (
    HooxInvocationContext * context, hx_pointer value);

HOOX_API hx_pointer hoox_invocation_context_get_return_address (
    HooxInvocationContext * context);

HOOX_API HooxThreadId hoox_invocation_context_get_thread_id (
    HooxInvocationContext * context);
HOOX_API hx_uint hoox_invocation_context_get_depth (
    HooxInvocationContext * context);

HOOX_API hx_pointer hoox_invocation_context_get_listener_thread_data (
    HooxInvocationContext * context, hx_size required_size);
HOOX_API hx_pointer hoox_invocation_context_get_listener_function_data (
    HooxInvocationContext * context);
HOOX_API hx_pointer hoox_invocation_context_get_listener_invocation_data (
    HooxInvocationContext * context, hx_size required_size);

HOOX_API hx_pointer hoox_invocation_context_get_replacement_data (
    HooxInvocationContext * context);

HX_END_DECLS

#endif
