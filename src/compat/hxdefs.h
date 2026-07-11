/*
 * hoox nano-glib: fundamental types and utility macros.
 *
 * A tiny, dependency-free reimplementation of the subset of GLib's
 * types/macros that the extracted frida-gum hook engine relies on. The
 * public surface intentionally keeps GLib's names (gint, gpointer,
 * G_BEGIN_DECLS, MIN/MAX, ...) so that extracted gum code compiles with only
 * its <glib.h> include swapped for <hxglib.h>.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_DEFS_H__
#define __HOOX_COMPAT_DEFS_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#ifdef __cplusplus
# define G_BEGIN_DECLS extern "C" {
# define G_END_DECLS }
#else
# define G_BEGIN_DECLS
# define G_END_DECLS
#endif

/* Platform detection macros (GLib-style) that extracted gum code expects. */
#if defined (_WIN32) && !defined (G_OS_WIN32)
# define G_OS_WIN32 1
#endif
#if !defined (_WIN32) && !defined (G_OS_UNIX)
# define G_OS_UNIX 1
#endif

G_BEGIN_DECLS

/* ---- fundamental types -------------------------------------------------- */

typedef int             gint;
typedef unsigned int    guint;
typedef short           gshort;
typedef unsigned short  gushort;
typedef long            glong;
typedef unsigned long   gulong;
typedef char            gchar;
typedef unsigned char   guchar;
typedef int             gboolean;

typedef int8_t          gint8;
typedef uint8_t         guint8;
typedef int16_t         gint16;
typedef uint16_t        guint16;
typedef int32_t         gint32;
typedef uint32_t        guint32;
typedef int64_t         gint64;
typedef uint64_t        guint64;

typedef float           gfloat;
typedef double          gdouble;

typedef size_t          gsize;
typedef ptrdiff_t       gssize;
typedef intptr_t        gintptr;
typedef uintptr_t       guintptr;
typedef gint64          goffset;

typedef void *          gpointer;
typedef const void *    gconstpointer;

typedef guint32         gunichar;

/* GObject/GLib type-system stand-ins: the hook path never registers or uses
 * these, but extracted declarations reference the typedefs. */
typedef guint32         GQuark;
typedef gsize           GType;
typedef struct _GError  GError;   /* opaque; only pointer decls survive */

/* ---- callback typedefs -------------------------------------------------- */

typedef void     (* GDestroyNotify) (gpointer data);
typedef void     (* GFreeFunc)      (gpointer data);
typedef guint    (* GHashFunc)      (gconstpointer key);
typedef gboolean (* GEqualFunc)     (gconstpointer a, gconstpointer b);
typedef gint     (* GCompareFunc)   (gconstpointer a, gconstpointer b);
typedef gint     (* GCompareDataFunc) (gconstpointer a, gconstpointer b,
                                       gpointer user_data);
typedef void     (* GFunc)          (gpointer data, gpointer user_data);
typedef void     (* GHFunc)         (gpointer key, gpointer value,
                                     gpointer user_data);
typedef gboolean (* GHRFunc)        (gpointer key, gpointer value,
                                     gpointer user_data);
typedef gpointer (* GCopyFunc)      (gconstpointer src, gpointer data);
typedef void     (* GCallback)      (void);

/* ---- truth values ------------------------------------------------------- */

#ifndef FALSE
# define FALSE (0)
#endif
#ifndef TRUE
# define TRUE (!FALSE)
#endif
#ifndef NULL
# define NULL ((void *) 0)
#endif

/* ---- limits ------------------------------------------------------------- */

#define G_MININT8   INT8_MIN
#define G_MAXINT8   INT8_MAX
#define G_MAXUINT8  UINT8_MAX
#define G_MININT16  INT16_MIN
#define G_MAXINT16  INT16_MAX
#define G_MAXUINT16 UINT16_MAX
#define G_MININT32  INT32_MIN
#define G_MAXINT32  INT32_MAX
#define G_MAXUINT32 UINT32_MAX
#define G_MININT64  INT64_MIN
#define G_MAXINT64  INT64_MAX
#define G_MAXUINT64 UINT64_MAX
#define G_MININT    INT_MIN
#define G_MAXINT    INT_MAX
#define G_MAXUINT   UINT_MAX
#define G_MINLONG   LONG_MIN
#define G_MAXLONG   LONG_MAX
#define G_MAXULONG  ULONG_MAX
#define G_MAXSIZE   SIZE_MAX
#define G_MAXSSIZE  PTRDIFF_MAX
#define G_MINSSIZE  PTRDIFF_MIN

#define G_GINT64_CONSTANT(val)  (val##LL)
#define G_GUINT64_CONSTANT(val) (val##ULL)

/* ---- pointer size / endianness ------------------------------------------ */

#if UINTPTR_MAX == 0xffffffffffffffffull
# define GLIB_SIZEOF_VOID_P 8
#else
# define GLIB_SIZEOF_VOID_P 4
#endif

#define G_LITTLE_ENDIAN 1234
#define G_BIG_ENDIAN    4321

#if defined (__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define G_BYTE_ORDER G_BIG_ENDIAN
#else
# define G_BYTE_ORDER G_LITTLE_ENDIAN
#endif

/* ---- format modifiers --------------------------------------------------- */

#define G_GINT64_MODIFIER  "ll"
#define G_GUINT64_FORMAT   "llu"
#define G_GINT64_FORMAT    "lld"
#if GLIB_SIZEOF_VOID_P == 8
# define G_GSIZE_MODIFIER  "ll"
# define G_GSIZE_FORMAT    "llu"
# define G_GSSIZE_FORMAT   "lld"
#else
# define G_GSIZE_MODIFIER  ""
# define G_GSIZE_FORMAT    "u"
# define G_GSSIZE_FORMAT   "d"
#endif

/* ---- compiler attributes ------------------------------------------------ */

#if defined (__GNUC__) || defined (__clang__)
# define G_GNUC_UNUSED            __attribute__ ((__unused__))
# define G_GNUC_NORETURN          __attribute__ ((__noreturn__))
# define G_GNUC_CONST             __attribute__ ((__const__))
# define G_GNUC_PURE              __attribute__ ((__pure__))
# define G_GNUC_WARN_UNUSED_RESULT __attribute__ ((__warn_unused_result__))
# define G_GNUC_PRINTF(f, a)      __attribute__ ((__format__ (__printf__, f, a)))
# define G_GNUC_NULL_TERMINATED   __attribute__ ((__sentinel__))
# define G_GNUC_INTERNAL          __attribute__ ((__visibility__ ("hidden")))
# define G_LIKELY(expr)           (__builtin_expect (!!(expr), 1))
# define G_UNLIKELY(expr)         (__builtin_expect (!!(expr), 0))
# define G_GNUC_FALLTHROUGH       __attribute__ ((__fallthrough__))
# define G_ALIGNOF(t)             __alignof__ (t)
# define G_GNUC_ALIGNED(n)        __attribute__ ((__aligned__ (n)))
# define G_GNUC_PACKED            __attribute__ ((__packed__))
# define G_NORETURN               __attribute__ ((__noreturn__))
#else
# define G_GNUC_UNUSED
# define G_GNUC_NORETURN          __declspec (noreturn)
# define G_GNUC_CONST
# define G_GNUC_PURE
# define G_GNUC_WARN_UNUSED_RESULT
# define G_GNUC_PRINTF(f, a)
# define G_GNUC_NULL_TERMINATED
# define G_GNUC_INTERNAL
# define G_LIKELY(expr)           (expr)
# define G_UNLIKELY(expr)         (expr)
# define G_GNUC_FALLTHROUGH
# define G_ALIGNOF(t)             __alignof (t)
# define G_GNUC_ALIGNED(n)        __declspec (align (n))
# define G_GNUC_PACKED
# define G_NORETURN               __declspec (noreturn)
#endif

#define G_ANALYZER_NORETURN

/* ---- statement wrappers ------------------------------------------------- */

#define G_STMT_START do
#define G_STMT_END   while (0)

/* ---- misc utility macros ------------------------------------------------ */

#define G_N_ELEMENTS(arr) (sizeof (arr) / sizeof ((arr)[0]))
#define G_STRUCT_OFFSET(struct_type, member) offsetof (struct_type, member)
#define G_STRUCT_MEMBER_P(struct_p, offset) \
    ((gpointer) ((guint8 *) (struct_p) + (glong) (offset)))
#define G_STRUCT_MEMBER(member_type, struct_p, offset) \
    (*(member_type *) G_STRUCT_MEMBER_P ((struct_p), (offset)))

#ifndef MIN
# define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
# define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef ABS
# define ABS(a) (((a) < 0) ? -(a) : (a))
#endif
#ifndef CLAMP
# define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

#define GPOINTER_TO_INT(p)   ((gint) (gintptr) (p))
#define GPOINTER_TO_UINT(p)  ((guint) (guintptr) (p))
#define GINT_TO_POINTER(i)   ((gpointer) (gintptr) (i))
#define GUINT_TO_POINTER(u)  ((gpointer) (guintptr) (u))
#define GPOINTER_TO_SIZE(p)  ((gsize) (p))
#define GSIZE_TO_POINTER(s)  ((gpointer) (gsize) (s))

#define G_STATIC_ASSERT(expr) \
    typedef char _GStaticAssert[(expr) ? 1 : -1] G_GNUC_UNUSED

/* Byte order: hoox targets are little-endian (x86/ARM/ARM64/MIPSEL), so the
 * LE conversions are identities; provide byte-swapping BE forms for
 * completeness. */
#if defined (__GNUC__) || defined (__clang__)
# define GUINT16_SWAP(v) __builtin_bswap16 (v)
# define GUINT32_SWAP(v) __builtin_bswap32 (v)
# define GUINT64_SWAP(v) __builtin_bswap64 (v)
#else
# define GUINT16_SWAP(v) _byteswap_ushort (v)
# define GUINT32_SWAP(v) _byteswap_ulong (v)
# define GUINT64_SWAP(v) _byteswap_uint64 (v)
#endif

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
# define GUINT16_TO_LE(v)   ((guint16) (v))
# define GUINT32_TO_LE(v)   ((guint32) (v))
# define GUINT64_TO_LE(v)   ((guint64) (v))
# define GUINT16_TO_BE(v)   ((guint16) GUINT16_SWAP (v))
# define GUINT32_TO_BE(v)   ((guint32) GUINT32_SWAP (v))
# define GUINT64_TO_BE(v)   ((guint64) GUINT64_SWAP (v))
#else
# define GUINT16_TO_LE(v)   ((guint16) GUINT16_SWAP (v))
# define GUINT32_TO_LE(v)   ((guint32) GUINT32_SWAP (v))
# define GUINT64_TO_LE(v)   ((guint64) GUINT64_SWAP (v))
# define GUINT16_TO_BE(v)   ((guint16) (v))
# define GUINT32_TO_BE(v)   ((guint32) (v))
# define GUINT64_TO_BE(v)   ((guint64) (v))
#endif
#define GUINT16_FROM_LE(v)  GUINT16_TO_LE (v)
#define GUINT32_FROM_LE(v)  GUINT32_TO_LE (v)
#define GUINT64_FROM_LE(v)  GUINT64_TO_LE (v)
#define GINT16_TO_LE(v)     ((gint16) GUINT16_TO_LE ((guint16) (v)))
#define GINT32_TO_LE(v)     ((gint32) GUINT32_TO_LE ((guint32) (v)))
#define GINT64_TO_LE(v)     ((gint64) GUINT64_TO_LE ((guint64) (v)))
#define GINT16_FROM_LE(v)   GINT16_TO_LE (v)
#define GINT32_FROM_LE(v)   GINT32_TO_LE (v)
#define GINT64_FROM_LE(v)   GINT64_TO_LE (v)

#if defined (_MSC_VER)
# include <malloc.h>
# define g_alloca(n) _alloca (n)
#else
# define g_alloca(n) __builtin_alloca (n)
#endif

#define G_STRINGIFY(macro_or_string) G_STRINGIFY_ARG (macro_or_string)
#define G_STRINGIFY_ARG(contents) #contents

/* g_steal_pointer / g_clear_pointer live with the memory helpers but the
 * inline forms are handy from headers, so declare them here. */
static inline gpointer
g_steal_pointer (gpointer pp)
{
  gpointer * ptr = (gpointer *) pp;
  gpointer ref = *ptr;
  *ptr = NULL;
  return ref;
}

G_END_DECLS

#endif
