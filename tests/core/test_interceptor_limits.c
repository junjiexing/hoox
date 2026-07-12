/*
 * Listener-count limit test: attaching more than HOOX_MAX_LISTENERS_PER_FUNCTION
 * distinct listeners to one target must be refused with
 * HOOX_ATTACH_TOO_MANY_LISTENERS rather than overflowing the fixed
 * per-invocation listener_invocation_data[] array. Also checks that a detach
 * frees a slot so a later attach succeeds again (the count is of live entries,
 * not raw array length).
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hoox.h"

#include "hooxdefs.h"

#include <stdio.h>

static int hx_failures = 0;

#define CHECK(expr) \
    HX_STMT_START { \
      if (!(expr)) { \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        hx_failures++; \
      } \
    } HX_STMT_END

#ifdef _MSC_VER
# define HOOX_NOINLINE __declspec (noinline)
#else
# define HOOX_NOINLINE __attribute__ ((noinline))
#endif

HOOX_NOINLINE
static int
target_add (int a, int b)
{
  return a + b;
}

static void
on_enter (HooxInvocationContext * ic, hx_pointer user_data)
{
  (void) ic;
  (void) user_data;
}

int
main (void)
{
  HooxInterceptor * interceptor;
  HooxInvocationListener * listeners[HOOX_MAX_LISTENERS_PER_FUNCTION + 1];
  HooxInvocationListener * extra;
  HooxAttachReturn ar;
  int i;

  hoox_init ();

  interceptor = hoox_interceptor_obtain ();
  CHECK (interceptor != NULL);

  /* Fill exactly up to the limit — every attach must succeed. */
  for (i = 0; i != HOOX_MAX_LISTENERS_PER_FUNCTION; i++)
  {
    listeners[i] = hoox_make_call_listener (on_enter, NULL, NULL, NULL);
    ar = hoox_interceptor_attach (interceptor, (hx_pointer) target_add,
        listeners[i], NULL);
    CHECK (ar == HOOX_ATTACH_OK);
  }

  /* One past the limit — must be refused, not silently overflow. */
  extra = hoox_make_call_listener (on_enter, NULL, NULL, NULL);
  ar = hoox_interceptor_attach (interceptor, (hx_pointer) target_add, extra,
      NULL);
  CHECK (ar == HOOX_ATTACH_TOO_MANY_LISTENERS);

  /* Free a slot; the same over-the-limit listener should now fit (the count is
   * of live entries, so a detach makes room). */
  hoox_interceptor_detach (interceptor, listeners[0]);
  hoox_invocation_listener_unref (listeners[0]);

  ar = hoox_interceptor_attach (interceptor, (hx_pointer) target_add, extra,
      NULL);
  CHECK (ar == HOOX_ATTACH_OK);

  /* The function still fires correctly with a full complement of listeners. */
  CHECK (target_add (2, 3) == 5);

  hoox_interceptor_detach (interceptor, extra);
  hoox_invocation_listener_unref (extra);
  for (i = 1; i != HOOX_MAX_LISTENERS_PER_FUNCTION; i++)
  {
    hoox_interceptor_detach (interceptor, listeners[i]);
    hoox_invocation_listener_unref (listeners[i]);
  }

  hoox_interceptor_unref (interceptor);

  hoox_deinit ();

  if (hx_failures == 0)
  {
    printf ("interceptor limits: all tests passed\n");
    return 0;
  }
  printf ("interceptor limits: %d failure(s)\n", hx_failures);
  return 1;
}
