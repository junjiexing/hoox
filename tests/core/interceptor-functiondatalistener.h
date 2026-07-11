/*
 * Copyright (C) 2008-2019 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2008 Christian Berentsen <jc.berentsen@gmail.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __INTERCEPTOR_FUNCTIONDATALISTENER_H__
#define __INTERCEPTOR_FUNCTIONDATALISTENER_H__

#include "hooxinvocationlistener.h"

HX_BEGIN_DECLS

typedef struct _TestFunctionDataListener TestFunctionDataListener;
typedef struct _TestFunctionInvocationData TestFunctionInvocationData;
typedef struct _TestFuncThreadState TestFuncThreadState;
typedef struct _TestFuncInvState TestFuncInvState;

struct _TestFuncThreadState
{
  hx_boolean initialized;
  hx_char name[8];
};

struct _TestFuncInvState
{
  hx_char arg[16];
};

struct _TestFunctionInvocationData
{
  hx_pointer function_data;
  TestFuncThreadState thread_data;
  TestFuncInvState invocation_data;
};

struct _TestFunctionDataListener
{
  HooxInvocationListener listener;   /* plain-C base */

  hx_uint on_enter_call_count;
  hx_uint on_leave_call_count;

  hx_uint init_thread_state_count;

  TestFunctionInvocationData last_on_enter_data;
  TestFunctionInvocationData last_on_leave_data;

  HxSList * a_threads_seen;
  hx_uint a_thread_index;

  HxSList * b_threads_seen;
  hx_uint b_thread_index;
};

TestFunctionDataListener * test_function_data_listener_new (void);
void test_function_data_listener_reset (TestFunctionDataListener * self);

HX_END_DECLS

#endif
