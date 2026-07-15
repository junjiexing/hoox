/*
 * Listener snapshot regression: mutating the listener set from on-enter must
 * not change which listeners receive on-leave, or which per-invocation slot
 * each listener sees.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hoox.h"

#include <stdio.h>

#ifdef _MSC_VER
# define HOOX_NOINLINE __declspec (noinline)
#else
# define HOOX_NOINLINE __attribute__ ((noinline))
#endif

typedef struct _SnapshotProbe SnapshotProbe;

struct _SnapshotProbe
{
  HooxInterceptor * interceptor;
  HooxInvocationListener * listener;
  HooxInvocationListener * replacement;
  hx_pointer target;
  hx_uint magic;
  hx_boolean mutate;
  hx_uint enter_count;
  hx_uint leave_count;
  hx_uint mismatches;
};

static int failures;

#define CHECK(expr) \
    do { \
      if (!(expr)) { \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
      } \
    } while (0)

HOOX_NOINLINE
static int
target_add (int a,
            int b)
{
  return a + b;
}

static void
on_enter (HooxInvocationContext * context,
          hx_pointer user_data)
{
  SnapshotProbe * probe = user_data;
  hx_uint * invocation_data;

  invocation_data = hoox_invocation_context_get_listener_invocation_data (
      context, sizeof (hx_uint));
  *invocation_data = probe->magic;
  probe->enter_count++;

  if (probe->mutate)
  {
    HooxAttachReturn result;

    probe->mutate = 0;
    hoox_interceptor_detach (probe->interceptor, probe->listener);
    result = hoox_interceptor_attach (probe->interceptor, probe->target,
        probe->replacement, NULL);
    CHECK (result == HOOX_ATTACH_OK);
  }
}

static void
on_leave (HooxInvocationContext * context,
          hx_pointer user_data)
{
  SnapshotProbe * probe = user_data;
  hx_uint * invocation_data;

  invocation_data = hoox_invocation_context_get_listener_invocation_data (
      context, sizeof (hx_uint));
  probe->leave_count++;
  if (*invocation_data != probe->magic)
    probe->mismatches++;
}

int
main (void)
{
  HooxInterceptor * interceptor;
  HooxInvocationListener * first_listener, * second_listener;
  HooxInvocationListener * replacement_listener;
  SnapshotProbe first = { 0, }, second = { 0, }, replacement = { 0, };

  hoox_init ();
  interceptor = hoox_interceptor_obtain ();

  first.interceptor = interceptor;
  first.target = (hx_pointer) target_add;
  first.magic = 0x11111111;
  first.mutate = 1;
  second.interceptor = interceptor;
  second.target = (hx_pointer) target_add;
  second.magic = 0x22222222;
  replacement.interceptor = interceptor;
  replacement.target = (hx_pointer) target_add;
  replacement.magic = 0x33333333;

  first_listener = hoox_make_call_listener (on_enter, on_leave, &first, NULL);
  second_listener = hoox_make_call_listener (on_enter, on_leave, &second, NULL);
  replacement_listener = hoox_make_call_listener (on_enter, on_leave,
      &replacement, NULL);
  first.listener = first_listener;
  first.replacement = replacement_listener;
  second.listener = second_listener;
  replacement.listener = replacement_listener;

  CHECK (hoox_interceptor_attach (interceptor, (hx_pointer) target_add,
      first_listener, NULL) == HOOX_ATTACH_OK);
  CHECK (hoox_interceptor_attach (interceptor, (hx_pointer) target_add,
      second_listener, NULL) == HOOX_ATTACH_OK);

  CHECK (target_add (19, 23) == 42);
  CHECK (first.enter_count == 1);
  CHECK (first.leave_count == 1);
  CHECK (first.mismatches == 0);
  CHECK (second.enter_count == 1);
  CHECK (second.leave_count == 1);
  CHECK (second.mismatches == 0);
  CHECK (replacement.enter_count == 0);
  CHECK (replacement.leave_count == 0);

  hoox_interceptor_detach (interceptor, second_listener);
  hoox_interceptor_detach (interceptor, replacement_listener);
  CHECK (hoox_interceptor_flush (interceptor));

  hoox_invocation_listener_unref (replacement_listener);
  hoox_invocation_listener_unref (second_listener);
  hoox_invocation_listener_unref (first_listener);
  hoox_interceptor_unref (interceptor);
  hoox_deinit ();

  if (failures != 0)
    return 1;

  printf ("interceptor listener snapshot: all tests passed\n");
  return 0;
}
