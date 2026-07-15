/*
 * Multi-threaded interceptor test: keep one listener attached while worker
 * threads hammer the hooked function and the main thread repeatedly attaches
 * and detaches another listener. This exercises both the per-thread TLS
 * invocation stack and copy-on-write listener dispatch. A single-threaded test
 * cannot expose a listener array being reclaimed while another thread reads it.
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
# include <sched.h>
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

#define NUM_THREADS           4
#define MIN_CALLS_PER_THREAD  2000
#define MUTATION_ITERATIONS   5000

HOOX_NOINLINE
static int
target_add (int a, int b)
{
  return a + b;
}

/* The persistent listener counts every hooked invocation. */
static volatile hx_int enter_count = 0;
static volatile hx_int leave_count = 0;
static volatile hx_int temporary_enter_count = 0;
static volatile hx_int workers_ready = 0;
static volatile hx_int mutations_done = 0;

static volatile hx_int worker_ok = 1;

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
on_temporary_enter (HooxInvocationContext * ic, hx_pointer user_data)
{
  (void) ic;
  (void) user_data;
  hx_atomic_int_inc (&temporary_enter_count);
}

static void
yield_thread (void)
{
#ifdef _WIN32
  Sleep (0);
#else
  sched_yield ();
#endif
}

static void
worker (void)
{
  int i;

  hx_atomic_int_inc (&workers_ready);

  for (i = 0; i < MIN_CALLS_PER_THREAD ||
      !hx_atomic_int_get (&mutations_done); i++)
  {
    if (target_add (i, 1) != i + 1)
      hx_atomic_int_set (&worker_ok, 0);
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
  HooxInvocationListener * listener, * temporary_listener;
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
  temporary_listener = hoox_make_call_listener (on_temporary_enter, NULL,
      NULL, NULL);
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

  while (hx_atomic_int_get (&workers_ready) != NUM_THREADS)
    yield_thread ();

  for (i = 0; i != MUTATION_ITERATIONS; i++)
  {
    ar = hoox_interceptor_attach (interceptor, (hx_pointer) target_add,
        temporary_listener, NULL);
    CHECK (ar == HOOX_ATTACH_OK);
    CHECK (target_add (i, 1) == i + 1);
    hoox_interceptor_detach (interceptor, temporary_listener);
  }
  hx_atomic_int_set (&mutations_done, 1);

  for (i = 0; i != NUM_THREADS; i++)
  {
#ifdef _WIN32
    WaitForSingleObject (threads[i], INFINITE);
    CloseHandle (threads[i]);
#else
    pthread_join (threads[i], NULL);
#endif
  }

  CHECK (hx_atomic_int_get (&worker_ok));
  CHECK (hx_atomic_int_get (&enter_count) >=
      NUM_THREADS * MIN_CALLS_PER_THREAD);
  CHECK (hx_atomic_int_get (&enter_count) == hx_atomic_int_get (&leave_count));
  CHECK (hx_atomic_int_get (&temporary_enter_count) > 0);
  CHECK (hoox_interceptor_flush (interceptor));

  hoox_interceptor_detach (interceptor, listener);
  hoox_invocation_listener_unref (temporary_listener);
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
