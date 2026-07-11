/*
 * hoox nano-glib: fundamental types and utility macros.
 *
 * A tiny, dependency-free reimplementation of the subset of GLib's
 * types/macros that the extracted frida-gum hook engine relies on. The
 * public surface intentionally keeps GLib's names (hx_int, hx_pointer,
 * HX_BEGIN_DECLS, MIN/MAX, ...) so that extracted hoox code compiles with only
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
# define HX_BEGIN_DECLS extern "C" {
# define HX_END_DECLS }
#else
# define HX_BEGIN_DECLS
# define HX_END_DECLS
#endif

/* Platform detection macros (GLib-style) that extracted hoox code expects. */
#if defined (_WIN32) && !defined (HX_OS_WIN32)
# define HX_OS_WIN32 1
#endif
#if !defined (_WIN32) && !defined (HX_OS_UNIX)
# define HX_OS_UNIX 1
#endif

HX_BEGIN_DECLS

/* ---- fundamental types -------------------------------------------------- */

typedef int             hx_int;
typedef unsigned int    hx_uint;
typedef short           hx_short;
typedef unsigned short  hx_ushort;
typedef long            hx_long;
typedef unsigned long   hx_ulong;
typedef char            hx_char;
typedef unsigned char   hx_uchar;
typedef int             hx_boolean;

typedef int8_t          hx_int8;
typedef uint8_t         hx_uint8;
typedef int16_t         hx_int16;
typedef uint16_t        hx_uint16;
typedef int32_t         hx_int32;
typedef uint32_t        hx_uint32;
typedef int64_t         hx_int64;
typedef uint64_t        hx_uint64;

typedef float           hx_float;
typedef double          hx_double;

typedef size_t          hx_size;
typedef ptrdiff_t       hx_ssize;
typedef intptr_t        hx_intptr;
typedef uintptr_t       hx_uintptr;
typedef hx_int64          hx_offset;

typedef void *          hx_pointer;
typedef const void *    hx_constpointer;

typedef hx_uint32         hx_unichar;

/* GObject/GLib type-system stand-ins: the hook path never registers or uses
 * these, but extracted declarations reference the typedefs. */
typedef hx_uint32         HxQuark;
typedef hx_size           HxType;
typedef struct _HxError  HxError;   /* opaque; only pointer decls survive */

/* ---- callback typedefs -------------------------------------------------- */

typedef void     (* HxDestroyNotify) (hx_pointer data);
typedef void     (* HxFreeFunc)      (hx_pointer data);
typedef hx_uint    (* HxHashFunc)      (hx_constpointer key);
typedef hx_boolean (* HxEqualFunc)     (hx_constpointer a, hx_constpointer b);
typedef hx_int     (* HxCompareFunc)   (hx_constpointer a, hx_constpointer b);
typedef hx_int     (* HxCompareDataFunc) (hx_constpointer a, hx_constpointer b,
                                       hx_pointer user_data);
typedef void     (* HxFunc)          (hx_pointer data, hx_pointer user_data);
typedef void     (* HxHFunc)         (hx_pointer key, hx_pointer value,
                                     hx_pointer user_data);
typedef hx_boolean (* HxHRFunc)        (hx_pointer key, hx_pointer value,
                                     hx_pointer user_data);
typedef hx_pointer (* HxCopyFunc)      (hx_constpointer src, hx_pointer data);
typedef void     (* HxCallback)      (void);

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

#define HX_MININT8   INT8_MIN
#define HX_MAXINT8   INT8_MAX
#define HX_MAXUINT8  UINT8_MAX
#define HX_MININT16  INT16_MIN
#define HX_MAXINT16  INT16_MAX
#define HX_MAXUINT16 UINT16_MAX
#define HX_MININT32  INT32_MIN
#define HX_MAXINT32  INT32_MAX
#define HX_MAXUINT32 UINT32_MAX
#define HX_MININT64  INT64_MIN
#define HX_MAXINT64  INT64_MAX
#define HX_MAXUINT64 UINT64_MAX
#define HX_MININT    INT_MIN
#define HX_MAXINT    INT_MAX
#define HX_MAXUINT   UINT_MAX
#define HX_MINLONG   LONG_MIN
#define HX_MAXLONG   LONG_MAX
#define HX_MAXULONG  ULONG_MAX
#define HX_MAXSIZE   SIZE_MAX
#define HX_MAXSSIZE  PTRDIFF_MAX
#define HX_MINSSIZE  PTRDIFF_MIN

#define HX_INT64_CONSTANT(val)  (val##LL)
#define HX_UINT64_CONSTANT(val) (val##ULL)

/* ---- pointer size / endianness ------------------------------------------ */

#if UINTPTR_MAX == 0xffffffffffffffffull
# define HX_SIZEOF_VOID_P 8
#else
# define HX_SIZEOF_VOID_P 4
#endif

#define HX_LITTLE_ENDIAN 1234
#define HX_BIG_ENDIAN    4321

#if defined (__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define HX_BYTE_ORDER HX_BIG_ENDIAN
#else
# define HX_BYTE_ORDER HX_LITTLE_ENDIAN
#endif

/* ---- format modifiers --------------------------------------------------- */

#define HX_INT64_MODIFIER  "ll"
#define HX_UINT64_FORMAT   "llu"
#define HX_INT64_FORMAT    "lld"
#if HX_SIZEOF_VOID_P == 8
# define HX_SIZE_MODIFIER  "ll"
# define HX_SIZE_FORMAT    "llu"
# define HX_SSIZE_FORMAT   "lld"
#else
# define HX_SIZE_MODIFIER  ""
# define HX_SIZE_FORMAT    "u"
# define HX_SSIZE_FORMAT   "d"
#endif

/* ---- compiler attributes ------------------------------------------------ */

#if defined (__GNUC__) || defined (__clang__)
# define HX_GNUC_UNUSED            __attribute__ ((__unused__))
# define HX_GNUC_NORETURN          __attribute__ ((__noreturn__))
# define HX_GNUC_CONST             __attribute__ ((__const__))
# define HX_GNUC_PURE              __attribute__ ((__pure__))
# define HX_GNUC_WARN_UNUSED_RESULT __attribute__ ((__warn_unused_result__))
# define HX_GNUC_PRINTF(f, a)      __attribute__ ((__format__ (__printf__, f, a)))
# define HX_GNUC_NULL_TERMINATED   __attribute__ ((__sentinel__))
/* PE/COFF (Windows) has no ELF-style symbol visibility; the attribute is
 * ignored there and gcc warns about it, so drop it on Windows targets. */
# if defined (_WIN32)
#  define HX_GNUC_INTERNAL
# else
#  define HX_GNUC_INTERNAL         __attribute__ ((__visibility__ ("hidden")))
# endif
# define HX_LIKELY(expr)           (__builtin_expect (!!(expr), 1))
# define HX_UNLIKELY(expr)         (__builtin_expect (!!(expr), 0))
# define HX_GNUC_FALLTHROUGH       __attribute__ ((__fallthrough__))
# define HX_ALIGNOF(t)             __alignof__ (t)
# define HX_GNUC_ALIGNED(n)        __attribute__ ((__aligned__ (n)))
# define HX_GNUC_PACKED            __attribute__ ((__packed__))
# define HX_NORETURN               __attribute__ ((__noreturn__))
#else
# define HX_GNUC_UNUSED
# define HX_GNUC_NORETURN          __declspec (noreturn)
# define HX_GNUC_CONST
# define HX_GNUC_PURE
# define HX_GNUC_WARN_UNUSED_RESULT
# define HX_GNUC_PRINTF(f, a)
# define HX_GNUC_NULL_TERMINATED
# define HX_GNUC_INTERNAL
# define HX_LIKELY(expr)           (expr)
# define HX_UNLIKELY(expr)         (expr)
# define HX_GNUC_FALLTHROUGH
# define HX_ALIGNOF(t)             __alignof (t)
# define HX_GNUC_ALIGNED(n)        __declspec (align (n))
# define HX_GNUC_PACKED
# define HX_NORETURN               __declspec (noreturn)
#endif

#define HX_ANALYZER_NORETURN

/* ---- statement wrappers ------------------------------------------------- */

#define HX_STMT_START do
#define HX_STMT_END   while (0)

/* ---- misc utility macros ------------------------------------------------ */

#define HX_N_ELEMENTS(arr) (sizeof (arr) / sizeof ((arr)[0]))
#define HX_STRUCT_OFFSET(struct_type, member) offsetof (struct_type, member)
#define HX_STRUCT_MEMBER_P(struct_p, offset) \
    ((hx_pointer) ((hx_uint8 *) (struct_p) + (hx_long) (offset)))
#define HX_STRUCT_MEMBER(member_type, struct_p, offset) \
    (*(member_type *) HX_STRUCT_MEMBER_P ((struct_p), (offset)))

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

#define HX_POINTER_TO_INT(p)   ((hx_int) (hx_intptr) (p))
#define HX_POINTER_TO_UINT(p)  ((hx_uint) (hx_uintptr) (p))
#define HX_INT_TO_POINTER(i)   ((hx_pointer) (hx_intptr) (i))
#define HX_UINT_TO_POINTER(u)  ((hx_pointer) (hx_uintptr) (u))
#define HX_POINTER_TO_SIZE(p)  ((hx_size) (p))
#define HX_SIZE_TO_POINTER(s)  ((hx_pointer) (hx_size) (s))

#define HX_STATIC_ASSERT(expr) \
    typedef char _GStaticAssert[(expr) ? 1 : -1] HX_GNUC_UNUSED

/* Byte order: hoox targets are little-endian (x86/ARM/ARM64/MIPSEL), so the
 * LE conversions are identities; provide byte-swapping BE forms for
 * completeness. */
#if defined (__GNUC__) || defined (__clang__)
# define HX_UINT16_SWAP(v) __builtin_bswap16 (v)
# define HX_UINT32_SWAP(v) __builtin_bswap32 (v)
# define HX_UINT64_SWAP(v) __builtin_bswap64 (v)
#else
# define HX_UINT16_SWAP(v) _byteswap_ushort (v)
# define HX_UINT32_SWAP(v) _byteswap_ulong (v)
# define HX_UINT64_SWAP(v) _byteswap_uint64 (v)
#endif

#if HX_BYTE_ORDER == HX_LITTLE_ENDIAN
# define HX_UINT16_TO_LE(v)   ((hx_uint16) (v))
# define HX_UINT32_TO_LE(v)   ((hx_uint32) (v))
# define HX_UINT64_TO_LE(v)   ((hx_uint64) (v))
# define HX_UINT16_TO_BE(v)   ((hx_uint16) HX_UINT16_SWAP (v))
# define HX_UINT32_TO_BE(v)   ((hx_uint32) HX_UINT32_SWAP (v))
# define HX_UINT64_TO_BE(v)   ((hx_uint64) HX_UINT64_SWAP (v))
#else
# define HX_UINT16_TO_LE(v)   ((hx_uint16) HX_UINT16_SWAP (v))
# define HX_UINT32_TO_LE(v)   ((hx_uint32) HX_UINT32_SWAP (v))
# define HX_UINT64_TO_LE(v)   ((hx_uint64) HX_UINT64_SWAP (v))
# define HX_UINT16_TO_BE(v)   ((hx_uint16) (v))
# define HX_UINT32_TO_BE(v)   ((hx_uint32) (v))
# define HX_UINT64_TO_BE(v)   ((hx_uint64) (v))
#endif
#define HX_UINT16_FROM_LE(v)  HX_UINT16_TO_LE (v)
#define HX_UINT32_FROM_LE(v)  HX_UINT32_TO_LE (v)
#define HX_UINT64_FROM_LE(v)  HX_UINT64_TO_LE (v)
#define HX_INT16_TO_LE(v)     ((hx_int16) HX_UINT16_TO_LE ((hx_uint16) (v)))
#define HX_INT32_TO_LE(v)     ((hx_int32) HX_UINT32_TO_LE ((hx_uint32) (v)))
#define HX_INT64_TO_LE(v)     ((hx_int64) HX_UINT64_TO_LE ((hx_uint64) (v)))
#define HX_INT16_FROM_LE(v)   HX_INT16_TO_LE (v)
#define HX_INT32_FROM_LE(v)   HX_INT32_TO_LE (v)
#define HX_INT64_FROM_LE(v)   HX_INT64_TO_LE (v)

#if defined (_MSC_VER)
# include <malloc.h>
# define hx_alloca(n) _alloca (n)
#else
# define hx_alloca(n) __builtin_alloca (n)
#endif

#define HX_STRINGIFY(macro_or_string) HX_STRINGIFY_ARG (macro_or_string)
#define HX_STRINGIFY_ARG(contents) #contents

/* hx_steal_pointer / hx_clear_pointer live with the memory helpers but the
 * inline forms are handy from headers, so declare them here. */
static inline hx_pointer
hx_steal_pointer (hx_pointer pp)
{
  hx_pointer * ptr = (hx_pointer *) pp;
  hx_pointer ref = *ptr;
  *ptr = NULL;
  return ref;
}

HX_END_DECLS

#endif
