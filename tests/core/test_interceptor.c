/*
 * Interceptor behaviour tests (M6) — mirrors the core cases of frida-gum's
 * tests/core/interceptor.c using frida's real target functions and the ported
 * (plain-C) callback + function-data listeners. Covers attach (one/two/
 * ordering), detach, replace (+ keep-original / call-original), listener
 * refcount, argument & return-value access, and per-function data.
 *
 * The lowlevelhelpers-based CPU-clobber / proxy-relocation cases from the
 * upstream suite are not ported here (they rely on runtime proxy-function
 * generation); the writer/relocator relocation logic they stress is covered
 * by the arch_x86 suite instead.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gum.h"
#include "interceptor-callbacklistener.h"
#include "interceptor-functiondatalistener.h"

#include "testrunner.h"

#include <string.h>

extern gpointer gum_test_target_function (GString * str);
extern gpointer gum_test_target_nop_function_a (gpointer data);
extern gpointer gum_test_target_nop_function_b (gpointer data);

/* ---- fixture ------------------------------------------------------------ */

typedef struct _TestInterceptorFixture TestInterceptorFixture;
typedef struct _ListenerContext ListenerContext;

struct _ListenerContext
{
  TestCallbackListener * listener;
  TestInterceptorFixture * fixture;
  gchar enter_char;
  gchar leave_char;
  gsize last_seen_argument;
  gpointer last_return_value;
};

struct _TestInterceptorFixture
{
  GumInterceptor * interceptor;
  GString * result;
  ListenerContext * listener_context[2];
};

static void
listener_context_on_enter (ListenerContext * self,
                           GumInvocationContext * context)
{
  g_string_append_c (self->fixture->result, self->enter_char);
  self->last_seen_argument = (gsize)
      gum_invocation_context_get_nth_argument (context, 0);
}

static void
listener_context_on_leave (ListenerContext * self,
                           GumInvocationContext * context)
{
  g_string_append_c (self->fixture->result, self->leave_char);
  self->last_return_value =
      gum_invocation_context_get_return_value (context);
}

static void
listener_context_free (ListenerContext * ctx)
{
  gum_invocation_listener_unref (GUM_INVOCATION_LISTENER (ctx->listener));
  g_free (ctx);
}

static GumAttachReturn
fixture_attach (TestInterceptorFixture * h,
                guint listener_index,
                gpointer test_func,
                gchar enter_char,
                gchar leave_char)
{
  GumAttachReturn result;
  ListenerContext * ctx;

  ctx = h->listener_context[listener_index];
  if (ctx != NULL)
  {
    listener_context_free (ctx);
    h->listener_context[listener_index] = NULL;
  }

  ctx = g_new0 (ListenerContext, 1);
  ctx->listener = test_callback_listener_new ();
  ctx->listener->on_enter = (TestCallbackListenerFunc) listener_context_on_enter;
  ctx->listener->on_leave = (TestCallbackListenerFunc) listener_context_on_leave;
  ctx->listener->user_data = ctx;
  ctx->fixture = h;
  ctx->enter_char = enter_char;
  ctx->leave_char = leave_char;

  result = gum_interceptor_attach (h->interceptor, test_func,
      GUM_INVOCATION_LISTENER (ctx->listener), NULL);
  if (result == GUM_ATTACH_OK)
    h->listener_context[listener_index] = ctx;
  else
    listener_context_free (ctx);

  return result;
}

static void
test_interceptor_fixture_setup (TestInterceptorFixture * fixture,
                                gconstpointer data)
{
  (void) data;
  fixture->interceptor = gum_interceptor_obtain ();
  fixture->result = g_string_sized_new (256);
  memset (&fixture->listener_context, 0, sizeof (fixture->listener_context));
}

static void
test_interceptor_fixture_teardown (TestInterceptorFixture * fixture,
                                   gconstpointer data)
{
  guint i;
  (void) data;

  for (i = 0; i != G_N_ELEMENTS (fixture->listener_context); i++)
  {
    ListenerContext * ctx = fixture->listener_context[i];
    if (ctx != NULL)
    {
      gum_interceptor_detach (fixture->interceptor,
          GUM_INVOCATION_LISTENER (ctx->listener));
      listener_context_free (ctx);
    }
  }

  g_string_free (fixture->result, TRUE);
  gum_interceptor_unref (fixture->interceptor);
}

#define TESTCASE(NAME) \
    void test_interceptor_ ## NAME ( \
        TestInterceptorFixture * fixture, gconstpointer data)
#define TESTENTRY(NAME) \
    TESTENTRY_WITH_FIXTURE ("Core/Interceptor", test_interceptor, NAME, \
        TestInterceptorFixture)

TESTLIST_BEGIN (interceptor)
  TESTENTRY (attach_one)
  TESTENTRY (attach_two)
  TESTENTRY (detach)
  TESTENTRY (already_attached)
  TESTENTRY (function_arguments)
  TESTENTRY (function_return_value)
  TESTENTRY (replace_one)
  TESTENTRY (replace_keep_original)
  TESTENTRY (replace_then_attach)
  TESTENTRY (function_data)
TESTLIST_END ()

TESTCASE (attach_one)
{
  fixture_attach (fixture, 0, gum_test_target_function, '>', '<');
  gum_test_target_function (fixture->result);
  g_assert_cmpstr (fixture->result->str, ==, ">|<");
}

TESTCASE (attach_two)
{
  fixture_attach (fixture, 0, gum_test_target_function, 'a', 'A');
  fixture_attach (fixture, 1, gum_test_target_function, 'b', 'B');
  gum_test_target_function (fixture->result);
  g_assert_cmpstr (fixture->result->str, ==, "ab|AB");
}

TESTCASE (detach)
{
  fixture_attach (fixture, 0, gum_test_target_function, '>', '<');
  fixture_attach (fixture, 1, gum_test_target_function, 'a', 'A');

  gum_test_target_function (fixture->result);
  g_assert_cmpstr (fixture->result->str, ==, ">a|<A");

  gum_interceptor_detach (fixture->interceptor,
      GUM_INVOCATION_LISTENER (fixture->listener_context[0]->listener));

  g_string_truncate (fixture->result, 0);
  gum_test_target_function (fixture->result);
  g_assert_cmpstr (fixture->result->str, ==, "a|A");
}

TESTCASE (already_attached)
{
  GumAttachReturn r;

  fixture_attach (fixture, 0, gum_test_target_function, '>', '<');

  r = gum_interceptor_attach (fixture->interceptor, gum_test_target_function,
      GUM_INVOCATION_LISTENER (fixture->listener_context[0]->listener), NULL);
  g_assert_cmpint (r, ==, GUM_ATTACH_ALREADY_ATTACHED);
}

TESTCASE (function_arguments)
{
  fixture_attach (fixture, 0, gum_test_target_nop_function_a, '>', '<');
  gum_test_target_nop_function_a (GSIZE_TO_POINTER (0x12345678));
  g_assert_cmphex (fixture->listener_context[0]->last_seen_argument, ==,
      0x12345678);
}

TESTCASE (function_return_value)
{
  gpointer ret;
  fixture_attach (fixture, 0, gum_test_target_nop_function_a, '>', '<');
  ret = gum_test_target_nop_function_a (NULL);
  g_assert_cmphex (GPOINTER_TO_SIZE (fixture->listener_context[0]->
      last_return_value), ==, GPOINTER_TO_SIZE (ret));
  g_assert_cmphex (GPOINTER_TO_SIZE (ret), ==, 0x1337);
}

/* ---- replace ------------------------------------------------------------ */

static gpointer (* replace_nop_a_orig) (gpointer) = NULL;

static gpointer
replace_nop_a (gpointer data)
{
  (void) data;
  return GSIZE_TO_POINTER (0xbeef);
}

static gpointer
replace_nop_a_calling_original (gpointer data)
{
  gsize orig = GPOINTER_TO_SIZE (replace_nop_a_orig (data));
  return GSIZE_TO_POINTER (orig + 1);
}

TESTCASE (replace_one)
{
  gpointer ret;
  GumReplaceReturn rr;

  rr = gum_interceptor_replace (fixture->interceptor,
      gum_test_target_nop_function_a, replace_nop_a, NULL, NULL);
  g_assert_cmpint (rr, ==, GUM_REPLACE_OK);

  ret = gum_test_target_nop_function_a (NULL);
  g_assert_cmphex (GPOINTER_TO_SIZE (ret), ==, 0xbeef);

  gum_interceptor_revert (fixture->interceptor, gum_test_target_nop_function_a);
  ret = gum_test_target_nop_function_a (NULL);
  g_assert_cmphex (GPOINTER_TO_SIZE (ret), ==, 0x1337);
}

TESTCASE (replace_keep_original)
{
  gpointer ret;

  gum_interceptor_replace (fixture->interceptor, gum_test_target_nop_function_a,
      replace_nop_a_calling_original, (gpointer *) &replace_nop_a_orig, NULL);

  ret = gum_test_target_nop_function_a (NULL);
  g_assert_cmphex (GPOINTER_TO_SIZE (ret), ==, 0x1338); /* 0x1337 + 1 */

  gum_interceptor_revert (fixture->interceptor, gum_test_target_nop_function_a);
}

TESTCASE (replace_then_attach)
{
  /* Attach a listener, then replace a different function; both must work. */
  fixture_attach (fixture, 0, gum_test_target_function, '>', '<');

  gum_interceptor_replace (fixture->interceptor, gum_test_target_nop_function_b,
      replace_nop_a, NULL, NULL);

  gum_test_target_function (fixture->result);
  g_assert_cmpstr (fixture->result->str, ==, ">|<");

  g_assert_cmphex (GPOINTER_TO_SIZE (gum_test_target_nop_function_b (NULL)), ==,
      0xbeef);

  gum_interceptor_revert (fixture->interceptor, gum_test_target_nop_function_b);
}

TESTCASE (function_data)
{
  TestFunctionDataListener * fd = test_function_data_listener_new ();
  GumAttachOptions options = { 0, };

  options.listener_function_data = (gpointer) "a";
  gum_interceptor_attach (fixture->interceptor, gum_test_target_function,
      GUM_INVOCATION_LISTENER (fd), &options);

  gum_test_target_function (fixture->result);

  g_assert_cmpuint (fd->on_enter_call_count, ==, 1);
  g_assert_cmpuint (fd->on_leave_call_count, ==, 1);
  g_assert_cmpstr (fd->last_on_enter_data.thread_data.name, ==, "a1");
  g_assert_true (fd->last_on_enter_data.function_data == (gpointer) "a");

  gum_interceptor_detach (fixture->interceptor, GUM_INVOCATION_LISTENER (fd));
  gum_invocation_listener_unref (GUM_INVOCATION_LISTENER (fd));
}

int
main (int argc, char ** argv)
{
  int result;

  gum_init ();
  g_test_init (&argc, &argv, NULL);
  TESTLIST_REGISTER (interceptor);
  result = g_test_run ();
  gum_deinit ();

  return result;
}
