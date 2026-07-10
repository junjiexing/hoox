/*
 * hoox nano-glib: GString.
 *
 * Growable string buffer, mainly used by the tests and target functions.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_STRING_H__
#define __HOOX_COMPAT_STRING_H__

#include "hxdefs.h"

#include <stdarg.h>

G_BEGIN_DECLS

typedef struct _GString GString;

struct _GString
{
  gchar * str;
  gsize len;
  gsize allocated_len;
};

GString * g_string_new (const gchar * init);
GString * g_string_sized_new (gsize dfl_size);
gchar * g_string_free (GString * string, gboolean free_segment);

GString * g_string_assign (GString * string, const gchar * rval);
GString * g_string_truncate (GString * string, gsize len);
GString * g_string_append (GString * string, const gchar * val);
GString * g_string_append_c (GString * string, gchar c);
GString * g_string_append_len (GString * string, const gchar * val,
    gssize len);
GString * g_string_prepend (GString * string, const gchar * val);
GString * g_string_append_printf (GString * string, const gchar * format, ...)
    G_GNUC_PRINTF (2, 3);
GString * g_string_append_vprintf (GString * string, const gchar * format,
    va_list args);

G_END_DECLS

#endif
