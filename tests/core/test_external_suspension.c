/*
 * External thread suspension: hoox_memory_set_external_thread_suspension() lets a
 * caller that provides its own out-of-process stop-the-world (e.g. ptrace) skip
 * hoox's in-process one. On POSIX-guard builds a peer blocking the park signal
 * proves both directions: patching fails closed without the flag and succeeds
 * with it. Elsewhere only the behavior-preserving direction is exercised.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxmemory.h"

#include <stdio.h>
#include <string.h>

HOOX_API void hoox_internal_heap_ref (void);
HOOX_API void hoox_internal_heap_unref (void);

#if defined (HOOX_TEST_POSIX_PATCH_PC_GUARD)
# include <pthread.h>
# include <sched.h>
# include <signal.h>

# ifndef HOOX_PEER_PARK_SIGNAL
#  define HOOX_PEER_PARK_SIGNAL (SIGRTMIN + 6)
# endif
#endif

static int hx_failures = 0;

#define CHECK(expr) \
    do { \
      if (!(expr)) { \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        hx_failures++; \
      } \
    } while (0)

typedef int (* IntFunc) (void);

typedef struct
{
  const hx_uint8 * code;
  hx_size size;
} PatchData;

static void
apply_patch (hx_pointer mem, hx_pointer user_data)
{
  PatchData * d = user_data;
  memcpy (mem, d->code, d->size);
}

#if defined (HOOX_TEST_POSIX_PATCH_PC_GUARD)
typedef struct
{
  volatile int ready;
  volatile int stop;
} BlockedPeer;

static void *
blocked_peer_main (void * user_data)
{
  BlockedPeer * peer = user_data;
  sigset_t blocked;

  /* Block hoox's park signal: this thread can never be parked, so an
   * in-process stop-the-world attempt must fail closed. */
  sigemptyset (&blocked);
  sigaddset (&blocked, HOOX_PEER_PARK_SIGNAL);
  pthread_sigmask (SIG_BLOCK, &blocked, NULL);
  peer->ready = 1;
  while (peer->stop == 0)
    sched_yield ();
  return NULL;
}
#endif

int
main (void)
{
  hx_uint page_size;
  hx_uint8 * page;
  IntFunc fn;
#if defined (__aarch64__) || defined (_M_ARM64)
  /* mov w0, #42 ; ret   and   mov w0, #99 ; ret  (little-endian) */
  const hx_uint8 code_42[8] =
      { 0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6 };
  const hx_uint8 code_99[8] =
      { 0x60, 0x0c, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6 };
#elif defined (__arm__) || defined (_M_ARM)
  /* A32: mov r0, #imm ; bx lr  (little-endian) */
  const hx_uint8 code_42[8] =
      { 0x2a, 0x00, 0xa0, 0xe3, 0x1e, 0xff, 0x2f, 0xe1 };
  const hx_uint8 code_99[8] =
      { 0x63, 0x00, 0xa0, 0xe3, 0x1e, 0xff, 0x2f, 0xe1 };
#else
  const hx_uint8 code_42[6] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };
  const hx_uint8 code_99[6] = { 0xB8, 0x63, 0x00, 0x00, 0x00, 0xC3 };
#endif
  PatchData pd;

  hoox_internal_heap_ref ();

  if (hoox_query_rwx_support () != HOOX_RWX_FULL)
  {
    printf ("external-suspension: skipped (RWX unavailable on this platform)\n");
    hoox_internal_heap_unref ();
    return 0;
  }

  page_size = hoox_query_page_size ();
  CHECK (page_size >= 4096);

  page = hoox_alloc_n_pages (1, HOOX_PAGE_RWX);
  CHECK (page != NULL);
  if (page == NULL)
  {
    hoox_internal_heap_unref ();
    return 1;
  }

  memcpy (page, code_42, sizeof (code_42));
  hoox_memory_mark_code (page, sizeof (code_42));
  fn = (IntFunc) (void *) page;
  CHECK (fn () == 42);

  /* Baseline: the flag defaults to disabled and patching works. */
  pd.code = code_99;
  pd.size = sizeof (code_99);
  CHECK (hoox_memory_patch_code (page, sizeof (code_99), apply_patch, &pd));
  CHECK (fn () == 99);

#if defined (HOOX_TEST_POSIX_PATCH_PC_GUARD)
  {
    BlockedPeer peer = { 0, 0 };
    pthread_t peer_thread;
    PatchData back;

    back.code = code_42;
    back.size = sizeof (code_42);

    CHECK (pthread_create (&peer_thread, NULL, blocked_peer_main, &peer) == 0);
    while (peer.ready == 0)
      sched_yield ();

    /* A peer that cannot be parked: the in-process stop-the-world fails. */
    CHECK (!hoox_memory_patch_code (page, sizeof (code_42), apply_patch, &back));

    /* With an external freeze declared, hoox skips its own and the patch goes
     * through. (The peer only spins on flag words and never touches the page,
     * so this thread executing the freshly patched bytes stays safe.) */
    hoox_memory_set_external_thread_suspension (TRUE);
    CHECK (hoox_memory_patch_code (page, sizeof (code_99), apply_patch, &pd));
    CHECK (fn () == 99);
    hoox_memory_set_external_thread_suspension (FALSE);

    /* Flag cleared: the in-process guard is enforced again. */
    CHECK (!hoox_memory_patch_code (page, sizeof (code_42), apply_patch, &back));

    peer.stop = 1;
    pthread_join (peer_thread, NULL);
  }
#else
  /* Without the guard there is nothing to skip; the flag must not break
   * ordinary patching. */
  hoox_memory_set_external_thread_suspension (TRUE);
  CHECK (hoox_memory_patch_code (page, sizeof (code_99), apply_patch, &pd));
  hoox_memory_set_external_thread_suspension (FALSE);
  CHECK (fn () == 99);
#endif

  hoox_free_pages (page);

  hoox_internal_heap_unref ();

  if (hx_failures == 0)
  {
    printf ("external-suspension: all tests passed\n");
    return 0;
  }
  printf ("external-suspension: %d failure(s)\n", hx_failures);
  return 1;
}
