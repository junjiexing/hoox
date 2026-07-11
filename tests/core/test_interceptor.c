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

#include "hoox.h"
#include "interceptor-callbacklistener.h"
#include "interceptor-functiondatalistener.h"

#include "testrunner.h"

#include <string.h>

extern hx_pointer hoox_test_target_function (HxString * str);
extern hx_pointer hoox_test_target_nop_function_a (hx_pointer data);
extern hx_pointer hoox_test_target_nop_function_b (hx_pointer data);

/* ---- fixture ------------------------------------------------------------ */

typedef struct _TestInterceptorFixture TestInterceptorFixture;
typedef struct _ListenerContext ListenerContext;

struct _ListenerContext
{
  TestCallbackListener * listener;
  TestInterceptorFixture * fixture;
  hx_char enter_char;
  hx_char leave_char;
  hx_size last_seen_argument;
  hx_pointer last_return_value;
};

struct _TestInterceptorFixture
{
  HooxInterceptor * interceptor;
  HxString * result;
  ListenerContext * listener_context[2];
};

static void
listener_context_on_enter (ListenerContext * self,
                           HooxInvocationContext * context)
{
  hx_string_append_c (self->fixture->result, self->enter_char);
  self->last_seen_argument = (hx_size)
      hoox_invocation_context_get_nth_argument (context, 0);
}

static void
listener_context_on_leave (ListenerContext * self,
                           HooxInvocationContext * context)
{
  hx_string_append_c (self->fixture->result, self->leave_char);
  self->last_return_value =
      hoox_invocation_context_get_return_value (context);
}

static void
listener_context_free (ListenerContext * ctx)
{
  hoox_invocation_listener_unref (HOOX_INVOCATION_LISTENER (ctx->listener));
  hx_free (ctx);
}

static HooxAttachReturn
fixture_attach (TestInterceptorFixture * h,
                hx_uint listener_index,
                hx_pointer test_func,
                hx_char enter_char,
                hx_char leave_char)
{
  HooxAttachReturn result;
  ListenerContext * ctx;

  ctx = h->listener_context[listener_index];
  if (ctx != NULL)
  {
    listener_context_free (ctx);
    h->listener_context[listener_index] = NULL;
  }

  ctx = hx_new0 (ListenerContext, 1);
  ctx->listener = test_callback_listener_new ();
  ctx->listener->on_enter = (TestCallbackListenerFunc) listener_context_on_enter;
  ctx->listener->on_leave = (TestCallbackListenerFunc) listener_context_on_leave;
  ctx->listener->user_data = ctx;
  ctx->fixture = h;
  ctx->enter_char = enter_char;
  ctx->leave_char = leave_char;

  result = hoox_interceptor_attach (h->interceptor, test_func,
      HOOX_INVOCATION_LISTENER (ctx->listener), NULL);
  if (result == HOOX_ATTACH_OK)
    h->listener_context[listener_index] = ctx;
  else
    listener_context_free (ctx);

  return result;
}

static void
test_interceptor_fixture_setup (TestInterceptorFixture * fixture,
                                hx_constpointer data)
{
  (void) data;
  fixture->interceptor = hoox_interceptor_obtain ();
  fixture->result = hx_string_sized_new (256);
  memset (&fixture->listener_context, 0, sizeof (fixture->listener_context));
}

static void
test_interceptor_fixture_teardown (TestInterceptorFixture * fixture,
                                   hx_constpointer data)
{
  hx_uint i;
  (void) data;

  for (i = 0; i != HX_N_ELEMENTS (fixture->listener_context); i++)
  {
    ListenerContext * ctx = fixture->listener_context[i];
    if (ctx != NULL)
    {
      hoox_interceptor_detach (fixture->interceptor,
          HOOX_INVOCATION_LISTENER (ctx->listener));
      listener_context_free (ctx);
    }
  }

  hx_string_free (fixture->result, TRUE);
  hoox_interceptor_unref (fixture->interceptor);
}

#define TESTCASE(NAME) \
    void test_interceptor_ ## NAME ( \
        TestInterceptorFixture * fixture, hx_constpointer data)
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
  fixture_attach (fixture, 0, hoox_test_target_function, '>', '<');
  hoox_test_target_function (fixture->result);
  hx_assert_cmpstr (fixture->result->str, ==, ">|<");
}

TESTCASE (attach_two)
{
  fixture_attach (fixture, 0, hoox_test_target_function, 'a', 'A');
  fixture_attach (fixture, 1, hoox_test_target_function, 'b', 'B');
  hoox_test_target_function (fixture->result);
  hx_assert_cmpstr (fixture->result->str, ==, "ab|AB");
}

TESTCASE (detach)
{
  fixture_attach (fixture, 0, hoox_test_target_function, '>', '<');
  fixture_attach (fixture, 1, hoox_test_target_function, 'a', 'A');

  hoox_test_target_function (fixture->result);
  hx_assert_cmpstr (fixture->result->str, ==, ">a|<A");

  hoox_interceptor_detach (fixture->interceptor,
      HOOX_INVOCATION_LISTENER (fixture->listener_context[0]->listener));

  hx_string_truncate (fixture->result, 0);
  hoox_test_target_function (fixture->result);
  hx_assert_cmpstr (fixture->result->str, ==, "a|A");
}

TESTCASE (already_attached)
{
  HooxAttachReturn r;

  fixture_attach (fixture, 0, hoox_test_target_function, '>', '<');

  r = hoox_interceptor_attach (fixture->interceptor, hoox_test_target_function,
      HOOX_INVOCATION_LISTENER (fixture->listener_context[0]->listener), NULL);
  hx_assert_cmpint (r, ==, HOOX_ATTACH_ALREADY_ATTACHED);
}

TESTCASE (function_arguments)
{
  fixture_attach (fixture, 0, hoox_test_target_nop_function_a, '>', '<');
  hoox_test_target_nop_function_a (HX_SIZE_TO_POINTER (0x12345678));
  hx_assert_cmphex (fixture->listener_context[0]->last_seen_argument, ==,
      0x12345678);
}

TESTCASE (function_return_value)
{
  hx_pointer ret;
  fixture_attach (fixture, 0, hoox_test_target_nop_function_a, '>', '<');
  ret = hoox_test_target_nop_function_a (NULL);
  hx_assert_cmphex (HX_POINTER_TO_SIZE (fixture->listener_context[0]->
      last_return_value), ==, HX_POINTER_TO_SIZE (ret));
  hx_assert_cmphex (HX_POINTER_TO_SIZE (ret), ==, 0x1337);
}

/* ---- replace ------------------------------------------------------------ */

static hx_pointer (* replace_nop_a_orig) (hx_pointer) = NULL;

static hx_pointer
replace_nop_a (hx_pointer data)
{
  (void) data;
  return HX_SIZE_TO_POINTER (0xbeef);
}

static hx_pointer
replace_nop_a_calling_original (hx_pointer data)
{
  hx_size orig = HX_POINTER_TO_SIZE (replace_nop_a_orig (data));
  return HX_SIZE_TO_POINTER (orig + 1);
}

TESTCASE (replace_one)
{
  hx_pointer ret;
  HooxReplaceReturn rr;

  rr = hoox_interceptor_replace (fixture->interceptor,
      hoox_test_target_nop_function_a, replace_nop_a, NULL, NULL);
  hx_assert_cmpint (rr, ==, HOOX_REPLACE_OK);

  ret = hoox_test_target_nop_function_a (NULL);
  hx_assert_cmphex (HX_POINTER_TO_SIZE (ret), ==, 0xbeef);

  hoox_interceptor_revert (fixture->interceptor, hoox_test_target_nop_function_a);
  ret = hoox_test_target_nop_function_a (NULL);
  hx_assert_cmphex (HX_POINTER_TO_SIZE (ret), ==, 0x1337);
}

TESTCASE (replace_keep_original)
{
  hx_pointer ret;

  hoox_interceptor_replace (fixture->interceptor, hoox_test_target_nop_function_a,
      replace_nop_a_calling_original, (hx_pointer *) &replace_nop_a_orig, NULL);

  ret = hoox_test_target_nop_function_a (NULL);
  hx_assert_cmphex (HX_POINTER_TO_SIZE (ret), ==, 0x1338); /* 0x1337 + 1 */

  hoox_interceptor_revert (fixture->interceptor, hoox_test_target_nop_function_a);
}

TESTCASE (replace_then_attach)
{
  /* Attach a listener, then replace a different function; both must work. */
  fixture_attach (fixture, 0, hoox_test_target_function, '>', '<');

  hoox_interceptor_replace (fixture->interceptor, hoox_test_target_nop_function_b,
      replace_nop_a, NULL, NULL);

  hoox_test_target_function (fixture->result);
  hx_assert_cmpstr (fixture->result->str, ==, ">|<");

  hx_assert_cmphex (HX_POINTER_TO_SIZE (hoox_test_target_nop_function_b (NULL)), ==,
      0xbeef);

  hoox_interceptor_revert (fixture->interceptor, hoox_test_target_nop_function_b);
}

TESTCASE (function_data)
{
  TestFunctionDataListener * fd = test_function_data_listener_new ();
  HooxAttachOptions options = { 0, };

  options.listener_function_data = (hx_pointer) "a";
  hoox_interceptor_attach (fixture->interceptor, hoox_test_target_function,
      HOOX_INVOCATION_LISTENER (fd), &options);

  hoox_test_target_function (fixture->result);

  hx_assert_cmpuint (fd->on_enter_call_count, ==, 1);
  hx_assert_cmpuint (fd->on_leave_call_count, ==, 1);
  hx_assert_cmpstr (fd->last_on_enter_data.thread_data.name, ==, "a1");
  hx_assert_true (fd->last_on_enter_data.function_data == (hx_pointer) "a");

  hoox_interceptor_detach (fixture->interceptor, HOOX_INVOCATION_LISTENER (fd));
  hoox_invocation_listener_unref (HOOX_INVOCATION_LISTENER (fd));
}

int
main (int argc, char ** argv)
{
  int result;

  hoox_init ();
  hx_test_init (&argc, &argv, NULL);
  TESTLIST_REGISTER (interceptor);
  result = hx_test_run ();
  hoox_deinit ();

  return result;
}
