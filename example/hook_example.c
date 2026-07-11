/*
 * hoox — end-to-end example.
 *
 * Demonstrates the two ways hoox lets you intercept a function, using nothing
 * but the amalgamated single-file library (hoox.c / hoox.h):
 *
 *   1. attach  — wrap a function with an invocation listener that observes
 *                (and may tamper with) the call on the way in and out, while
 *                the original body still runs.
 *   2. replace — swap the function body entirely for your own, keeping a
 *                pointer to a trampoline that still reaches the original.
 *
 * Build with the accompanying CMakeLists.txt, or directly:
 *
 *   clang -DGUM_STATIC -DGUM_USE_SYSTEM_ALLOC -DHAVE_I386 -DHAVE_WINDOWS \
 *         hoox.c hook_example.c -lpsapi -o hook_example      (Windows x64)
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hoox.h"

#include <stdio.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- *
 * Target function.
 *
 * Kept out-of-line (noinline) so it has a genuine prologue for hoox to
 * overwrite with a trampoline. In a real program this could be any function
 * in your own code, another module, or a system API.
 * ------------------------------------------------------------------------- */
#if defined (_MSC_VER)
# define NOINLINE __declspec (noinline)
#else
# define NOINLINE __attribute__ ((noinline))
#endif

NOINLINE
static int
compute (int a, int b)
{
  return a + b;
}

/* ========================================================================= *
 * Part 1 — attach an invocation listener.
 *
 * The listener sees every call. on_enter fires before the original body,
 * on_leave after it. We read the arguments, then bump the return value by 1
 * to prove we can tamper with results without touching `compute` itself.
 * ========================================================================= */

typedef struct
{
  int calls;
} ListenerState;

static void
on_enter (GumInvocationContext * ic, gpointer user_data)
{
  ListenerState * st = user_data;
  int a = (int) (intptr_t) gum_invocation_context_get_nth_argument (ic, 0);
  int b = (int) (intptr_t) gum_invocation_context_get_nth_argument (ic, 1);

  st->calls++;
  printf ("  [listener] enter: compute(%d, %d)  thread=%u depth=%u\n",
      a, b, gum_invocation_context_get_thread_id (ic),
      gum_invocation_context_get_depth (ic));
}

static void
on_leave (GumInvocationContext * ic, gpointer user_data)
{
  int retval = (int) (intptr_t) gum_invocation_context_get_return_value (ic);

  (void) user_data;
  printf ("  [listener] leave: original returned %d, tampering -> %d\n",
      retval, retval + 1);

  gum_invocation_context_replace_return_value (ic,
      (gpointer) (intptr_t) (retval + 1));
}

/* ========================================================================= *
 * Part 2 — replace the function body.
 *
 * `original_compute` is filled in with a trampoline that still runs the real
 * `compute`, so the replacement can decorate the original behaviour.
 * ========================================================================= */

typedef int (* ComputeFunc) (int, int);
static ComputeFunc original_compute = NULL;

static int
replacement_compute (int a, int b)
{
  int base = original_compute (a, b);
  printf ("  [replace] intercepted compute(%d, %d); original=%d, returning %d\n",
      a, b, base, base * 10);
  return base * 10;
}

/* ========================================================================= */

int
main (void)
{
  GumInterceptor * interceptor;
  GumInvocationListener * listener;
  ListenerState state = { 0 };
  GumAttachReturn ar;
  GumReplaceReturn rr;
  int r;

  /* Bring the library up. */
  gum_init ();
  interceptor = gum_interceptor_obtain ();

  printf ("baseline: compute(3, 4) = %d\n\n", compute (3, 4));

  /* ---- Part 1: attach a listener ------------------------------------- */
  printf ("== attach listener ==\n");
  listener = gum_make_call_listener (on_enter, on_leave, &state, NULL);

  ar = gum_interceptor_attach (interceptor, (gpointer) compute, listener, NULL);
  if (ar != GUM_ATTACH_OK)
  {
    fprintf (stderr, "attach failed: %d\n", ar);
    return 1;
  }

  r = compute (3, 4);
  printf ("caller sees: compute(3, 4) = %d  (7 from body, +1 from listener)\n",
      r);
  r = compute (10, 20);
  printf ("caller sees: compute(10, 20) = %d\n", r);
  printf ("listener observed %d call(s)\n\n", state.calls);

  gum_interceptor_detach (interceptor, listener);
  gum_invocation_listener_unref (listener);

  printf ("after detach: compute(3, 4) = %d  (back to normal)\n\n",
      compute (3, 4));

  /* ---- Part 2: replace the function ---------------------------------- */
  printf ("== replace function ==\n");
  rr = gum_interceptor_replace (interceptor, (gpointer) compute,
      (gpointer) replacement_compute, (gpointer *) &original_compute, NULL);
  if (rr != GUM_REPLACE_OK)
  {
    fprintf (stderr, "replace failed: %d\n", rr);
    return 1;
  }

  r = compute (5, 6);
  printf ("caller sees: compute(5, 6) = %d  ((5+6) * 10)\n\n", r);

  gum_interceptor_revert (interceptor, (gpointer) compute);
  printf ("after revert: compute(5, 6) = %d  (back to normal)\n", compute (5, 6));

  /* Tear down. */
  gum_interceptor_unref (interceptor);
  gum_deinit ();

  printf ("\ndone.\n");
  return 0;
}
