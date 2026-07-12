/*
 * hoox nano-glib: threading primitives implementation.
 *
 * Windows: HxMutex over SRWLOCK (zero-init == SRWLOCK_INIT, no lazy-init
 * race), HxRecMutex manually recursive over SRWLOCK, HxPrivate over FLS (so
 * per-thread destructors run on thread exit).
 * POSIX: pthread mutex/key.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxthread.h"
#include "hxatomic.h"
#include "hxmem.h"
#include "hxmessages.h"

/* ---- one-time init (portable, above the platform split) ----------------- */

static HxMutex hx_once_mutex;

hx_boolean
hx_once_init_enter_impl (volatile void * location)
{
  hx_size * loc = (hx_size *) location;
  hx_boolean need_init;

  hx_mutex_lock (&hx_once_mutex);
  if (*loc == 0)
  {
    need_init = TRUE;   /* keep the lock held until hx_once_init_leave */
  }
  else
  {
    need_init = FALSE;
    hx_mutex_unlock (&hx_once_mutex);
  }

  return need_init;
}

void
hx_once_init_leave_impl (volatile void * location,
                        hx_size result)
{
  hx_size * loc = (hx_size *) location;

  hx_atomic_pointer_set (loc, result);
  hx_mutex_unlock (&hx_once_mutex);
}

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

HX_STATIC_ASSERT (sizeof (SRWLOCK) == sizeof (void *));

HxThread *
hx_thread_self (void)
{
  return (HxThread *) (hx_size) GetCurrentThreadId ();
}

void
hx_usleep (hx_ulong microseconds)
{
  Sleep ((DWORD) (microseconds / 1000));
}

/* ---- HxMutex (SRWLOCK) --------------------------------------------------- */

void
hx_mutex_init (HxMutex * mutex)
{
  mutex->p = NULL; /* SRWLOCK_INIT */
}

void
hx_mutex_clear (HxMutex * mutex)
{
  (void) mutex;
}

void
hx_mutex_lock (HxMutex * mutex)
{
  AcquireSRWLockExclusive ((PSRWLOCK) &mutex->p);
}

void
hx_mutex_unlock (HxMutex * mutex)
{
  ReleaseSRWLockExclusive ((PSRWLOCK) &mutex->p);
}

/* ---- HxRecMutex (recursive over SRWLOCK) --------------------------------- */

void
hx_rec_mutex_init (HxRecMutex * mutex)
{
  mutex->srw = NULL;
  mutex->owner = 0;
  mutex->count = 0;
}

void
hx_rec_mutex_clear (HxRecMutex * mutex)
{
  (void) mutex;
}

void
hx_rec_mutex_lock (HxRecMutex * mutex)
{
  hx_size self = (hx_size) GetCurrentThreadId ();

  if (mutex->owner == self)
  {
    mutex->count++;
    return;
  }

  AcquireSRWLockExclusive ((PSRWLOCK) &mutex->srw);
  mutex->owner = self;
  mutex->count = 1;
}

hx_boolean
hx_rec_mutex_trylock (HxRecMutex * mutex)
{
  hx_size self = (hx_size) GetCurrentThreadId ();

  if (mutex->owner == self)
  {
    mutex->count++;
    return TRUE;
  }

  if (!TryAcquireSRWLockExclusive ((PSRWLOCK) &mutex->srw))
    return FALSE;

  mutex->owner = self;
  mutex->count = 1;
  return TRUE;
}

void
hx_rec_mutex_unlock (HxRecMutex * mutex)
{
  if (--mutex->count == 0)
  {
    mutex->owner = 0;
    ReleaseSRWLockExclusive ((PSRWLOCK) &mutex->srw);
  }
}

/* ---- HxPrivate (FLS with per-thread destructor) -------------------------- */

typedef struct _HxPrivateSlot HxPrivateSlot;

struct _HxPrivateSlot
{
  HxDestroyNotify notify;
  hx_pointer value;
};

static void NTAPI
hx_private_fls_cb (PVOID data)
{
  HxPrivateSlot * slot = data;

  if (slot == NULL)
    return;

  if (slot->notify != NULL && slot->value != NULL)
    slot->notify (slot->value);

  hx_free (slot);
}

static DWORD
hx_private_index (HxPrivate * key)
{
  hx_size stored;

  stored = (hx_size) hx_atomic_pointer_get (&key->impl);
  if (stored == 0)
  {
    DWORD index = FlsAlloc (hx_private_fls_cb);
    hx_pointer desired;

    if (index == FLS_OUT_OF_INDEXES)
      hx_abort ();

    desired = (hx_pointer) (hx_size) (index + 1);

    if (hx_atomic_pointer_compare_and_exchange (&key->impl, NULL, desired))
    {
      stored = (hx_size) desired;
    }
    else
    {
      FlsFree (index);
      stored = (hx_size) hx_atomic_pointer_get (&key->impl);
    }
  }

  return (DWORD) (stored - 1);
}

static HxPrivateSlot *
hx_private_slot (HxPrivate * key,
                 hx_boolean create)
{
  DWORD index = hx_private_index (key);
  HxPrivateSlot * slot = FlsGetValue (index);

  if (slot == NULL && create)
  {
    slot = hx_new0 (HxPrivateSlot, 1);
    slot->notify = key->notify;
    FlsSetValue (index, slot);
  }

  return slot;
}

hx_pointer
hx_private_get (HxPrivate * key)
{
  HxPrivateSlot * slot = hx_private_slot (key, FALSE);
  return (slot != NULL) ? slot->value : NULL;
}

void
hx_private_set (HxPrivate * key,
               hx_pointer value)
{
  HxPrivateSlot * slot = hx_private_slot (key, TRUE);
  slot->value = value;
}

#else /* POSIX */

#include <pthread.h>
#include <unistd.h>

HxThread *
hx_thread_self (void)
{
  return (HxThread *) (hx_size) pthread_self ();
}

void
hx_usleep (hx_ulong microseconds)
{
  usleep (microseconds);
}

void
hx_mutex_init (HxMutex * mutex)
{
  pthread_mutex_t * m = hx_new0 (pthread_mutex_t, 1);
  pthread_mutex_init (m, NULL);
  mutex->p = m;
}

void
hx_mutex_clear (HxMutex * mutex)
{
  if (mutex->p != NULL)
  {
    pthread_mutex_destroy (mutex->p);
    hx_free (mutex->p);
    mutex->p = NULL;
  }
}

static pthread_mutex_t *
hx_mutex_get (HxMutex * mutex)
{
  /* Lazy init for statically-declared HxMutex. */
  if (hx_atomic_pointer_get (&mutex->p) == NULL)
  {
    pthread_mutex_t * m = hx_new0 (pthread_mutex_t, 1);
    pthread_mutex_init (m, NULL);
    if (!hx_atomic_pointer_compare_and_exchange (&mutex->p, NULL, m))
    {
      pthread_mutex_destroy (m);
      hx_free (m);
    }
  }
  return mutex->p;
}

void
hx_mutex_lock (HxMutex * mutex)
{
  pthread_mutex_lock (hx_mutex_get (mutex));
}

void
hx_mutex_unlock (HxMutex * mutex)
{
  pthread_mutex_unlock (hx_mutex_get (mutex));
}

static pthread_mutex_t *
hx_rec_get (HxRecMutex * mutex)
{
  if (hx_atomic_pointer_get (&mutex->srw) == NULL)
  {
    pthread_mutex_t * m = hx_new0 (pthread_mutex_t, 1);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init (&attr);
    pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init (m, &attr);
    pthread_mutexattr_destroy (&attr);
    if (!hx_atomic_pointer_compare_and_exchange (&mutex->srw, NULL, m))
    {
      pthread_mutex_destroy (m);
      hx_free (m);
    }
  }
  return mutex->srw;
}

void
hx_rec_mutex_init (HxRecMutex * mutex)
{
  mutex->srw = NULL;
  (void) hx_rec_get (mutex);
}

void
hx_rec_mutex_clear (HxRecMutex * mutex)
{
  if (mutex->srw != NULL)
  {
    pthread_mutex_destroy (mutex->srw);
    hx_free (mutex->srw);
    mutex->srw = NULL;
  }
}

void
hx_rec_mutex_lock (HxRecMutex * mutex)
{
  pthread_mutex_lock (hx_rec_get (mutex));
}

hx_boolean
hx_rec_mutex_trylock (HxRecMutex * mutex)
{
  return pthread_mutex_trylock (hx_rec_get (mutex)) == 0;
}

void
hx_rec_mutex_unlock (HxRecMutex * mutex)
{
  pthread_mutex_unlock (hx_rec_get (mutex));
}

typedef struct _HxPrivateKey HxPrivateKey;

struct _HxPrivateKey
{
  pthread_key_t key;
};

static pthread_key_t
hx_private_key (HxPrivate * key)
{
  HxPrivateKey * pk;

  pk = hx_atomic_pointer_get (&key->impl);
  if (pk == NULL)
  {
    pk = hx_new0 (HxPrivateKey, 1);
    if (pthread_key_create (&pk->key, key->notify) != 0)
      hx_abort ();
    if (!hx_atomic_pointer_compare_and_exchange (&key->impl, NULL, pk))
    {
      pthread_key_delete (pk->key);
      hx_free (pk);
      pk = hx_atomic_pointer_get (&key->impl);
    }
  }

  return pk->key;
}

hx_pointer
hx_private_get (HxPrivate * key)
{
  return pthread_getspecific (hx_private_key (key));
}

void
hx_private_set (HxPrivate * key,
               hx_pointer value)
{
  pthread_setspecific (hx_private_key (key), value);
}

#endif
