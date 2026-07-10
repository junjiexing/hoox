/*
 * Copyright (C) 2008-2019 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2008 Christian Berentsen <jc.berentsen@gmail.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "interceptor-functiondatalistener.h"

#include "hxglib.h"

#include <string.h>

static void
test_function_data_listener_init_thread_state (TestFunctionDataListener * self,
                                               TestFuncThreadState * state,
                                               gpointer function_data)
{
  GSList ** threads_seen = NULL;
  guint * thread_index = 0;
  GThread * cur_thread;

  self->init_thread_state_count++;

  if (strcmp ((gchar *) function_data, "a") == 0)
  {
    threads_seen = &self->a_threads_seen;
    thread_index = &self->a_thread_index;
  }
  else if (strcmp ((gchar *) function_data, "b") == 0)
  {
    threads_seen = &self->b_threads_seen;
    thread_index = &self->b_thread_index;
  }
  else
    g_assert_not_reached ();

  cur_thread = g_thread_self ();
  if (g_slist_find (*threads_seen, cur_thread) == NULL)
  {
    *threads_seen = g_slist_prepend (*threads_seen, cur_thread);
    (*thread_index)++;
  }

  g_snprintf (state->name, sizeof (state->name), "%s%d",
      (gchar *) function_data, *thread_index);

  state->initialized = TRUE;
}

static void
test_function_data_listener_on_enter (GumInvocationListener * listener,
                                      GumInvocationContext * context)
{
  TestFunctionDataListener * self = (TestFunctionDataListener *) listener;
  gpointer function_data;
  TestFuncThreadState * thread_state;
  TestFuncInvState * invocation_state;

  function_data = GUM_IC_GET_FUNC_DATA (context, gpointer);

  thread_state = GUM_IC_GET_THREAD_DATA (context, TestFuncThreadState);
  if (!thread_state->initialized)
  {
    test_function_data_listener_init_thread_state (self, thread_state,
        function_data);
  }

  invocation_state = GUM_IC_GET_INVOCATION_DATA (context, TestFuncInvState);
  g_strlcpy (invocation_state->arg,
      (const gchar *) gum_invocation_context_get_nth_argument (context, 0),
      sizeof (invocation_state->arg));

  self->on_enter_call_count++;

  self->last_on_enter_data.function_data = function_data;
  self->last_on_enter_data.thread_data = *thread_state;
  self->last_on_enter_data.invocation_data = *invocation_state;
}

static void
test_function_data_listener_on_leave (GumInvocationListener * listener,
                                      GumInvocationContext * context)
{
  TestFunctionDataListener * self = (TestFunctionDataListener *) listener;
  TestFuncThreadState * thread_state;
  TestFuncInvState * invocation_state;

  thread_state = GUM_IC_GET_THREAD_DATA (context, TestFuncThreadState);

  invocation_state = GUM_IC_GET_INVOCATION_DATA (context, TestFuncInvState);

  self->on_leave_call_count++;
  self->last_on_leave_data.function_data =
      GUM_IC_GET_FUNC_DATA (context, gpointer);
  self->last_on_leave_data.thread_data = *thread_state;
  self->last_on_leave_data.invocation_data = *invocation_state;
}

static const GumInvocationListenerInterface
    test_function_data_listener_iface =
{
  test_function_data_listener_on_enter,
  test_function_data_listener_on_leave
};

static void
test_function_data_listener_finalize (gpointer instance)
{
  TestFunctionDataListener * self = instance;

  g_slist_free (self->a_threads_seen);
  g_slist_free (self->b_threads_seen);
}

TestFunctionDataListener *
test_function_data_listener_new (void)
{
  TestFunctionDataListener * self = g_new0 (TestFunctionDataListener, 1);

  gum_invocation_listener_init (&self->listener,
      &test_function_data_listener_iface,
      test_function_data_listener_finalize);

  return self;
}

void
test_function_data_listener_reset (TestFunctionDataListener * self)
{
  self->on_enter_call_count = 0;
  self->on_leave_call_count = 0;
  self->init_thread_state_count = 0;
  memset (&self->last_on_enter_data, 0, sizeof (TestFunctionInvocationData));
  memset (&self->last_on_leave_data, 0, sizeof (TestFunctionInvocationData));
}
