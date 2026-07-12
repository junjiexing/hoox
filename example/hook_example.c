/*
 * hoox — end-to-end example / API tour.
 *
 * Exercises the public surface of the amalgamated single-file library
 * (hoox.c / hoox.h):
 *
 *   1. attach          — a call listener that reads/rewrites arguments and the
 *                        return value (get/replace_nth_argument,
 *                        get/replace_return_value, get_return_address,
 *                        get_thread_id, get_depth).
 *   2. custom listener — implement the HooxInvocationListener vtable directly,
 *                        branch on get_point_cut, and use the three data slots
 *                        (function / thread / per-invocation).
 *   3. probe listener  — a lightweight enter-only probe (make_probe_listener).
 *   4. transaction     — batch several attaches so they activate as a unit
 *                        (begin_transaction / end_transaction).
 *   5. replace         — swap the body, reach the original via a trampoline,
 *                        and read replacement_data via get_current_invocation.
 *   6. replace_fast    — the lighter, direct replacement variant.
 *   7. ignore thread   — silence a hook on the current thread
 *                        (ignore/unignore_current_thread).
 *
 * Build with the accompanying CMakeLists.txt, or directly — no feature macros
 * are needed (static linkage, the system allocator, and the target arch/OS are
 * all defaults; MSVC, clang and gcc are supported):
 *
 *   clang hoox.c hook_example.c -lpsapi -o hook_example       (Windows x64)
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hoox.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------- *
 * Target functions.
 *
 * Kept out-of-line (noinline) so each has a genuine prologue for hoox to
 * overwrite with a trampoline. In a real program these could be any functions
 * in your own code, another module, or a system API.
 * ------------------------------------------------------------------------- */
#if defined (_MSC_VER)
# define NOINLINE __declspec (noinline)
#else
# define NOINLINE __attribute__ ((noinline))
#endif

NOINLINE static int
compute (int a, int b)
{
  return a + b;
}

NOINLINE static int
mul (int a, int b)
{
  return a * b;
}

/* ========================================================================= *
 * Part 1 — attach a call listener.
 *
 * on_enter fires before the original body, on_leave after it. Here we rewrite
 * an argument on the way in and the return value on the way out, without
 * touching `compute` itself.
 * ========================================================================= */

typedef struct
{
  int calls;
} ListenerState;

static void
on_enter (HooxInvocationContext * ic, hx_pointer user_data)
{
  ListenerState * st = user_data;
  int a = (int) (intptr_t) hoox_invocation_context_get_nth_argument (ic, 0);
  int b = (int) (intptr_t) hoox_invocation_context_get_nth_argument (ic, 1);

  st->calls++;

  /* Rewrite the 2nd argument: the body will compute a + (b * 10). */
  hoox_invocation_context_replace_nth_argument (ic, 1,
      (hx_pointer) (intptr_t) (b * 10));

  printf ("  [listener] enter: compute(%d, %d) -> forcing b=%d  "
      "thread=%u depth=%u ret_addr=%p\n",
      a, b, b * 10,
      hoox_invocation_context_get_thread_id (ic),
      hoox_invocation_context_get_depth (ic),
      hoox_invocation_context_get_return_address (ic));
}

static void
on_leave (HooxInvocationContext * ic, hx_pointer user_data)
{
  int retval = (int) (intptr_t) hoox_invocation_context_get_return_value (ic);

  (void) user_data;
  printf ("  [listener] leave: body returned %d, bumping -> %d\n",
      retval, retval + 1);

  hoox_invocation_context_replace_return_value (ic,
      (hx_pointer) (intptr_t) (retval + 1));
}

static void
demo_attach (HooxInterceptor * interceptor)
{
  HooxInvocationListener * listener;
  ListenerState state = { 0 };
  int r;

  printf ("== 1. attach a call listener ==\n");
  listener = hoox_make_call_listener (on_enter, on_leave, &state, NULL);
  hoox_interceptor_attach (interceptor, (hx_pointer) compute, listener, NULL);

  r = compute (3, 4);
  printf ("caller sees: compute(3, 4) = %d  (3 + 4*10 = 43, +1 = 44)\n", r);
  r = compute (10, 2);
  printf ("caller sees: compute(10, 2) = %d\n", r);
  printf ("listener observed %d call(s)\n\n", state.calls);

  hoox_interceptor_detach (interceptor, listener);
  hoox_invocation_listener_unref (listener);
}

/* ========================================================================= *
 * Part 2 — a custom listener (implement the vtable directly).
 *
 * One handler serves both enter and leave; it branches on get_point_cut. It
 * also uses all three data slots:
 *   - function data:   set per-attach via HooxAttachOptions (a label here);
 *   - thread data:     per-(listener, thread) scratch — a call counter;
 *   - invocation data: per-call scratch shared between enter and leave.
 * ========================================================================= */

typedef struct
{
  HooxInvocationListener parent;   /* must be the first member */
  int cumulative;
} TapListener;

static void
tap_handler (HooxInvocationListener * base, HooxInvocationContext * ic)
{
  TapListener * self = (TapListener *) base;

  if (hoox_invocation_context_get_point_cut (ic) == HOOX_POINT_ENTER)
  {
    int a = (int) (intptr_t) hoox_invocation_context_get_nth_argument (ic, 0);
    int b = (int) (intptr_t) hoox_invocation_context_get_nth_argument (ic, 1);
    const char * tag =
        hoox_invocation_context_get_listener_function_data (ic);
    int * per_call =
        hoox_invocation_context_get_listener_invocation_data (ic, sizeof (int));
    int * per_thread =
        hoox_invocation_context_get_listener_thread_data (ic, sizeof (int));

    *per_call = a + b;          /* stash for on_leave */
    (*per_thread)++;

    printf ("  [tap:%s] enter a=%d b=%d  (call #%d on this thread)\n",
        tag, a, b, *per_thread);
  }
  else
  {
    int * per_call =
        hoox_invocation_context_get_listener_invocation_data (ic, sizeof (int));
    int ret = (int) (intptr_t) hoox_invocation_context_get_return_value (ic);

    self->cumulative += *per_call;
    printf ("  [tap] leave: a+b(from enter)=%d result=%d cumulative=%d\n",
        *per_call, ret, self->cumulative);
  }
}

static const HooxInvocationListenerInterface tap_iface =
{
  tap_handler,   /* on_enter */
  tap_handler    /* on_leave */
};

static void
demo_custom_listener (HooxInterceptor * interceptor)
{
  TapListener * tap;
  HooxAttachOptions options = { 0, };

  printf ("== 2. custom listener + data slots + point-cut ==\n");

  tap = calloc (1, sizeof (TapListener));     /* freed by hoox on unref */
  hoox_invocation_listener_init (&tap->parent, &tap_iface, NULL);

  options.listener_function_data = (hx_pointer) "compute";
  hoox_interceptor_attach (interceptor, (hx_pointer) compute,
      HOOX_INVOCATION_LISTENER (tap), &options);

  compute (2, 5);
  compute (100, 1);

  hoox_interceptor_detach (interceptor, HOOX_INVOCATION_LISTENER (tap));
  hoox_invocation_listener_unref (HOOX_INVOCATION_LISTENER (tap));
  printf ("\n");
}

/* ========================================================================= *
 * Part 3 — a probe listener (enter-only, no on_leave).
 * ========================================================================= */

static void
on_hit (HooxInvocationContext * ic, hx_pointer user_data)
{
  (void) ic;
  (*(int *) user_data)++;
}

static void
demo_probe (HooxInterceptor * interceptor)
{
  HooxInvocationListener * probe;
  int hits = 0;

  printf ("== 3. probe listener (counts calls) ==\n");
  probe = hoox_make_probe_listener (on_hit, &hits, NULL);
  hoox_interceptor_attach (interceptor, (hx_pointer) mul, probe, NULL);

  mul (2, 3);
  mul (4, 5);
  mul (6, 7);
  printf ("mul was called %d time(s); results still correct (e.g. 6*7=%d)\n\n",
      hits, mul (6, 7));

  hoox_interceptor_detach (interceptor, probe);
  hoox_invocation_listener_unref (probe);
}

/* ========================================================================= *
 * Part 4 — batch attaches in a transaction.
 *
 * Between begin/end the changes are staged; end_transaction activates them
 * together (faster, and atomic from the target's point of view).
 * ========================================================================= */

static void
demo_transaction (HooxInterceptor * interceptor)
{
  HooxInvocationListener * p1;
  HooxInvocationListener * p2;
  int hits = 0;

  printf ("== 4. transaction (batch two attaches) ==\n");
  p1 = hoox_make_probe_listener (on_hit, &hits, NULL);
  p2 = hoox_make_probe_listener (on_hit, &hits, NULL);

  hoox_interceptor_begin_transaction (interceptor);
  hoox_interceptor_attach (interceptor, (hx_pointer) compute, p1, NULL);
  hoox_interceptor_attach (interceptor, (hx_pointer) mul, p2, NULL);
  hoox_interceptor_end_transaction (interceptor);   /* both go live here */

  compute (1, 1);
  mul (1, 1);
  printf ("two hooks installed as one transaction; combined hits = %d\n\n",
      hits);

  hoox_interceptor_detach (interceptor, p1);
  hoox_interceptor_detach (interceptor, p2);
  hoox_invocation_listener_unref (p1);
  hoox_invocation_listener_unref (p2);
}

/* ========================================================================= *
 * Part 5 — replace the function body (with replacement_data).
 * ========================================================================= */

typedef int (* ComputeFunc) (int, int);
static ComputeFunc original_compute = NULL;

static int
replacement_compute (int a, int b)
{
  HooxInvocationContext * ic = hoox_interceptor_get_current_invocation ();
  const char * tag = (ic != NULL)
      ? hoox_invocation_context_get_replacement_data (ic)
      : "?";
  int base = original_compute (a, b);

  printf ("  [replace:%s] compute(%d, %d): original=%d, returning %d\n",
      tag, a, b, base, base * 10);
  return base * 10;
}

static void
demo_replace (HooxInterceptor * interceptor)
{
  HooxReplaceOptions options = { 0, };

  printf ("== 5. replace function body ==\n");
  options.replacement_data = (hx_pointer) "v1";
  hoox_interceptor_replace (interceptor, (hx_pointer) compute,
      (hx_pointer) replacement_compute, (hx_pointer *) &original_compute,
      &options);

  printf ("caller sees: compute(5, 6) = %d  ((5+6) * 10)\n", compute (5, 6));

  hoox_interceptor_revert (interceptor, (hx_pointer) compute);
  printf ("after revert: compute(5, 6) = %d  (back to normal)\n\n",
      compute (5, 6));
}

/* ========================================================================= *
 * Part 6 — replace_fast (lightweight direct replacement).
 * ========================================================================= */

typedef int (* MulFunc) (int, int);
static MulFunc original_mul = NULL;

static int
replacement_mul (int a, int b)
{
  return original_mul (a, b) + 1;   /* off-by-one on purpose, to show effect */
}

static void
demo_replace_fast (HooxInterceptor * interceptor)
{
  printf ("== 6. replace_fast ==\n");
  hoox_interceptor_replace_fast (interceptor, (hx_pointer) mul,
      (hx_pointer) replacement_mul, (hx_pointer *) &original_mul, NULL);

  printf ("caller sees: mul(6, 7) = %d  (42 + 1)\n", mul (6, 7));

  hoox_interceptor_revert (interceptor, (hx_pointer) mul);
  printf ("after revert: mul(6, 7) = %d\n\n", mul (6, 7));
}

/* ========================================================================= *
 * Part 7 — ignore the current thread.
 *
 * A hook stays installed, but its listener is skipped for calls made on a
 * thread that has been marked ignored (useful to avoid recursion when your
 * own callback calls the hooked function).
 * ========================================================================= */

static void
demo_ignore_thread (HooxInterceptor * interceptor)
{
  HooxInvocationListener * probe;
  int hits = 0;

  printf ("== 7. ignore current thread ==\n");
  probe = hoox_make_probe_listener (on_hit, &hits, NULL);
  hoox_interceptor_attach (interceptor, (hx_pointer) compute, probe, NULL);

  hoox_interceptor_ignore_current_thread (interceptor);
  compute (1, 2);                 /* listener skipped */
  hoox_interceptor_unignore_current_thread (interceptor);
  compute (1, 2);                 /* listener fires */

  printf ("hits with one ignored + one observed call = %d (expected 1)\n\n",
      hits);

  hoox_interceptor_detach (interceptor, probe);
  hoox_invocation_listener_unref (probe);
}

/* ========================================================================= *
 * Part 8 — read the CPU register context, and probe an arbitrary instruction.
 *
 * A listener receives HooxInvocationContext.cpu_context: the register state
 * live at the hooked instruction. The layout is public (HooxCpuContext), so you
 * can read (and write) registers directly. And a probe can target ANY
 * instruction address, not only a function entry — the frida-style inline hook.
 * ========================================================================= */

/* Straight-line leaf: on arm64 any +4 offset into it is a valid boundary. */
static int
straight (int a, int b)
{
  int s = a + b;
  s = s * 3;
  s = s ^ 0x55;
  return s;
}

static void
dump_context (const char * where, HooxCpuContext * c)
{
#if defined (_M_IX86) || defined (__i386__)
  printf ("  [ctx@%s] eip=%08x esp=%08x eax=%08x ecx=%08x\n",
      where, c->eip, c->esp, c->eax, c->ecx);
#elif defined (_M_X64) || defined (__x86_64__)
  printf ("  [ctx@%s] rip=%llx rsp=%llx rdi=%lld rsi=%lld\n", where,
      (unsigned long long) c->rip, (unsigned long long) c->rsp,
      (long long) c->rdi, (long long) c->rsi);
#elif defined (_M_ARM) || defined (__arm__)
  printf ("  [ctx@%s] pc=%08x sp=%08x r0=%d r1=%d\n",
      where, c->pc, c->sp, (int) c->r[0], (int) c->r[1]);
#else /* arm64 */
  printf ("  [ctx@%s] pc=%llx sp=%llx x0=%lld x1=%lld\n", where,
      (unsigned long long) c->pc, (unsigned long long) c->sp,
      (long long) c->x[0], (long long) c->x[1]);
#endif
}

static void
on_context_enter (HooxInvocationContext * ic, hx_pointer user_data)
{
  (void) user_data;
  dump_context ("entry", ic->cpu_context);   /* registers at the entry */
}

static void
on_context_mid (HooxInvocationContext * ic, hx_pointer user_data)
{
  (void) user_data;
  dump_context ("mid  ", ic->cpu_context);    /* registers mid-function */
}

static void
demo_cpu_context (HooxInterceptor * interceptor)
{
  HooxInvocationListener * at_entry;

  printf ("== 8. cpu context + arbitrary-instruction probe ==\n");

  at_entry = hoox_make_probe_listener (on_context_enter, NULL, NULL);
  hoox_interceptor_attach (interceptor, (hx_pointer) straight, at_entry, NULL);

  printf ("straight(5, 4) = %d\n", straight (5, 4));

  hoox_interceptor_detach (interceptor, at_entry);
  hoox_invocation_listener_unref (at_entry);

#if defined (__aarch64__) || defined (_M_ARM64)
  {
    /* Probe the 3rd instruction (entry + 8) — a mid-function address. */
    HooxInvocationListener * at_mid = hoox_make_probe_listener (on_context_mid,
        NULL, NULL);
    hx_pointer mid = (hx_pointer) ((hx_uint32 *) (void *) straight + 2);

    hoox_interceptor_attach (interceptor, mid, at_mid, NULL);
    printf ("straight(5, 4) = %d  (probing a mid-function instruction)\n",
        straight (5, 4));
    hoox_interceptor_detach (interceptor, at_mid);
    hoox_invocation_listener_unref (at_mid);
  }
#else
  printf ("(mid-function probe demonstrated on arm64; this arch has a "
      "variable-width ISA)\n");
#endif
  printf ("\n");
}

/* ========================================================================= */

int
main (void)
{
  HooxInterceptor * interceptor;

  hoox_init ();
  interceptor = hoox_interceptor_obtain ();

  printf ("baseline: compute(3, 4) = %d, mul(6, 7) = %d\n\n",
      compute (3, 4), mul (6, 7));

  demo_attach (interceptor);
  demo_custom_listener (interceptor);
  demo_probe (interceptor);
  demo_transaction (interceptor);
  demo_replace (interceptor);
  demo_replace_fast (interceptor);
  demo_ignore_thread (interceptor);
  demo_cpu_context (interceptor);

  hoox_interceptor_unref (interceptor);
  hoox_deinit ();

  printf ("done.\n");
  return 0;
}
