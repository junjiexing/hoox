/*
 * Multi-threaded interceptor test: attach one listener, then hammer the hooked
 * function concurrently from several threads. This exercises the per-thread TLS
 * invocation stack and the lock-free listener dispatch — the exact machinery
 * that silently broke on FreeBSD (pthread stubs -> NULL TLS -> a fresh, empty
 * invocation context per call -> on-leave crash). A single-threaded test never
 * touches it; this one does.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hoox.h"

#include "hxatomic.h"

#include <stdio.h>

#ifdef _WIN32
# include <windows.h>
#else
# include <pthread.h>
#endif

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

#define NUM_THREADS       4
#define CALLS_PER_THREAD  2000

HOOX_NOINLINE
static int
target_add (int a, int b)
{
  return a + b;
}

/* enter/leave counters are bumped from every worker thread. */
static volatile hx_int enter_count = 0;
static volatile hx_int leave_count = 0;

/* Set to 0 (never anything else) if any thread sees a wrong result — a benign
 * race: all writers store the same value and it is read only after join. */
static volatile int worker_ok = 1;

static void
on_enter (HooxInvocationContext * ic, hx_pointer user_data)
{
  (void) ic;
  (void) user_data;
  hx_atomic_int_inc (&enter_count);
}

static void
on_leave (HooxInvocationContext * ic, hx_pointer user_data)
{
  (void) ic;
  (void) user_data;
  hx_atomic_int_inc (&leave_count);
}

static void
worker (void)
{
  int i;

  for (i = 0; i != CALLS_PER_THREAD; i++)
  {
    if (target_add (i, 1) != i + 1)
      worker_ok = 0;
  }
}

#ifdef _WIN32
static DWORD WINAPI
thread_entry (LPVOID param)
{
  (void) param;
  worker ();
  return 0;
}
#else
static void *
thread_entry (void * param)
{
  (void) param;
  worker ();
  return NULL;
}
#endif

int
main (void)
{
  HooxInterceptor * interceptor;
  HooxInvocationListener * listener;
  HooxAttachReturn ar;
  int i;
#ifdef _WIN32
  HANDLE threads[NUM_THREADS];
#else
  pthread_t threads[NUM_THREADS];
#endif

  hoox_init ();

  interceptor = hoox_interceptor_obtain ();
  CHECK (interceptor != NULL);

  listener = hoox_make_call_listener (on_enter, on_leave, NULL, NULL);
  ar = hoox_interceptor_attach (interceptor, (hx_pointer) target_add, listener,
      NULL);
  CHECK (ar == HOOX_ATTACH_OK);

  for (i = 0; i != NUM_THREADS; i++)
  {
#ifdef _WIN32
    threads[i] = CreateThread (NULL, 0, thread_entry, NULL, 0, NULL);
    CHECK (threads[i] != NULL);
#else
    CHECK (pthread_create (&threads[i], NULL, thread_entry, NULL) == 0);
#endif
  }

  for (i = 0; i != NUM_THREADS; i++)
  {
#ifdef _WIN32
    WaitForSingleObject (threads[i], INFINITE);
    CloseHandle (threads[i]);
#else
    pthread_join (threads[i], NULL);
#endif
  }

  CHECK (worker_ok);
  CHECK (hx_atomic_int_get (&enter_count) == NUM_THREADS * CALLS_PER_THREAD);
  CHECK (hx_atomic_int_get (&leave_count) == NUM_THREADS * CALLS_PER_THREAD);

  hoox_interceptor_detach (interceptor, listener);
  hoox_invocation_listener_unref (listener);

  hoox_interceptor_unref (interceptor);

  hoox_deinit ();

  if (hx_failures == 0)
  {
    printf ("interceptor threads: all tests passed\n");
    return 0;
  }
  printf ("interceptor threads: %d failure(s)\n", hx_failures);
  return 1;
}
