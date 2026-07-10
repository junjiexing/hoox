/*
 * hoox nano-glib: atomics.
 *
 * g_atomic_int_* / g_atomic_pointer_* over plain (non-_Atomic-qualified)
 * memory, matching GLib's contract. Backed by __atomic builtins on
 * clang/gcc and by _Interlocked* intrinsics on MSVC. Full sequential
 * consistency is used throughout (conservative; correct for the interceptor's
 * lock-free listener-entry COW).
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_ATOMIC_H__
#define __HOOX_COMPAT_ATOMIC_H__

#include "hxdefs.h"

G_BEGIN_DECLS

#if defined (__GNUC__) || defined (__clang__)

static inline gint
g_atomic_int_get (const volatile gint * atomic)
{
  return __atomic_load_n (atomic, __ATOMIC_SEQ_CST);
}

static inline void
g_atomic_int_set (volatile gint * atomic,
                  gint newval)
{
  __atomic_store_n (atomic, newval, __ATOMIC_SEQ_CST);
}

static inline void
g_atomic_int_inc (volatile gint * atomic)
{
  (void) __atomic_fetch_add (atomic, 1, __ATOMIC_SEQ_CST);
}

static inline gboolean
g_atomic_int_dec_and_test (volatile gint * atomic)
{
  return __atomic_fetch_sub (atomic, 1, __ATOMIC_SEQ_CST) == 1;
}

static inline gint
g_atomic_int_add (volatile gint * atomic,
                  gint val)
{
  return __atomic_fetch_add (atomic, val, __ATOMIC_SEQ_CST);
}

static inline gboolean
g_atomic_int_compare_and_exchange (volatile gint * atomic,
                                   gint oldval,
                                   gint newval)
{
  return __atomic_compare_exchange_n (atomic, &oldval, newval, 0,
      __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#define g_atomic_pointer_get(atomic) \
    __atomic_load_n ((atomic), __ATOMIC_SEQ_CST)
#define g_atomic_pointer_set(atomic, newval) \
    __atomic_store_n ((atomic), (newval), __ATOMIC_SEQ_CST)
#define g_atomic_pointer_compare_and_exchange(atomic, oldval, newval) \
    __extension__ ({ \
      __typeof__ (*(atomic)) _hx_old = (oldval); \
      __atomic_compare_exchange_n ((atomic), &_hx_old, (newval), 0, \
          __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
    })

#else /* MSVC */

# include <intrin.h>

static inline gint
g_atomic_int_get (const volatile gint * atomic)
{
  gint v = *atomic;
  _ReadWriteBarrier ();
  return v;
}

static inline void
g_atomic_int_set (volatile gint * atomic,
                  gint newval)
{
  _ReadWriteBarrier ();
  *atomic = newval;
  _ReadWriteBarrier ();
}

static inline void
g_atomic_int_inc (volatile gint * atomic)
{
  (void) _InterlockedIncrement ((volatile long *) atomic);
}

static inline gboolean
g_atomic_int_dec_and_test (volatile gint * atomic)
{
  return _InterlockedDecrement ((volatile long *) atomic) == 0;
}

static inline gint
g_atomic_int_add (volatile gint * atomic,
                  gint val)
{
  return _InterlockedExchangeAdd ((volatile long *) atomic, val);
}

static inline gboolean
g_atomic_int_compare_and_exchange (volatile gint * atomic,
                                   gint oldval,
                                   gint newval)
{
  return _InterlockedCompareExchange ((volatile long *) atomic, newval,
      oldval) == oldval;
}

static inline gpointer
hx_atomic_pointer_get (void * const volatile * atomic)
{
  gpointer v = (gpointer) *atomic;
  _ReadWriteBarrier ();
  return v;
}

static inline void
hx_atomic_pointer_set (void * volatile * atomic,
                       gpointer newval)
{
  _ReadWriteBarrier ();
  *atomic = newval;
  _ReadWriteBarrier ();
}

static inline gboolean
hx_atomic_pointer_cas (void * volatile * atomic,
                       gpointer oldval,
                       gpointer newval)
{
  return _InterlockedCompareExchangePointer (atomic, newval, oldval) == oldval;
}

# define g_atomic_pointer_get(atomic) \
    hx_atomic_pointer_get ((void * const volatile *) (atomic))
# define g_atomic_pointer_set(atomic, newval) \
    hx_atomic_pointer_set ((void * volatile *) (atomic), (gpointer) (newval))
# define g_atomic_pointer_compare_and_exchange(atomic, oldval, newval) \
    hx_atomic_pointer_cas ((void * volatile *) (atomic), \
        (gpointer) (oldval), (gpointer) (newval))

#endif

G_END_DECLS

#endif
