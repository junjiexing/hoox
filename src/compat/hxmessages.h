/*
 * hoox nano-glib: assertions and logging.
 *
 * Minimal replacements for hx_assert / hx_return_* / hx_warning etc. Logging is
 * routed to stderr; fatal paths abort(). No GLib log domains.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_MESSAGES_H__
#define __HOOX_COMPAT_MESSAGES_H__

#include "hxdefs.h"

#include <stdio.h>
#include <stdlib.h>

HX_BEGIN_DECLS

#define hx_error(...) \
    HX_STMT_START { \
      fprintf (stderr, "** ERROR: " __VA_ARGS__); \
      fprintf (stderr, "\n"); \
      abort (); \
    } HX_STMT_END

#define hx_critical(...) \
    HX_STMT_START { \
      fprintf (stderr, "** CRITICAL: " __VA_ARGS__); \
      fprintf (stderr, "\n"); \
    } HX_STMT_END

#define hx_warning(...) \
    HX_STMT_START { \
      fprintf (stderr, "** WARNING: " __VA_ARGS__); \
      fprintf (stderr, "\n"); \
    } HX_STMT_END

#define hx_message(...) \
    HX_STMT_START { \
      fprintf (stderr, __VA_ARGS__); \
      fprintf (stderr, "\n"); \
    } HX_STMT_END

#define hx_debug(...)   HX_STMT_START { } HX_STMT_END
#define hx_info(...)    HX_STMT_START { } HX_STMT_END
#define hx_print(...)   ((void) fprintf (stdout, __VA_ARGS__))
#define hx_printerr(...) ((void) fprintf (stderr, __VA_ARGS__))

#define hx_assert(expr) \
    HX_STMT_START { \
      if (HX_UNLIKELY (!(expr))) \
      { \
        fprintf (stderr, "%s:%d: assertion failed: %s\n", \
            __FILE__, __LINE__, #expr); \
        abort (); \
      } \
    } HX_STMT_END

#define hx_abort() abort ()

#define hx_assert_not_reached() \
    HX_STMT_START { \
      fprintf (stderr, "%s:%d: code should not be reached\n", \
          __FILE__, __LINE__); \
      abort (); \
    } HX_STMT_END

#define hx_assert_true(expr)   hx_assert ((expr))
#define hx_assert_false(expr)  hx_assert (!(expr))
#define hx_assert_null(expr)   hx_assert ((expr) == NULL)
#define hx_assert_nonnull(expr) hx_assert ((expr) != NULL)

#define hx_assert_cmpint(a, op, b)  hx_assert ((a) op (b))
#define hx_assert_cmpuint(a, op, b) hx_assert ((a) op (b))
#define hx_assert_cmphex(a, op, b)  hx_assert ((a) op (b))
#define hx_assert_cmpstr(a, op, b)  hx_assert (strcmp ((a), (b)) op 0)
#define hx_assert_cmpptr(a, op, b)  hx_assert ((a) op (b))

#define hx_return_if_fail(expr) \
    HX_STMT_START { \
      if (HX_UNLIKELY (!(expr))) \
      { \
        fprintf (stderr, "%s:%d: assertion '%s' failed\n", \
            __FILE__, __LINE__, #expr); \
        return; \
      } \
    } HX_STMT_END

#define hx_return_val_if_fail(expr, val) \
    HX_STMT_START { \
      if (HX_UNLIKELY (!(expr))) \
      { \
        fprintf (stderr, "%s:%d: assertion '%s' failed\n", \
            __FILE__, __LINE__, #expr); \
        return (val); \
      } \
    } HX_STMT_END

#define hx_return_if_reached() \
    HX_STMT_START { \
      fprintf (stderr, "%s:%d: should not be reached\n", __FILE__, __LINE__); \
      return; \
    } HX_STMT_END

#define hx_return_val_if_reached(val) \
    HX_STMT_START { \
      fprintf (stderr, "%s:%d: should not be reached\n", __FILE__, __LINE__); \
      return (val); \
    } HX_STMT_END

/* HxError: the hook path never surfaces errors this way. Provide no-op
 * stand-ins so extracted code that calls hx_set_error still compiles. */
#define hx_set_error(err, domain, code, ...)      ((void) 0)
#define hx_set_error_literal(err, domain, code, m) ((void) 0)
#define hx_clear_error(err)                        ((void) 0)
#define hx_error_free(err)                         ((void) 0)
#define hx_propagate_error(dest, src)              ((void) 0)

HX_END_DECLS

#endif
