/*
 * Regression test for the Apple-arm64 self-hosting W^X patch collision.
 *
 * When hoox is statically linked into the target it hooks, a hooked function
 * can share a 16 KiB page with hoox's own interceptor/patch code. Patching that
 * page in place drops execute from it during the write; if code that runs while
 * the page is writable lives on it, the patching thread self-faults.
 *
 * A previous fix guarded only the common case with a 3-function "anchor"
 * heuristic, but the in-place window actually reaches more of the interceptor
 * backend than those anchors describe — so a target sharing a page with, e.g.,
 * the backend's trampoline writer (but not an anchor) still faulted, undetected.
 *
 * This test hooks _hoox_interceptor_backend_get_function_address — a hoox
 * function that lives on exactly such a "reached but not an anchor" page — which
 * self-faulted (SIGBUS) under the heuristic. With the off-page patcher used
 * unconditionally on Apple arm64 the attach must succeed and be reversible.
 *
 * On non-Apple-arm64 platforms this is simply a normal self-hook of an internal
 * symbol and must also pass, so the test is unconditional.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hoox.h"
#include "hooxinterceptor.h"

#include <stdio.h>

static int hx_failures = 0;

#define CHECK(expr) \
    HX_STMT_START { \
      if (!(expr)) { \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        hx_failures++; \
      } \
    } HX_STMT_END

/*
 * A hoox-internal function that the linker places on a page reached by the
 * in-place patch window but not covered by the old anchor set. Declared here to
 * take its address as a hook target; it is never called through this pointer.
 */
extern hx_pointer _hoox_interceptor_backend_get_function_address (void * ctx);

static int enter_count = 0;

static void
on_enter (HooxInvocationContext * ic, hx_pointer user_data)
{
  (void) ic;
  (void) user_data;
  enter_count++;
}

int
main (void)
{
  HooxInterceptor * interceptor;
  HooxInvocationListener * listener;
  HooxAttachReturn ar;
  hx_pointer target = (hx_pointer) _hoox_interceptor_backend_get_function_address;

  hoox_init ();

  interceptor = hoox_interceptor_obtain ();
  CHECK (interceptor != NULL);

  listener = hoox_make_call_listener (on_enter, NULL, NULL, NULL);

  /* Under the old heuristic this attach self-faulted (SIGBUS) on Apple arm64. */
  ar = hoox_interceptor_attach (interceptor, target, listener, NULL);
  CHECK (ar == HOOX_ATTACH_OK);

  if (ar == HOOX_ATTACH_OK)
    hoox_interceptor_detach (interceptor, listener);

  hoox_invocation_listener_unref (listener);
  hoox_interceptor_unref (interceptor);
  hoox_deinit ();

  if (hx_failures == 0)
  {
    printf ("interceptor selfhost: all tests passed\n");
    return 0;
  }
  printf ("interceptor selfhost: %d failure(s)\n", hx_failures);
  return 1;
}
