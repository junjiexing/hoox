/*
 * hoox nano-glib: atomics.
 *
 * hx_atomic_int_* / hx_atomic_pointer_* over plain (non-_Atomic-qualified)
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

HX_BEGIN_DECLS

#if defined (__GNUC__) || defined (__clang__)

static inline hx_int
hx_atomic_int_get (const volatile hx_int * atomic)
{
  return __atomic_load_n (atomic, __ATOMIC_SEQ_CST);
}

static inline void
hx_atomic_int_set (volatile hx_int * atomic,
                  hx_int newval)
{
  __atomic_store_n (atomic, newval, __ATOMIC_SEQ_CST);
}

static inline void
hx_atomic_int_inc (volatile hx_int * atomic)
{
  (void) __atomic_fetch_add (atomic, 1, __ATOMIC_SEQ_CST);
}

static inline hx_boolean
hx_atomic_int_dec_and_test (volatile hx_int * atomic)
{
  return __atomic_fetch_sub (atomic, 1, __ATOMIC_SEQ_CST) == 1;
}

static inline hx_int
hx_atomic_int_add (volatile hx_int * atomic,
                  hx_int val)
{
  return __atomic_fetch_add (atomic, val, __ATOMIC_SEQ_CST);
}

static inline hx_boolean
hx_atomic_int_compare_and_exchange (volatile hx_int * atomic,
                                   hx_int oldval,
                                   hx_int newval)
{
  return __atomic_compare_exchange_n (atomic, &oldval, newval, 0,
      __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#define hx_atomic_pointer_get(atomic) \
    __atomic_load_n ((atomic), __ATOMIC_SEQ_CST)
#define hx_atomic_pointer_set(atomic, newval) \
    __atomic_store_n ((atomic), (newval), __ATOMIC_SEQ_CST)
#define hx_atomic_pointer_compare_and_exchange(atomic, oldval, newval) \
    __extension__ ({ \
      __typeof__ (*(atomic)) _hx_old = (oldval); \
      __atomic_compare_exchange_n ((atomic), &_hx_old, (newval), 0, \
          __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); \
    })

#else /* MSVC */

# include <intrin.h>

static inline hx_int
hx_atomic_int_get (const volatile hx_int * atomic)
{
  hx_int v = *atomic;
  _ReadWriteBarrier ();
  return v;
}

static inline void
hx_atomic_int_set (volatile hx_int * atomic,
                  hx_int newval)
{
  _ReadWriteBarrier ();
  *atomic = newval;
  _ReadWriteBarrier ();
}

static inline void
hx_atomic_int_inc (volatile hx_int * atomic)
{
  (void) _InterlockedIncrement ((volatile long *) atomic);
}

static inline hx_boolean
hx_atomic_int_dec_and_test (volatile hx_int * atomic)
{
  return _InterlockedDecrement ((volatile long *) atomic) == 0;
}

static inline hx_int
hx_atomic_int_add (volatile hx_int * atomic,
                  hx_int val)
{
  return _InterlockedExchangeAdd ((volatile long *) atomic, val);
}

static inline hx_boolean
hx_atomic_int_compare_and_exchange (volatile hx_int * atomic,
                                   hx_int oldval,
                                   hx_int newval)
{
  return _InterlockedCompareExchange ((volatile long *) atomic, newval,
      oldval) == oldval;
}

static inline hx_pointer
hx_atomic_pointer_get (void * const volatile * atomic)
{
  hx_pointer v = (hx_pointer) *atomic;
  _ReadWriteBarrier ();
  return v;
}

static inline void
hx_atomic_pointer_set (void * volatile * atomic,
                       hx_pointer newval)
{
  _ReadWriteBarrier ();
  *atomic = newval;
  _ReadWriteBarrier ();
}

static inline hx_boolean
hx_atomic_pointer_cas (void * volatile * atomic,
                       hx_pointer oldval,
                       hx_pointer newval)
{
  return _InterlockedCompareExchangePointer (atomic, newval, oldval) == oldval;
}

# define hx_atomic_pointer_get(atomic) \
    hx_atomic_pointer_get ((void * const volatile *) (atomic))
# define hx_atomic_pointer_set(atomic, newval) \
    hx_atomic_pointer_set ((void * volatile *) (atomic), (hx_pointer) (newval))
# define hx_atomic_pointer_compare_and_exchange(atomic, oldval, newval) \
    hx_atomic_pointer_cas ((void * volatile *) (atomic), \
        (hx_pointer) (oldval), (hx_pointer) (newval))

#endif

HX_END_DECLS

#endif
