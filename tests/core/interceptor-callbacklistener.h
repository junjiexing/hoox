/*
 * Copyright (C) 2010-2019 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __INTERCEPTOR_CALLBACKLISTENER_H__
#define __INTERCEPTOR_CALLBACKLISTENER_H__

#include "hooxinvocationlistener.h"

HX_BEGIN_DECLS

typedef struct _TestCallbackListener TestCallbackListener;

typedef void (* TestCallbackListenerFunc) (hx_pointer user_data,
    HooxInvocationContext * context);

struct _TestCallbackListener
{
  HooxInvocationListener listener;   /* plain-C base (vtable + refcount) */

  TestCallbackListenerFunc on_enter;
  TestCallbackListenerFunc on_leave;
  hx_pointer user_data;
};

TestCallbackListener * test_callback_listener_new (void);

HX_END_DECLS

#endif
