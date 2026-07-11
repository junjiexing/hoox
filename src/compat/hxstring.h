/*
 * hoox nano-glib: HxString.
 *
 * Growable string buffer, mainly used by the tests and target functions.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_STRING_H__
#define __HOOX_COMPAT_STRING_H__

#include "hxdefs.h"

#include <stdarg.h>

HX_BEGIN_DECLS

typedef struct _HxString HxString;

struct _HxString
{
  hx_char * str;
  hx_size len;
  hx_size allocated_len;
};

/* hoox:test-only-begin */
HxString * hx_string_new (const hx_char * init);
/* hoox:test-only-end */
HxString * hx_string_sized_new (hx_size dfl_size);
hx_char * hx_string_free (HxString * string, hx_boolean free_segment);

HxString * hx_string_truncate (HxString * string, hx_size len);
HxString * hx_string_append (HxString * string, const hx_char * val);
HxString * hx_string_append_c (HxString * string, hx_char c);
HxString * hx_string_append_len (HxString * string, const hx_char * val,
    hx_ssize len);
/* hoox:test-only-begin */
HxString * hx_string_prepend (HxString * string, const hx_char * val);
/* hoox:test-only-end */
HxString * hx_string_append_printf (HxString * string, const hx_char * format, ...)
    HX_GNUC_PRINTF (2, 3);
HxString * hx_string_append_vprintf (HxString * string, const hx_char * format,
    va_list args);

HX_END_DECLS

#endif
