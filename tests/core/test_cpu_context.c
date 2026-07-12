/*
 * Verifies the publicly-exposed CPU register context: a listener can read the
 * registers live at the hooked instruction through HooxInvocationContext's
 * cpu_context, and a probe can be placed at an arbitrary instruction address
 * (not just a function entry) — the frida-style inline-hook surface.
 *
 * This test includes ONLY the public header (hoox.h) on purpose: it exercises
 * exactly what a downstream amalgam consumer sees, so it also guards the public
 * cpu-context layout against drifting from the internal one.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hoox.h"

#include <stdio.h>
#include <stdint.h>

static int failures = 0;

#define CHECK(expr) \
    do { \
      if (!(expr)) { \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
      } \
    } while (0)

/* Architecture-agnostic accessor for the instruction pointer register. */
static uintptr_t
ip_of (HooxCpuContext * c)
{
#if defined (_M_IX86) || defined (__i386__)
  return (uintptr_t) c->eip;
#elif defined (_M_X64) || defined (__x86_64__)
  return (uintptr_t) c->rip;
#else /* arm / arm64 */
  return (uintptr_t) c->pc;
#endif
}

/* Straight-line leaf (no branches) so any 4-byte offset into it on arm64 is a
 * valid, relocatable instruction boundary. */
#ifdef _MSC_VER
# define NOINLINE __declspec (noinline)
#else
# define NOINLINE __attribute__ ((noinline))
#endif

NOINLINE
static int
target (int a, int b)
{
  int s = a + b;
  s = s * 3;
  s = s ^ 0x55;
  return s;
}

static int entry_hits = 0;
static uintptr_t entry_ip = 0;
static int entry_arg0 = 0, entry_arg1 = 0;

static void
on_entry (HooxInvocationContext * ic, hx_pointer u)
{
  (void) u;
  entry_hits++;
  entry_ip = ip_of (ic->cpu_context);          /* read a register directly */
  entry_arg0 = (int) (intptr_t) hoox_invocation_context_get_nth_argument (ic, 0);
  entry_arg1 = (int) (intptr_t) hoox_invocation_context_get_nth_argument (ic, 1);
}

static int mid_hits = 0;
static uintptr_t mid_ip = 0;

static void
on_mid (HooxInvocationContext * ic, hx_pointer u)
{
  (void) u;
  mid_hits++;
  mid_ip = ip_of (ic->cpu_context);
}

int
main (void)
{
  HooxInterceptor * itc;
  HooxInvocationListener * l_entry;
  HooxAttachReturn ar;
  int r;

  hoox_init ();
  itc = hoox_interceptor_obtain ();

  /* --- read the register context at a hook point --- */
  l_entry = hoox_make_probe_listener (on_entry, NULL, NULL);
  ar = hoox_interceptor_attach (itc, (hx_pointer) target, l_entry, NULL);
  CHECK (ar == HOOX_ATTACH_OK);

  r = target (5, 4);
  CHECK (r == (((5 + 4) * 3) ^ 0x55));
  CHECK (entry_hits == 1);
  /* pc read from cpu_context must be the hooked instruction (the entry). */
  CHECK (entry_ip == (uintptr_t) (void *) target);
  /* arguments recovered from the same context. */
  CHECK (entry_arg0 == 5 && entry_arg1 == 4);

  hoox_interceptor_detach (itc, l_entry);
  hoox_invocation_listener_unref (l_entry);

#if defined (__aarch64__) || defined (_M_ARM64)
  /* --- probe an ARBITRARY mid-function instruction (not the entry) ---
   * arm64 is fixed-width, so entry + 8 is a valid, relocatable boundary in
   * this straight-line function. */
  {
    HooxInvocationListener * l_mid;
    hx_pointer mid = (hx_pointer) ((uint32_t *) (void *) target + 2);

    l_mid = hoox_make_probe_listener (on_mid, NULL, NULL);
    ar = hoox_interceptor_attach (itc, mid, l_mid, NULL);
    CHECK (ar == HOOX_ATTACH_OK);

    r = target (5, 4);
    CHECK (r == (((5 + 4) * 3) ^ 0x55));   /* still correct with the mid hook */
    CHECK (mid_hits == 1);
    CHECK (mid_ip == (uintptr_t) mid);     /* pc == the arbitrary address */

    hoox_interceptor_detach (itc, l_mid);
    hoox_invocation_listener_unref (l_mid);
  }
#else
  printf ("cpu_context: mid-function probe assertion skipped on this arch "
      "(variable-width ISA; covered on arm64)\n");
#endif

  hoox_interceptor_unref (itc);
  hoox_deinit ();

  if (failures == 0)
  {
    printf ("cpu_context: all tests passed\n");
    return 0;
  }
  printf ("cpu_context: %d failure(s)\n", failures);
  return 1;
}
