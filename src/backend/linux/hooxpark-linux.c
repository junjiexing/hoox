/*
 * hoox — Linux peer parking: signal-driven stop-the-world for safe patching.
 *
 * Every peer thread is sent a dedicated real-time signal; its handler records
 * the interrupted instruction pointer and parks until released. Fail-closed
 * throughout: enumeration failures, signals blocked by the target, and wait
 * budget overruns all fail the park attempt, and with it the patch.
 *
 * The wait window performs no heap allocation and touches no loader state:
 * peer threads can be parked while holding any libc lock (including the
 * malloc arena lock), so the parking thread must not allocate either.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxmemory.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/ucontext.h>

#ifndef HOOX_PEER_PARK_SIGNAL
/* glibc's NPTL consumes SIGRTMIN+0 and SIGRTMIN+1 internally. */
# define HOOX_PEER_PARK_SIGNAL (SIGRTMIN + 6)
#endif

/* REG_RIP/REG_EIP sit behind glibc's _GNU_SOURCE gate, which cannot be relied
 * on here: the amalgamation may include libc headers before this section, and
 * consumers are not required to define it. The gregs indices are ABI-stable
 * on both glibc and musl, so fall back to them when the macros are hidden. */
#if defined (REG_RIP)
# define HOOX_PEER_PARK_GREG_PC REG_RIP
#elif defined (REG_EIP)
# define HOOX_PEER_PARK_GREG_PC REG_EIP
#elif defined (__x86_64__)
# define HOOX_PEER_PARK_GREG_PC 16
#elif defined (__i386__)
# define HOOX_PEER_PARK_GREG_PC 14
#endif

#define HOOX_PEER_PARK_MAX_THREADS 4096
#define HOOX_PEER_PARK_ROUND_WAIT_NS (50 * 1000 * 1000)

typedef struct _HooxPeerParkSlot HooxPeerParkSlot;

struct _HooxPeerParkSlot
{
  pid_t tid;
  volatile hx_uintptr pc;
  volatile hx_int32 arrived;
  volatile hx_int32 done;
};

struct _HooxPeerPark
{
  volatile hx_int32 release;
  hx_uint count;
  HooxPeerParkSlot slots[HOOX_PEER_PARK_MAX_THREADS];
};

static HooxPeerPark hoox_the_peer_park;
static volatile hx_int32 hoox_peer_park_busy;
static volatile hx_int32 hoox_peer_park_handler_ready;

struct _HooxLinuxDirent64
{
  hx_uint64 d_ino;
  hx_int64 d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[];
};

static hx_uint64
hoox_peer_park_now_ns (void)
{
  struct timespec ts;

  clock_gettime (CLOCK_MONOTONIC, &ts);

  return ((hx_uint64) ts.tv_sec * 1000000000ULL) + (hx_uint64) ts.tv_nsec;
}

static void
hoox_peer_park_pause (void)
{
#if defined (__x86_64__) || defined (__i386__)
  __asm__ __volatile__ ("pause");
#else
  sched_yield ();
#endif
}

static void
hoox_peer_park_handler (int sig,
                        siginfo_t * info,
                        void * context)
{
  HooxPeerPark * park = &hoox_the_peer_park;
  const ucontext_t * uc = (const ucontext_t *) context;
  pid_t tid;
  hx_uint i;
  int saved_errno;

  (void) sig;
  (void) info;

  saved_errno = errno;
  tid = (pid_t) syscall (SYS_gettid);

  for (i = 0; i < park->count; i++)
    {
      if (park->slots[i].tid == tid)
        break;
    }
  if (i == park->count)
    goto beach; /* not part of this park (stale delivery); leave immediately */

#if defined (__x86_64__)
  park->slots[i].pc = (hx_uintptr) uc->uc_mcontext.gregs[HOOX_PEER_PARK_GREG_PC];
#elif defined (__i386__)
  park->slots[i].pc = (hx_uintptr) uc->uc_mcontext.gregs[HOOX_PEER_PARK_GREG_PC];
#else
  park->slots[i].pc = 0;
#endif
  __atomic_store_n (&park->slots[i].arrived, 1, __ATOMIC_RELEASE);

  while (__atomic_load_n (&park->release, __ATOMIC_ACQUIRE) == 0)
    hoox_peer_park_pause ();

  __atomic_store_n (&park->slots[i].done, 1, __ATOMIC_RELEASE);

beach:
  errno = saved_errno;
}

static hx_boolean
hoox_peer_park_ensure_handler (void)
{
  struct sigaction action;

  if (__atomic_load_n (&hoox_peer_park_handler_ready, __ATOMIC_ACQUIRE))
    return TRUE;

  memset (&action, 0, sizeof (action));
  action.sa_sigaction = hoox_peer_park_handler;
  action.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset (&action.sa_mask);
  if (sigaction (HOOX_PEER_PARK_SIGNAL, &action, NULL) != 0)
    return FALSE;

  __atomic_store_n (&hoox_peer_park_handler_ready, 1, __ATOMIC_RELEASE);
  return TRUE;
}

/* Collects every live thread id except the caller's, using raw directory
 * reads only. Returns the tid count, or -1 on failure. */
static hx_int
hoox_peer_park_collect_tids (pid_t * tids,
                             hx_uint max_tids,
                             pid_t self_tid)
{
  char buffer[2048];
  hx_uint count = 0;
  int fd;
  long nread;

  fd = open ("/proc/self/task", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0)
    return -1;

  for (;;)
    {
      long bpos;

      nread = syscall (SYS_getdents64, fd, buffer, sizeof (buffer));
      if (nread < 0)
        {
          close (fd);
          return -1;
        }
      if (nread == 0)
        break;

      for (bpos = 0; bpos < nread;)
        {
          const struct _HooxLinuxDirent64 * entry =
              (const struct _HooxLinuxDirent64 *) (buffer + bpos);
          pid_t tid = 0;
          const hx_char * name;

          for (name = entry->d_name; *name >= '0' && *name <= '9'; name++)
            tid = tid * 10 + (*name - '0');
          if (*name == '\0' && tid != 0 && tid != self_tid)
            {
              hx_uint i;
              hx_boolean known = FALSE;

              for (i = 0; i < count; i++)
                {
                  if (tids[i] == tid)
                    {
                      known = TRUE;
                      break;
                    }
                }
              if (!known)
                {
                  if (count == max_tids)
                    {
                      close (fd);
                      return -1;
                    }
                  tids[count++] = tid;
                }
            }

          bpos += entry->d_reclen;
        }
    }

  close (fd);
  return (hx_int) count;
}

static hx_boolean
hoox_peer_park_wait_arrived (HooxPeerPark * park,
                             hx_uint upto)
{
  const hx_uint64 deadline =
      hoox_peer_park_now_ns () + HOOX_PEER_PARK_ROUND_WAIT_NS;
  hx_uint i;

  for (;;)
    {
      hx_boolean all_arrived = TRUE;

      for (i = 0; i < upto; i++)
        {
          if (__atomic_load_n (&park->slots[i].arrived, __ATOMIC_ACQUIRE) == 0)
            {
              all_arrived = FALSE;
              break;
            }
        }
      if (all_arrived)
        return TRUE;
      if (hoox_peer_park_now_ns () >= deadline)
        return FALSE;

      hoox_peer_park_pause ();
    }
}

static void
hoox_peer_park_release_all (HooxPeerPark * park)
{
  const hx_uint64 deadline =
      hoox_peer_park_now_ns () + HOOX_PEER_PARK_ROUND_WAIT_NS;
  hx_uint i;

  __atomic_store_n (&park->release, 1, __ATOMIC_RELEASE);
  for (;;)
    {
      hx_boolean all_done = TRUE;

      for (i = 0; i < park->count; i++)
        {
          if (park->slots[i].arrived &&
              __atomic_load_n (&park->slots[i].done, __ATOMIC_ACQUIRE) == 0)
            {
              all_done = FALSE;
              break;
            }
        }
      if (all_done || hoox_peer_park_now_ns () >= deadline)
        break;

      hoox_peer_park_pause ();
    }
}

HooxPeerPark *
hoox_peer_park_begin (void)
{
  HooxPeerPark * park = &hoox_the_peer_park;
  const pid_t self_tid = (pid_t) syscall (SYS_gettid);
  const pid_t pid = getpid ();
  pid_t new_tids[256];

  if (__atomic_test_and_set (&hoox_peer_park_busy, __ATOMIC_ACQUIRE))
    return NULL;
  if (!hoox_peer_park_ensure_handler ())
    goto failure;

  park->release = 0;
  park->count = 0;

  for (;;)
    {
      const hx_int found = hoox_peer_park_collect_tids (
          new_tids, HX_N_ELEMENTS (new_tids), self_tid);
      hx_int n;
      hx_uint newly_signaled = 0;

      if (found < 0)
        goto failure;

      for (n = 0; n < found; n++)
        {
          HooxPeerParkSlot * slot;
          hx_uint i;

          for (i = 0; i < park->count; i++)
            {
              if (park->slots[i].tid == new_tids[n])
                break;
            }
          if (i != park->count)
            continue;
          if (park->count == HOOX_PEER_PARK_MAX_THREADS)
            goto failure;

          slot = &park->slots[park->count];
          slot->tid = new_tids[n];
          slot->pc = 0;
          slot->arrived = 0;
          slot->done = 0;
          park->count++;

          if (syscall (SYS_tgkill, pid, slot->tid, HOOX_PEER_PARK_SIGNAL) != 0)
            {
              /* The thread exited between enumeration and signal delivery;
               * drop the slot again, a dead thread executes nothing. */
              park->count--;
              continue;
            }
          newly_signaled++;
        }

      if (!hoox_peer_park_wait_arrived (park, park->count))
        goto failure;
      if (newly_signaled == 0)
        return park; /* a full pass found no new threads: every peer is parked */
    }

failure:
  hoox_peer_park_release_all (park);
  __atomic_clear (&hoox_peer_park_busy, __ATOMIC_RELEASE);
  return NULL;
}

hx_boolean
hoox_peer_park_all_clear_of (HooxPeerPark * self,
                             const HooxPeerParkRange * ranges,
                             hx_uint n_ranges)
{
  hx_uint i, n;

  for (i = 0; i < self->count; i++)
    {
      const hx_uintptr pc = self->slots[i].pc;

      for (n = 0; n < n_ranges; n++)
        {
          if (pc >= (hx_uintptr) ranges[n].begin &&
              pc < (hx_uintptr) ranges[n].end)
            return FALSE;
        }
    }

  return TRUE;
}

void
hoox_peer_park_end (HooxPeerPark * self)
{
  hoox_peer_park_release_all (self);
  __atomic_clear (&hoox_peer_park_busy, __ATOMIC_RELEASE);
}
