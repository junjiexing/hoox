/*
 * Copyright (C) 2010-2019 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "interceptor-callbacklistener.h"

#include "hxmem.h"

static void
test_callback_listener_on_enter (HooxInvocationListener * listener,
                                 HooxInvocationContext * context)
{
  TestCallbackListener * self = (TestCallbackListener *) listener;

  if (self->on_enter != NULL)
    self->on_enter (self->user_data, context);
}

static void
test_callback_listener_on_leave (HooxInvocationListener * listener,
                                 HooxInvocationContext * context)
{
  TestCallbackListener * self = (TestCallbackListener *) listener;

  if (self->on_leave != NULL)
    self->on_leave (self->user_data, context);
}

static const HooxInvocationListenerInterface test_callback_listener_iface =
{
  test_callback_listener_on_enter,
  test_callback_listener_on_leave
};

TestCallbackListener *
test_callback_listener_new (void)
{
  TestCallbackListener * listener = hx_new0 (TestCallbackListener, 1);

  hoox_invocation_listener_init (&listener->listener,
      &test_callback_listener_iface, NULL);

  return listener;
}
