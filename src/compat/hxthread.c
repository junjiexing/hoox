/*
 * hoox nano-glib: threading primitives implementation.
 *
 * Windows: GMutex over SRWLOCK (zero-init == SRWLOCK_INIT, no lazy-init
 * race), GRecMutex manually recursive over SRWLOCK, GPrivate over FLS (so
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

static GMutex hx_once_mutex;

gboolean
g_once_init_enter_impl (volatile void * location)
{
  gsize * loc = (gsize *) location;
  gboolean need_init;

  g_mutex_lock (&hx_once_mutex);
  if (*loc == 0)
  {
    need_init = TRUE;   /* keep the lock held until g_once_init_leave */
  }
  else
  {
    need_init = FALSE;
    g_mutex_unlock (&hx_once_mutex);
  }

  return need_init;
}

void
g_once_init_leave_impl (volatile void * location,
                        gsize result)
{
  gsize * loc = (gsize *) location;

  g_atomic_pointer_set (loc, result);
  g_mutex_unlock (&hx_once_mutex);
}

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

G_STATIC_ASSERT (sizeof (SRWLOCK) == sizeof (void *));

/* ---- GMutex (SRWLOCK) --------------------------------------------------- */

void
g_mutex_init (GMutex * mutex)
{
  mutex->p = NULL; /* SRWLOCK_INIT */
}

void
g_mutex_clear (GMutex * mutex)
{
  (void) mutex;
}

void
g_mutex_lock (GMutex * mutex)
{
  AcquireSRWLockExclusive ((PSRWLOCK) &mutex->p);
}

gboolean
g_mutex_trylock (GMutex * mutex)
{
  return TryAcquireSRWLockExclusive ((PSRWLOCK) &mutex->p);
}

void
g_mutex_unlock (GMutex * mutex)
{
  ReleaseSRWLockExclusive ((PSRWLOCK) &mutex->p);
}

/* ---- GRecMutex (recursive over SRWLOCK) --------------------------------- */

void
g_rec_mutex_init (GRecMutex * mutex)
{
  mutex->srw = NULL;
  mutex->owner = 0;
  mutex->count = 0;
}

void
g_rec_mutex_clear (GRecMutex * mutex)
{
  (void) mutex;
}

void
g_rec_mutex_lock (GRecMutex * mutex)
{
  gsize self = (gsize) GetCurrentThreadId ();

  if (mutex->owner == self)
  {
    mutex->count++;
    return;
  }

  AcquireSRWLockExclusive ((PSRWLOCK) &mutex->srw);
  mutex->owner = self;
  mutex->count = 1;
}

gboolean
g_rec_mutex_trylock (GRecMutex * mutex)
{
  gsize self = (gsize) GetCurrentThreadId ();

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
g_rec_mutex_unlock (GRecMutex * mutex)
{
  if (--mutex->count == 0)
  {
    mutex->owner = 0;
    ReleaseSRWLockExclusive ((PSRWLOCK) &mutex->srw);
  }
}

/* ---- GPrivate (FLS with per-thread destructor) -------------------------- */

typedef struct _HxPrivateSlot HxPrivateSlot;

struct _HxPrivateSlot
{
  GDestroyNotify notify;
  gpointer value;
};

static void NTAPI
hx_private_fls_cb (PVOID data)
{
  HxPrivateSlot * slot = data;

  if (slot == NULL)
    return;

  if (slot->notify != NULL && slot->value != NULL)
    slot->notify (slot->value);

  g_free (slot);
}

static DWORD
hx_private_index (GPrivate * key)
{
  gsize stored;

  stored = (gsize) g_atomic_pointer_get (&key->impl);
  if (stored == 0)
  {
    DWORD index = FlsAlloc (hx_private_fls_cb);
    gpointer desired = (gpointer) (gsize) (index + 1);

    if (g_atomic_pointer_compare_and_exchange (&key->impl, NULL, desired))
    {
      stored = (gsize) desired;
    }
    else
    {
      FlsFree (index);
      stored = (gsize) g_atomic_pointer_get (&key->impl);
    }
  }

  return (DWORD) (stored - 1);
}

static HxPrivateSlot *
hx_private_slot (GPrivate * key,
                 gboolean create)
{
  DWORD index = hx_private_index (key);
  HxPrivateSlot * slot = FlsGetValue (index);

  if (slot == NULL && create)
  {
    slot = g_new0 (HxPrivateSlot, 1);
    slot->notify = key->notify;
    FlsSetValue (index, slot);
  }

  return slot;
}

gpointer
g_private_get (GPrivate * key)
{
  HxPrivateSlot * slot = hx_private_slot (key, FALSE);
  return (slot != NULL) ? slot->value : NULL;
}

void
g_private_set (GPrivate * key,
               gpointer value)
{
  HxPrivateSlot * slot = hx_private_slot (key, TRUE);
  slot->value = value;
}

void
g_private_replace (GPrivate * key,
                   gpointer value)
{
  HxPrivateSlot * slot = hx_private_slot (key, TRUE);

  if (slot->value != NULL && slot->notify != NULL && slot->value != value)
    slot->notify (slot->value);

  slot->value = value;
}

#else /* POSIX */

#include <pthread.h>

void
g_mutex_init (GMutex * mutex)
{
  pthread_mutex_t * m = g_new0 (pthread_mutex_t, 1);
  pthread_mutex_init (m, NULL);
  mutex->p = m;
}

void
g_mutex_clear (GMutex * mutex)
{
  if (mutex->p != NULL)
  {
    pthread_mutex_destroy (mutex->p);
    g_free (mutex->p);
    mutex->p = NULL;
  }
}

static pthread_mutex_t *
hx_mutex_get (GMutex * mutex)
{
  /* Lazy init for statically-declared GMutex. */
  if (g_atomic_pointer_get (&mutex->p) == NULL)
  {
    pthread_mutex_t * m = g_new0 (pthread_mutex_t, 1);
    pthread_mutex_init (m, NULL);
    if (!g_atomic_pointer_compare_and_exchange (&mutex->p, NULL, m))
    {
      pthread_mutex_destroy (m);
      g_free (m);
    }
  }
  return mutex->p;
}

void
g_mutex_lock (GMutex * mutex)
{
  pthread_mutex_lock (hx_mutex_get (mutex));
}

gboolean
g_mutex_trylock (GMutex * mutex)
{
  return pthread_mutex_trylock (hx_mutex_get (mutex)) == 0;
}

void
g_mutex_unlock (GMutex * mutex)
{
  pthread_mutex_unlock (hx_mutex_get (mutex));
}

static pthread_mutex_t *
hx_rec_get (GRecMutex * mutex)
{
  if (g_atomic_pointer_get (&mutex->srw) == NULL)
  {
    pthread_mutex_t * m = g_new0 (pthread_mutex_t, 1);
    pthread_mutexattr_t attr;
    pthread_mutexattr_init (&attr);
    pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init (m, &attr);
    pthread_mutexattr_destroy (&attr);
    if (!g_atomic_pointer_compare_and_exchange (&mutex->srw, NULL, m))
    {
      pthread_mutex_destroy (m);
      g_free (m);
    }
  }
  return mutex->srw;
}

void
g_rec_mutex_init (GRecMutex * mutex)
{
  mutex->srw = NULL;
  (void) hx_rec_get (mutex);
}

void
g_rec_mutex_clear (GRecMutex * mutex)
{
  if (mutex->srw != NULL)
  {
    pthread_mutex_destroy (mutex->srw);
    g_free (mutex->srw);
    mutex->srw = NULL;
  }
}

void
g_rec_mutex_lock (GRecMutex * mutex)
{
  pthread_mutex_lock (hx_rec_get (mutex));
}

gboolean
g_rec_mutex_trylock (GRecMutex * mutex)
{
  return pthread_mutex_trylock (hx_rec_get (mutex)) == 0;
}

void
g_rec_mutex_unlock (GRecMutex * mutex)
{
  pthread_mutex_unlock (hx_rec_get (mutex));
}

typedef struct _HxPrivateKey HxPrivateKey;

struct _HxPrivateKey
{
  pthread_key_t key;
};

static pthread_key_t
hx_private_key (GPrivate * key)
{
  HxPrivateKey * pk;

  pk = g_atomic_pointer_get (&key->impl);
  if (pk == NULL)
  {
    pk = g_new0 (HxPrivateKey, 1);
    pthread_key_create (&pk->key, key->notify);
    if (!g_atomic_pointer_compare_and_exchange (&key->impl, NULL, pk))
    {
      pthread_key_delete (pk->key);
      g_free (pk);
      pk = g_atomic_pointer_get (&key->impl);
    }
  }

  return pk->key;
}

gpointer
g_private_get (GPrivate * key)
{
  return pthread_getspecific (hx_private_key (key));
}

void
g_private_set (GPrivate * key,
               gpointer value)
{
  pthread_setspecific (hx_private_key (key), value);
}

void
g_private_replace (GPrivate * key,
                   gpointer value)
{
  pthread_key_t k = hx_private_key (key);
  gpointer old = pthread_getspecific (k);

  if (old != NULL && key->notify != NULL && old != value)
    key->notify (old);

  pthread_setspecific (k, value);
}

#endif
