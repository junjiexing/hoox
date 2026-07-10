/*
 * Copyright (C) 2010-2019 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __INTERCEPTOR_CALLBACKLISTENER_H__
#define __INTERCEPTOR_CALLBACKLISTENER_H__

#include "guminvocationlistener.h"

G_BEGIN_DECLS

typedef struct _TestCallbackListener TestCallbackListener;

typedef void (* TestCallbackListenerFunc) (gpointer user_data,
    GumInvocationContext * context);

struct _TestCallbackListener
{
  GumInvocationListener listener;   /* plain-C base (vtable + refcount) */

  TestCallbackListenerFunc on_enter;
  TestCallbackListenerFunc on_leave;
  gpointer user_data;
};

TestCallbackListener * test_callback_listener_new (void);

G_END_DECLS

#endif
