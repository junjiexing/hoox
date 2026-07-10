/*
 * hoox nano-glib: threading primitives.
 *
 * GMutex / GRecMutex / GPrivate. Types are defined with opaque, zero-
 * initialisable storage so that statically-declared, implicitly-initialised
 * mutexes (as GLib allows) work without an init race. The native backing is
 * cast inside hxthread.c (Win32 SRWLOCK / FLS; pthread on POSIX).
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_THREAD_H__
#define __HOOX_COMPAT_THREAD_H__

#include "hxdefs.h"

G_BEGIN_DECLS

typedef struct _GMutex GMutex;
typedef struct _GRecMutex GRecMutex;
typedef struct _GPrivate GPrivate;

struct _GMutex
{
  void * p;           /* SRWLOCK (one pointer) / pthread_mutex_t * */
};

struct _GRecMutex
{
  void * srw;         /* SRWLOCK */
  gsize owner;        /* owning thread id (0 = none) */
  guint32 count;      /* recursion depth */
#if GLIB_SIZEOF_VOID_P == 4
  guint32 pad;
#endif
};

struct _GPrivate
{
  void * impl;   /* lazily-allocated native key wrapper (atomic access) */
  GDestroyNotify notify;
};

#define G_PRIVATE_INIT(notify) { NULL, (notify) }

void g_mutex_init (GMutex * mutex);
void g_mutex_clear (GMutex * mutex);
void g_mutex_lock (GMutex * mutex);
gboolean g_mutex_trylock (GMutex * mutex);
void g_mutex_unlock (GMutex * mutex);

void g_rec_mutex_init (GRecMutex * mutex);
void g_rec_mutex_clear (GRecMutex * mutex);
void g_rec_mutex_lock (GRecMutex * mutex);
gboolean g_rec_mutex_trylock (GRecMutex * mutex);
void g_rec_mutex_unlock (GRecMutex * mutex);

gpointer g_private_get (GPrivate * key);
void g_private_set (GPrivate * key, gpointer value);
void g_private_replace (GPrivate * key, gpointer value);

/* one-time initialisation (bracketing enter/leave). Simple global-lock
 * implementation: correct for non-nesting inits (cpu features, etc.). */
gboolean g_once_init_enter_impl (volatile void * location);
void g_once_init_leave_impl (volatile void * location, gsize result);

#define g_once_init_enter(location) \
    (g_atomic_pointer_get ((gsize *) (void *) (location)) == 0 \
        ? g_once_init_enter_impl ((location)) \
        : FALSE)
#define g_once_init_leave(location, result) \
    g_once_init_leave_impl ((location), (gsize) (result))

G_END_DECLS

#endif
