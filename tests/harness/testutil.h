/*
 * hoox — trimmed test utilities.
 *
 * A minimal replacement for frida-gum's tests/testutil.h, providing just what
 * the ported writer/relocator/interceptor tests reference (the test macros via
 * the harness, plus the binary/text diff helpers used in assertion messages).
 * The heap/prof/exceptor bits of the original are intentionally omitted.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_TESTUTIL_H__
#define __HOOX_TESTUTIL_H__

#include "testrunner.h"

G_BEGIN_DECLS

gchar * test_util_diff_binary (const guint8 * expected_bytes,
    guint expected_length, const guint8 * actual_bytes, guint actual_length);
gchar * test_util_diff_text (const gchar * expected_text,
    const gchar * actual_text);

G_END_DECLS

#endif
