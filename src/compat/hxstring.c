/*
 * hoox nano-glib: HxString implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxstring.h"
#include "hxmem.h"

#include <string.h>
#include <stdio.h>

static void
hx_string_maybe_expand (HxString * s,
                        hx_size extra)
{
  hx_size want = s->len + extra + 1;
  hx_size new_alloc;

  if (want <= s->allocated_len)
    return;

  new_alloc = (s->allocated_len != 0) ? s->allocated_len : 16;
  while (new_alloc < want)
    new_alloc *= 2;

  s->str = hx_realloc (s->str, new_alloc);
  s->allocated_len = new_alloc;
}

HxString *
hx_string_sized_new (hx_size dfl_size)
{
  HxString * s = hx_new0 (HxString, 1);

  s->allocated_len = 0;
  s->len = 0;
  s->str = NULL;
  hx_string_maybe_expand (s, MAX (dfl_size, (hx_size) 2));
  s->str[0] = '\0';

  return s;
}

HxString *
hx_string_new (const hx_char * init)
{
  HxString * s = hx_string_sized_new ((init != NULL) ? strlen (init) : 2);

  if (init != NULL)
    hx_string_append (s, init);

  return s;
}

hx_char *
hx_string_free (HxString * string,
               hx_boolean free_segment)
{
  hx_char * segment;

  if (string == NULL)
    return NULL;

  if (free_segment)
  {
    hx_free (string->str);
    segment = NULL;
  }
  else
  {
    segment = string->str;
  }

  hx_free (string);

  return segment;
}

HxString *
hx_string_truncate (HxString * string,
                   hx_size len)
{
  if (len < string->len)
  {
    string->len = len;
    string->str[len] = '\0';
  }
  return string;
}

HxString *
hx_string_assign (HxString * string,
                 const hx_char * rval)
{
  hx_string_truncate (string, 0);
  return hx_string_append (string, rval);
}

HxString *
hx_string_append_len (HxString * string,
                     const hx_char * val,
                     hx_ssize len)
{
  hx_size n = (len < 0) ? strlen (val) : (hx_size) len;

  hx_string_maybe_expand (string, n);
  memcpy (string->str + string->len, val, n);
  string->len += n;
  string->str[string->len] = '\0';

  return string;
}

HxString *
hx_string_append (HxString * string,
                 const hx_char * val)
{
  return hx_string_append_len (string, val, -1);
}

HxString *
hx_string_append_c (HxString * string,
                   hx_char c)
{
  hx_string_maybe_expand (string, 1);
  string->str[string->len++] = c;
  string->str[string->len] = '\0';
  return string;
}

HxString *
hx_string_prepend (HxString * string,
                  const hx_char * val)
{
  hx_size n = strlen (val);

  hx_string_maybe_expand (string, n);
  memmove (string->str + n, string->str, string->len + 1);
  memcpy (string->str, val, n);
  string->len += n;

  return string;
}

HxString *
hx_string_append_vprintf (HxString * string,
                         const hx_char * format,
                         va_list args)
{
  va_list args2;
  int needed;

  va_copy (args2, args);
  needed = vsnprintf (NULL, 0, format, args2);
  va_end (args2);

  if (needed > 0)
  {
    hx_string_maybe_expand (string, (hx_size) needed);
    vsnprintf (string->str + string->len, (hx_size) needed + 1, format, args);
    string->len += (hx_size) needed;
  }

  return string;
}

HxString *
hx_string_append_printf (HxString * string,
                        const hx_char * format,
                        ...)
{
  va_list args;

  va_start (args, format);
  hx_string_append_vprintf (string, format, args);
  va_end (args);

  return string;
}
