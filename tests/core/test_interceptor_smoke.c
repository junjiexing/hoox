/*
 * M5 smoke test: attach an invocation listener to a real function and replace
 * a function, verifying the trampolines fire end-to-end on Windows x64.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gum.h"

#include <stdio.h>

static int g_failures = 0;

#define CHECK(expr) \
    G_STMT_START { \
      if (!(expr)) { \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
      } \
    } G_STMT_END

/* Target function — kept out-of-line so it has a real prologue to hook. */
#ifdef _MSC_VER
# define GUM_NOINLINE __declspec (noinline)
#else
# define GUM_NOINLINE __attribute__ ((noinline))
#endif

GUM_NOINLINE
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
on_enter (GumInvocationContext * ic, gpointer user_data)
{
  Probe * p = user_data;
  (void) ic;
  p->enter_count++;
}

static void
on_leave (GumInvocationContext * ic, gpointer user_data)
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

int
main (void)
{
  GumInterceptor * interceptor;
  GumInvocationListener * listener;
  Probe probe = { 0, 0 };
  GumAttachReturn ar;
  GumReplaceReturn rr;
  int r;

  gum_init ();

  interceptor = gum_interceptor_obtain ();
  CHECK (interceptor != NULL);

  /* ---- attach a listener ---- */
  listener = gum_make_call_listener (on_enter, on_leave, &probe, NULL);
  ar = gum_interceptor_attach (interceptor, (gpointer) target_add, listener,
      NULL);
  CHECK (ar == GUM_ATTACH_OK);

  r = target_add (3, 4);
  CHECK (r == 7);
  CHECK (probe.enter_count == 1);
  CHECK (probe.leave_count == 1);

  r = target_add (10, 20);
  CHECK (r == 30);
  CHECK (probe.enter_count == 2);
  CHECK (probe.leave_count == 2);

  gum_interceptor_detach (interceptor, listener);

  r = target_add (1, 1);
  CHECK (r == 2);
  CHECK (probe.enter_count == 2);   /* no longer firing */

  gum_invocation_listener_unref (listener);

  /* ---- replace a function ---- */
  rr = gum_interceptor_replace (interceptor, (gpointer) target_add,
      (gpointer) replacement_add, (gpointer *) &original_add, NULL);
  CHECK (rr == GUM_REPLACE_OK);

  r = target_add (5, 6);
  CHECK (r == 111);   /* 5 + 6 + 100 */

  gum_interceptor_revert (interceptor, (gpointer) target_add);

  r = target_add (5, 6);
  CHECK (r == 11);    /* back to normal */

  gum_interceptor_unref (interceptor);

  gum_deinit ();

  if (g_failures == 0)
  {
    printf ("interceptor smoke: all tests passed\n");
    return 0;
  }
  printf ("interceptor smoke: %d failure(s)\n", g_failures);
  return 1;
}
