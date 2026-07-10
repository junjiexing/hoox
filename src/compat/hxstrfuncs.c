/*
 * hoox nano-glib: string utility functions implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxstrfuncs.h"
#include "hxmem.h"

#include <string.h>
#include <stdio.h>

gchar *
g_strdup (const gchar * str)
{
  gsize len;
  gchar * copy;

  if (str == NULL)
    return NULL;

  len = strlen (str) + 1;
  copy = g_malloc (len);
  memcpy (copy, str, len);

  return copy;
}

gchar *
g_strndup (const gchar * str,
           gsize n)
{
  gchar * copy;
  gsize len;

  if (str == NULL)
    return NULL;

  len = strlen (str);
  if (len > n)
    len = n;

  copy = g_malloc (len + 1);
  memcpy (copy, str, len);
  copy[len] = '\0';

  return copy;
}

gchar *
g_strdup_vprintf (const gchar * format,
                  va_list args)
{
  va_list args2;
  int needed;
  gchar * result;

  va_copy (args2, args);
  needed = vsnprintf (NULL, 0, format, args2);
  va_end (args2);

  if (needed < 0)
    return g_strdup ("");

  result = g_malloc ((gsize) needed + 1);
  vsnprintf (result, (gsize) needed + 1, format, args);

  return result;
}

gchar *
g_strdup_printf (const gchar * format,
                 ...)
{
  va_list args;
  gchar * result;

  va_start (args, format);
  result = g_strdup_vprintf (format, args);
  va_end (args);

  return result;
}

gchar *
g_strconcat (const gchar * string1,
             ...)
{
  va_list args;
  gsize total;
  gchar * result;
  gchar * p;
  const gchar * s;

  if (string1 == NULL)
    return NULL;

  total = strlen (string1);
  va_start (args, string1);
  while ((s = va_arg (args, const gchar *)) != NULL)
    total += strlen (s);
  va_end (args);

  result = g_malloc (total + 1);
  p = result;

  {
    gsize n1 = strlen (string1);
    memcpy (p, string1, n1);
    p += n1;
  }

  va_start (args, string1);
  while ((s = va_arg (args, const gchar *)) != NULL)
  {
    gsize n = strlen (s);
    memcpy (p, s, n);
    p += n;
  }
  va_end (args);

  *p = '\0';

  return result;
}

gint
g_strcmp0 (const gchar * str1,
           const gchar * str2)
{
  if (str1 == NULL)
    return -(str1 != str2);
  if (str2 == NULL)
    return str1 != str2;
  return strcmp (str1, str2);
}

gboolean
g_str_has_prefix (const gchar * str,
                  const gchar * prefix)
{
  return strncmp (str, prefix, strlen (prefix)) == 0;
}

gboolean
g_str_has_suffix (const gchar * str,
                  const gchar * suffix)
{
  gsize str_len = strlen (str);
  gsize suffix_len = strlen (suffix);

  if (suffix_len > str_len)
    return FALSE;

  return strcmp (str + str_len - suffix_len, suffix) == 0;
}

gsize
g_strlcpy (gchar * dest,
           const gchar * src,
           gsize dest_size)
{
  gsize src_len = strlen (src);

  if (dest_size != 0)
  {
    gsize n = (src_len < dest_size - 1) ? src_len : dest_size - 1;
    memcpy (dest, src, n);
    dest[n] = '\0';
  }

  return src_len;
}

gsize
g_strlcat (gchar * dest,
           const gchar * src,
           gsize dest_size)
{
  gsize dest_len = strlen (dest);
  gsize src_len = strlen (src);

  if (dest_len < dest_size)
    g_strlcpy (dest + dest_len, src, dest_size - dest_len);

  return dest_len + src_len;
}
