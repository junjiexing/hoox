/*
 * hoox nano-glib: string utility functions implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hxstrfuncs.h"
#include "hxmem.h"

#include <string.h>
#include <stdio.h>

hx_char *
hx_strdup (const hx_char * str)
{
  hx_size len;
  hx_char * copy;

  if (str == NULL)
    return NULL;

  len = strlen (str) + 1;
  copy = hx_malloc (len);
  memcpy (copy, str, len);

  return copy;
}

hx_char *
hx_strndup (const hx_char * str,
           hx_size n)
{
  hx_char * copy;
  hx_size len;

  if (str == NULL)
    return NULL;

  len = strlen (str);
  if (len > n)
    len = n;

  copy = hx_malloc (len + 1);
  memcpy (copy, str, len);
  copy[len] = '\0';

  return copy;
}

hx_char *
hx_strdup_vprintf (const hx_char * format,
                  va_list args)
{
  va_list args2;
  int needed;
  hx_char * result;

  va_copy (args2, args);
  needed = vsnprintf (NULL, 0, format, args2);
  va_end (args2);

  if (needed < 0)
    return hx_strdup ("");

  result = hx_malloc ((hx_size) needed + 1);
  vsnprintf (result, (hx_size) needed + 1, format, args);

  return result;
}

hx_char *
hx_strdup_printf (const hx_char * format,
                 ...)
{
  va_list args;
  hx_char * result;

  va_start (args, format);
  result = hx_strdup_vprintf (format, args);
  va_end (args);

  return result;
}

hx_char *
hx_strconcat (const hx_char * string1,
             ...)
{
  va_list args;
  hx_size total;
  hx_char * result;
  hx_char * p;
  const hx_char * s;

  if (string1 == NULL)
    return NULL;

  total = strlen (string1);
  va_start (args, string1);
  while ((s = va_arg (args, const hx_char *)) != NULL)
    total += strlen (s);
  va_end (args);

  result = hx_malloc (total + 1);
  p = result;

  {
    hx_size n1 = strlen (string1);
    memcpy (p, string1, n1);
    p += n1;
  }

  va_start (args, string1);
  while ((s = va_arg (args, const hx_char *)) != NULL)
  {
    hx_size n = strlen (s);
    memcpy (p, s, n);
    p += n;
  }
  va_end (args);

  *p = '\0';

  return result;
}

hx_int
hx_strcmp0 (const hx_char * str1,
           const hx_char * str2)
{
  if (str1 == NULL)
    return -(str1 != str2);
  if (str2 == NULL)
    return str1 != str2;
  return strcmp (str1, str2);
}

hx_boolean
hx_str_has_prefix (const hx_char * str,
                  const hx_char * prefix)
{
  return strncmp (str, prefix, strlen (prefix)) == 0;
}

hx_boolean
hx_str_has_suffix (const hx_char * str,
                  const hx_char * suffix)
{
  hx_size str_len = strlen (str);
  hx_size suffix_len = strlen (suffix);

  if (suffix_len > str_len)
    return FALSE;

  return strcmp (str + str_len - suffix_len, suffix) == 0;
}

hx_size
hx_strlcpy (hx_char * dest,
           const hx_char * src,
           hx_size dest_size)
{
  hx_size src_len = strlen (src);

  if (dest_size != 0)
  {
    hx_size n = (src_len < dest_size - 1) ? src_len : dest_size - 1;
    memcpy (dest, src, n);
    dest[n] = '\0';
  }

  return src_len;
}

hx_size
hx_strlcat (hx_char * dest,
           const hx_char * src,
           hx_size dest_size)
{
  hx_size dest_len = strlen (dest);
  hx_size src_len = strlen (src);

  if (dest_len < dest_size)
    hx_strlcpy (dest + dest_len, src, dest_size - dest_len);

  return dest_len + src_len;
}
