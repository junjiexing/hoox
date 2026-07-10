/*
 * hoox — trimmed test utilities implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "testutil.h"

#include <string.h>

gchar *
test_util_diff_binary (const guint8 * expected_bytes,
                       guint expected_length,
                       const guint8 * actual_bytes,
                       guint actual_length)
{
  GString * s = g_string_sized_new (256);
  guint n = MAX (expected_length, actual_length);
  guint i;

  g_string_append_printf (s, "\nexpected (%u bytes):\n  ", expected_length);
  for (i = 0; i != expected_length; i++)
    g_string_append_printf (s, "%02x ", expected_bytes[i]);

  g_string_append_printf (s, "\nactual (%u bytes):\n  ", actual_length);
  for (i = 0; i != actual_length; i++)
    g_string_append_printf (s, "%02x ", actual_bytes[i]);

  g_string_append (s, "\ndiff:\n  ");
  for (i = 0; i != n; i++)
  {
    gboolean e_ok = (i < expected_length);
    gboolean a_ok = (i < actual_length);
    if (e_ok && a_ok && expected_bytes[i] == actual_bytes[i])
      g_string_append (s, "   ");
    else
      g_string_append (s, "^^ ");
  }
  g_string_append_c (s, '\n');

  return g_string_free (s, FALSE);
}

gchar *
test_util_diff_text (const gchar * expected_text,
                     const gchar * actual_text)
{
  GString * s = g_string_sized_new (256);
  g_string_append_printf (s, "\nexpected: %s\nactual:   %s\n",
      (expected_text != NULL) ? expected_text : "(null)",
      (actual_text != NULL) ? actual_text : "(null)");
  return g_string_free (s, FALSE);
}
