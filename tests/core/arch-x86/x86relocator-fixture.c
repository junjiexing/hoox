/*
 * Copyright (C) 2009-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxx86relocator.h"

#include "hooxmemory.h"
#include "testutil.h"

#include <string.h>

#define TESTCASE(NAME) \
    void test_relocator_ ## NAME ( \
        TestRelocatorFixture * fixture, hx_constpointer data)
#define TESTENTRY(NAME) \
    TESTENTRY_WITH_FIXTURE ("Core/X86Relocator", test_relocator, NAME, \
        TestRelocatorFixture)

#define TEST_OUTBUF_SIZE 32

typedef struct _TestRelocatorFixture
{
  hx_uint8 * output;
  HooxX86Writer cw;
  HooxX86Relocator rl;
} TestRelocatorFixture;

static void
test_relocator_fixture_setup (TestRelocatorFixture * fixture,
                              hx_constpointer data)
{
  hx_uint page_size;
  hx_uint8 stack_data[1] = { 42 };
  HooxAddressSpec as;

  page_size = hoox_query_page_size ();

  as.near_address = (hx_pointer) stack_data;
  as.max_distance = HX_MAXINT32 - page_size;

  fixture->output = (hx_uint8 *) hoox_alloc_n_pages_near (1, HOOX_PAGE_RW, &as);
  memset (fixture->output, 0, page_size);

  hoox_x86_writer_init (&fixture->cw, fixture->output);
}

static void
test_relocator_fixture_teardown (TestRelocatorFixture * fixture,
                                 hx_constpointer data)
{
  hoox_x86_relocator_clear (&fixture->rl);
  hoox_x86_writer_clear (&fixture->cw);
  hoox_free_pages (fixture->output);
}

static void
test_relocator_fixture_assert_output_equals (TestRelocatorFixture * fixture,
                                             const hx_uint8 * expected_code,
                                             hx_uint expected_length)
{
  hx_uint actual_length;
  hx_boolean same_length, same_content;

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
      hx_print ("\n\nRelocated code is not equal to expected code:\n\n%s\n",
          diff);
      hx_free (diff);
    }
    else
    {
      hx_print ("\n\nNo code was relocated!\n\n");
    }
  }

  hx_assert_true (same_length);
  hx_assert_true (same_content);
}

static const hx_uint8 cleared_outbuf[TEST_OUTBUF_SIZE] = { 0, };

#define SETUP_RELOCATOR_WITH(CODE) \
    hoox_x86_relocator_init (&fixture->rl, CODE, &fixture->cw)

#define assert_outbuf_still_zeroed_from_offset(OFF) \
    hx_assert_cmpint (memcmp (fixture->output + OFF, cleared_outbuf + OFF, \
        sizeof (cleared_outbuf) - OFF), ==, 0)
#define assert_output_equals(e) test_relocator_fixture_assert_output_equals \
    (fixture, e, sizeof (e))
