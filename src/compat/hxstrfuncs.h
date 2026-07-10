/*
 * hoox nano-glib: string utility functions.
 *
 * g_strdup family and the string predicates used by extracted gum code.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_STRFUNCS_H__
#define __HOOX_COMPAT_STRFUNCS_H__

#include "hxdefs.h"

#include <stdarg.h>

G_BEGIN_DECLS

gchar * g_strdup (const gchar * str);
gchar * g_strndup (const gchar * str, gsize n);
gchar * g_strdup_printf (const gchar * format, ...) G_GNUC_PRINTF (1, 2);
gchar * g_strdup_vprintf (const gchar * format, va_list args);
gchar * g_strconcat (const gchar * string1, ...) G_GNUC_NULL_TERMINATED;

gint g_strcmp0 (const gchar * str1, const gchar * str2);
gboolean g_str_has_prefix (const gchar * str, const gchar * prefix);
gboolean g_str_has_suffix (const gchar * str, const gchar * suffix);

gsize g_strlcpy (gchar * dest, const gchar * src, gsize dest_size);
gsize g_strlcat (gchar * dest, const gchar * src, gsize dest_size);

#include <stdio.h>
#define g_snprintf(str, n, ...) ((void) snprintf ((str), (n), __VA_ARGS__))

G_END_DECLS

#endif
