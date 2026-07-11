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

HX_BEGIN_DECLS

hx_char * test_util_diff_binary (const hx_uint8 * expected_bytes,
    hx_uint expected_length, const hx_uint8 * actual_bytes, hx_uint actual_length);
hx_char * test_util_diff_text (const hx_char * expected_text,
    const hx_char * actual_text);

HX_END_DECLS

#endif
