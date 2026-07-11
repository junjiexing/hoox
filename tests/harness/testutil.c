/*
 * hoox — trimmed test utilities implementation.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "testutil.h"

#include <string.h>

hx_char *
test_util_diff_binary (const hx_uint8 * expected_bytes,
                       hx_uint expected_length,
                       const hx_uint8 * actual_bytes,
                       hx_uint actual_length)
{
  HxString * s = hx_string_sized_new (256);
  hx_uint n = MAX (expected_length, actual_length);
  hx_uint i;

  hx_string_append_printf (s, "\nexpected (%u bytes):\n  ", expected_length);
  for (i = 0; i != expected_length; i++)
    hx_string_append_printf (s, "%02x ", expected_bytes[i]);

  hx_string_append_printf (s, "\nactual (%u bytes):\n  ", actual_length);
  for (i = 0; i != actual_length; i++)
    hx_string_append_printf (s, "%02x ", actual_bytes[i]);

  hx_string_append (s, "\ndiff:\n  ");
  for (i = 0; i != n; i++)
  {
    hx_boolean e_ok = (i < expected_length);
    hx_boolean a_ok = (i < actual_length);
    if (e_ok && a_ok && expected_bytes[i] == actual_bytes[i])
      hx_string_append (s, "   ");
    else
      hx_string_append (s, "^^ ");
  }
  hx_string_append_c (s, '\n');

  return hx_string_free (s, FALSE);
}

hx_char *
test_util_diff_text (const hx_char * expected_text,
                     const hx_char * actual_text)
{
  HxString * s = hx_string_sized_new (256);
  hx_string_append_printf (s, "\nexpected: %s\nactual:   %s\n",
      (expected_text != NULL) ? expected_text : "(null)",
      (actual_text != NULL) ? actual_text : "(null)");
  return hx_string_free (s, FALSE);
}
