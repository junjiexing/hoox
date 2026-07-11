/*
 * Copyright (C) 2009-2010 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxx86writer.h"

#include "hooxmemory.h"
#include "testutil.h"

#include <string.h>

#define TESTCASE(NAME) \
    void test_code_writer_ ## NAME ( \
        TestCodeWriterFixture * fixture, hx_constpointer data)
#define TESTENTRY(NAME) \
    TESTENTRY_WITH_FIXTURE ("Core/X86Writer", test_code_writer, NAME, \
        TestCodeWriterFixture)

typedef struct _TestCodeWriterFixture
{
  hx_uint8 output[32];
  HooxX86Writer cw;
} TestCodeWriterFixture;

static void
test_code_writer_fixture_setup (TestCodeWriterFixture * fixture,
                                hx_constpointer data)
{
  hoox_x86_writer_init (&fixture->cw, fixture->output);

  hoox_x86_writer_set_target_cpu (&fixture->cw, HOOX_CPU_AMD64);
  hoox_x86_writer_set_target_abi (&fixture->cw, HOOX_ABI_WINDOWS);
}

static void
test_code_writer_fixture_teardown (TestCodeWriterFixture * fixture,
                                   hx_constpointer data)
{
  hoox_x86_writer_clear (&fixture->cw);
}

static void
test_code_writer_fixture_assert_output_equals (TestCodeWriterFixture * fixture,
                                               const hx_uint8 * expected_code,
                                               hx_uint expected_length)
{
  hx_uint actual_length;
  hx_boolean same_length, same_content;

  hoox_x86_writer_flush (&fixture->cw);

  actual_length = hoox_x86_writer_offset (&fixture->cw);
  same_length = (actual_length == expected_length);
  if (same_length)
  {
    same_content =
        memcmp (fixture->output, expected_code, expected_length) == 0;
  }
  else
  {
    same_content = FALSE;
  }

  if (!same_length || !same_content)
  {
    hx_char * diff;

    if (actual_length != 0)
    {
      diff = test_util_diff_binary (expected_code, expected_length,
          fixture->output, actual_length);
      hx_print ("\n\nGenerated code is not equal to expected code:\n\n%s\n",
          diff);
      hx_free (diff);
    }
    else
    {
      hx_print ("\n\nNo code was generated!\n\n");
    }
  }

  hx_assert_true (same_length);
  hx_assert_true (same_content);
}

#ifdef HAVE_I386
static void hoox_test_native_function (const hx_char * arg1, const hx_char * arg2,
    const hx_char * arg3, const hx_char * arg4);
#endif

#define assert_output_equals(e) \
    test_code_writer_fixture_assert_output_equals (fixture, e, sizeof (e))
