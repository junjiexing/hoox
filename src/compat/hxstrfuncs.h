/*
 * hoox nano-glib: string utility functions.
 *
 * hx_strdup family and the string predicates used by extracted hoox code.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_STRFUNCS_H__
#define __HOOX_COMPAT_STRFUNCS_H__

#include "hxdefs.h"

#include <stdarg.h>

HX_BEGIN_DECLS

hx_char * hx_strdup (const hx_char * str);
/* hoox:test-only-begin */
hx_char * hx_strdup_printf (const hx_char * format, ...) HX_GNUC_PRINTF (1, 2);
/* hoox:test-only-end */
hx_char * hx_strdup_vprintf (const hx_char * format, va_list args);
hx_char * hx_strconcat (const hx_char * string1, ...) HX_GNUC_NULL_TERMINATED;

hx_boolean hx_str_has_prefix (const hx_char * str, const hx_char * prefix);

hx_size hx_strlcpy (hx_char * dest, const hx_char * src, hx_size dest_size);

#include <stdio.h>
#define hx_snprintf(str, n, ...) ((void) snprintf ((str), (n), __VA_ARGS__))

HX_END_DECLS

#endif
