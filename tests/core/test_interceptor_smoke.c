/*
 * M5 smoke test: attach an invocation listener to a real function and replace
 * a function, verifying the trampolines fire end-to-end on Windows x64.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hoox.h"

#include <stdio.h>

static int hx_failures = 0;

#define CHECK(expr) \
    HX_STMT_START { \
      if (!(expr)) { \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        hx_failures++; \
      } \
    } HX_STMT_END

/* Target function — kept out-of-line so it has a real prologue to hook. */
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

/* Listener state. */
typedef struct
{
  int enter_count;
  int leave_count;
} Probe;

static void
on_enter (HooxInvocationContext * ic, hx_pointer user_data)
{
  Probe * p = user_data;
  (void) ic;
  p->enter_count++;
}

static void
on_leave (HooxInvocationContext * ic, hx_pointer user_data)
{
  Probe * p = user_data;
  (void) ic;
  p->leave_count++;
}

/* Replacement for target_add. */
typedef int (* AddFunc) (int, int);
static AddFunc original_add = NULL;

static int
replacement_add (int a, int b)
{
  return original_add (a, b) + 100;
}

static void
run_cycle (void)
{
  /*
   * On Apple arm64 (16 KiB pages, W^X) this tiny statically-linked binary places
   * target_add on the same page as hoox's own patch code — the self-hosting
   * collision. hoox patches from an off-page stub (backend/darwin/hooxpatch-darwin.c)
   * so the target page losing execute during the write can't fault the patcher,
   * so this runs here too.
   */
  HooxInterceptor * interceptor;
  HooxInvocationListener * listener;
  Probe probe = { 0, 0 };
  HooxAttachReturn ar;
  HooxReplaceReturn rr;
  int r;

  hoox_init ();

  interceptor = hoox_interceptor_obtain ();
  CHECK (interceptor != NULL);

  /* ---- attach a listener ---- */
  listener = hoox_make_call_listener (on_enter, on_leave, &probe, NULL);
  ar = hoox_interceptor_attach (interceptor, (hx_pointer) target_add, listener,
      NULL);
  CHECK (ar == HOOX_ATTACH_OK);

  r = target_add (3, 4);
  CHECK (r == 7);
  CHECK (probe.enter_count == 1);
  CHECK (probe.leave_count == 1);

  r = target_add (10, 20);
  CHECK (r == 30);
  CHECK (probe.enter_count == 2);
  CHECK (probe.leave_count == 2);

  hoox_interceptor_detach (interceptor, listener);

  r = target_add (1, 1);
  CHECK (r == 2);
  CHECK (probe.enter_count == 2);   /* no longer firing */

  hoox_invocation_listener_unref (listener);

  /* ---- replace a function ---- */
  rr = hoox_interceptor_replace (interceptor, (hx_pointer) target_add,
      (hx_pointer) replacement_add, (hx_pointer *) &original_add, NULL);
  CHECK (rr == HOOX_REPLACE_OK);

  r = target_add (5, 6);
  CHECK (r == 111);   /* 5 + 6 + 100 */

  hoox_interceptor_revert (interceptor, (hx_pointer) target_add);

  r = target_add (5, 6);
  CHECK (r == 11);    /* back to normal */

  hoox_interceptor_unref (interceptor);

  hoox_deinit ();
}

int
main (void)
{
  run_cycle ();
  run_cycle ();

  if (hx_failures == 0)
  {
    printf ("interceptor smoke: all tests passed\n");
    return 0;
  }
  printf ("interceptor smoke: %d failure(s)\n", hx_failures);
  return 1;
}
