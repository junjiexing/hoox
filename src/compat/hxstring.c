/*
 * hoox nano-glib: GString implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxstring.h"
#include "hxmem.h"

#include <string.h>
#include <stdio.h>

static void
hx_string_maybe_expand (GString * s,
                        gsize extra)
{
  gsize want = s->len + extra + 1;
  gsize new_alloc;

  if (want <= s->allocated_len)
    return;

  new_alloc = (s->allocated_len != 0) ? s->allocated_len : 16;
  while (new_alloc < want)
    new_alloc *= 2;

  s->str = g_realloc (s->str, new_alloc);
  s->allocated_len = new_alloc;
}

GString *
g_string_sized_new (gsize dfl_size)
{
  GString * s = g_new0 (GString, 1);

  s->allocated_len = 0;
  s->len = 0;
  s->str = NULL;
  hx_string_maybe_expand (s, MAX (dfl_size, (gsize) 2));
  s->str[0] = '\0';

  return s;
}

GString *
g_string_new (const gchar * init)
{
  GString * s = g_string_sized_new ((init != NULL) ? strlen (init) : 2);

  if (init != NULL)
    g_string_append (s, init);

  return s;
}

gchar *
g_string_free (GString * string,
               gboolean free_segment)
{
  gchar * segment;

  if (string == NULL)
    return NULL;

  if (free_segment)
  {
    g_free (string->str);
    segment = NULL;
  }
  else
  {
    segment = string->str;
  }

  g_free (string);

  return segment;
}

GString *
g_string_truncate (GString * string,
                   gsize len)
{
  if (len < string->len)
  {
    string->len = len;
    string->str[len] = '\0';
  }
  return string;
}

GString *
g_string_assign (GString * string,
                 const gchar * rval)
{
  g_string_truncate (string, 0);
  return g_string_append (string, rval);
}

GString *
g_string_append_len (GString * string,
                     const gchar * val,
                     gssize len)
{
  gsize n = (len < 0) ? strlen (val) : (gsize) len;

  hx_string_maybe_expand (string, n);
  memcpy (string->str + string->len, val, n);
  string->len += n;
  string->str[string->len] = '\0';

  return string;
}

GString *
g_string_append (GString * string,
                 const gchar * val)
{
  return g_string_append_len (string, val, -1);
}

GString *
g_string_append_c (GString * string,
                   gchar c)
{
  hx_string_maybe_expand (string, 1);
  string->str[string->len++] = c;
  string->str[string->len] = '\0';
  return string;
}

GString *
g_string_prepend (GString * string,
                  const gchar * val)
{
  gsize n = strlen (val);

  hx_string_maybe_expand (string, n);
  memmove (string->str + n, string->str, string->len + 1);
  memcpy (string->str, val, n);
  string->len += n;

  return string;
}

GString *
g_string_append_vprintf (GString * string,
                         const gchar * format,
                         va_list args)
{
  va_list args2;
  int needed;

  va_copy (args2, args);
  needed = vsnprintf (NULL, 0, format, args2);
  va_end (args2);

  if (needed > 0)
  {
    hx_string_maybe_expand (string, (gsize) needed);
    vsnprintf (string->str + string->len, (gsize) needed + 1, format, args);
    string->len += (gsize) needed;
  }

  return string;
}

GString *
g_string_append_printf (GString * string,
                        const gchar * format,
                        ...)
{
  va_list args;

  va_start (args, format);
  g_string_append_vprintf (string, format, args);
  va_end (args);

  return string;
}
