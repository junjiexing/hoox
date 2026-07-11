/*
 * hoox nano-glib: threading primitives.
 *
 * HxMutex / HxRecMutex / HxPrivate. Types are defined with opaque, zero-
 * initialisable storage so that statically-declared, implicitly-initialised
 * mutexes (as GLib allows) work without an init race. The native backing is
 * cast inside hxthread.c (Win32 SRWLOCK / FLS; pthread on POSIX).
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_THREAD_H__
#define __HOOX_COMPAT_THREAD_H__

#include "hxdefs.h"

HX_BEGIN_DECLS

typedef struct _HxMutex HxMutex;
typedef struct _HxRecMutex HxRecMutex;
typedef struct _HxPrivate HxPrivate;
typedef void HxThread;

/* Opaque per-thread identity (current OS thread id as a pointer). */
HxThread * hx_thread_self (void);

#define HX_USEC_PER_SEC 1000000
void hx_usleep (hx_ulong microseconds);

struct _HxMutex
{
  void * p;           /* SRWLOCK (one pointer) / pthread_mutex_t * */
};

struct _HxRecMutex
{
  void * srw;         /* SRWLOCK */
  hx_size owner;        /* owning thread id (0 = none) */
  hx_uint32 count;      /* recursion depth */
#if HX_SIZEOF_VOID_P == 4
  hx_uint32 pad;
#endif
};

struct _HxPrivate
{
  void * impl;   /* lazily-allocated native key wrapper (atomic access) */
  HxDestroyNotify notify;
};

#define HX_PRIVATE_INIT(notify) { NULL, (notify) }

void hx_mutex_init (HxMutex * mutex);
void hx_mutex_clear (HxMutex * mutex);
void hx_mutex_lock (HxMutex * mutex);
hx_boolean hx_mutex_trylock (HxMutex * mutex);
void hx_mutex_unlock (HxMutex * mutex);

void hx_rec_mutex_init (HxRecMutex * mutex);
void hx_rec_mutex_clear (HxRecMutex * mutex);
void hx_rec_mutex_lock (HxRecMutex * mutex);
hx_boolean hx_rec_mutex_trylock (HxRecMutex * mutex);
void hx_rec_mutex_unlock (HxRecMutex * mutex);

hx_pointer hx_private_get (HxPrivate * key);
void hx_private_set (HxPrivate * key, hx_pointer value);
void hx_private_replace (HxPrivate * key, hx_pointer value);

/* one-time initialisation (bracketing enter/leave). Simple global-lock
 * implementation: correct for non-nesting inits (cpu features, etc.). */
hx_boolean hx_once_init_enter_impl (volatile void * location);
void hx_once_init_leave_impl (volatile void * location, hx_size result);

#define hx_once_init_enter(location) \
    (hx_atomic_pointer_get ((hx_size *) (void *) (location)) == 0 \
        ? hx_once_init_enter_impl ((location)) \
        : FALSE)
#define hx_once_init_leave(location, result) \
    hx_once_init_leave_impl ((location), (hx_size) (result))

HX_END_DECLS

#endif
