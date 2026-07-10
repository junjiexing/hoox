/*
 * hoox nano-glib: assertions and logging.
 *
 * Minimal replacements for g_assert / g_return_* / g_warning etc. Logging is
 * routed to stderr; fatal paths abort(). No GLib log domains.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_MESSAGES_H__
#define __HOOX_COMPAT_MESSAGES_H__

#include "hxdefs.h"

#include <stdio.h>
#include <stdlib.h>

G_BEGIN_DECLS

#define g_error(...) \
    G_STMT_START { \
      fprintf (stderr, "** ERROR: " __VA_ARGS__); \
      fprintf (stderr, "\n"); \
      abort (); \
    } G_STMT_END

#define g_critical(...) \
    G_STMT_START { \
      fprintf (stderr, "** CRITICAL: " __VA_ARGS__); \
      fprintf (stderr, "\n"); \
    } G_STMT_END

#define g_warning(...) \
    G_STMT_START { \
      fprintf (stderr, "** WARNING: " __VA_ARGS__); \
      fprintf (stderr, "\n"); \
    } G_STMT_END

#define g_message(...) \
    G_STMT_START { \
      fprintf (stderr, __VA_ARGS__); \
      fprintf (stderr, "\n"); \
    } G_STMT_END

#define g_debug(...)   G_STMT_START { } G_STMT_END
#define g_info(...)    G_STMT_START { } G_STMT_END
#define g_print(...)   ((void) fprintf (stdout, __VA_ARGS__))
#define g_printerr(...) ((void) fprintf (stderr, __VA_ARGS__))

#define g_assert(expr) \
    G_STMT_START { \
      if (G_UNLIKELY (!(expr))) \
      { \
        fprintf (stderr, "%s:%d: assertion failed: %s\n", \
            __FILE__, __LINE__, #expr); \
        abort (); \
      } \
    } G_STMT_END

#define g_assert_not_reached() \
    G_STMT_START { \
      fprintf (stderr, "%s:%d: code should not be reached\n", \
          __FILE__, __LINE__); \
      abort (); \
    } G_STMT_END

#define g_assert_true(expr)   g_assert ((expr))
#define g_assert_false(expr)  g_assert (!(expr))
#define g_assert_null(expr)   g_assert ((expr) == NULL)
#define g_assert_nonnull(expr) g_assert ((expr) != NULL)

#define g_assert_cmpint(a, op, b)  g_assert ((a) op (b))
#define g_assert_cmpuint(a, op, b) g_assert ((a) op (b))
#define g_assert_cmphex(a, op, b)  g_assert ((a) op (b))
#define g_assert_cmpstr(a, op, b)  g_assert (strcmp ((a), (b)) op 0)
#define g_assert_cmpptr(a, op, b)  g_assert ((a) op (b))

#define g_return_if_fail(expr) \
    G_STMT_START { \
      if (G_UNLIKELY (!(expr))) \
      { \
        fprintf (stderr, "%s:%d: assertion '%s' failed\n", \
            __FILE__, __LINE__, #expr); \
        return; \
      } \
    } G_STMT_END

#define g_return_val_if_fail(expr, val) \
    G_STMT_START { \
      if (G_UNLIKELY (!(expr))) \
      { \
        fprintf (stderr, "%s:%d: assertion '%s' failed\n", \
            __FILE__, __LINE__, #expr); \
        return (val); \
      } \
    } G_STMT_END

#define g_return_if_reached() \
    G_STMT_START { \
      fprintf (stderr, "%s:%d: should not be reached\n", __FILE__, __LINE__); \
      return; \
    } G_STMT_END

#define g_return_val_if_reached(val) \
    G_STMT_START { \
      fprintf (stderr, "%s:%d: should not be reached\n", __FILE__, __LINE__); \
      return (val); \
    } G_STMT_END

G_END_DECLS

#endif
