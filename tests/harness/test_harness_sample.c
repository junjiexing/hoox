/*
 * Self-test for the hoox test harness: exercises fixture setup/teardown,
 * TESTENTRY_WITH_FIXTURE, TESTENTRY_SIMPLE, and the assertion macros.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "testrunner.h"

typedef struct _TestSampleFixture TestSampleFixture;

struct _TestSampleFixture
{
  hx_int value;
};

static void
test_sample_fixture_setup (TestSampleFixture * fixture,
                           hx_constpointer data)
{
  (void) data;
  fixture->value = 41;
}

static void
test_sample_fixture_teardown (TestSampleFixture * fixture,
                              hx_constpointer data)
{
  (void) fixture;
  (void) data;
}

#define TESTCASE(NAME) \
    void test_sample_ ##NAME (TestSampleFixture * fixture, hx_constpointer data)
#define TESTENTRY(NAME) \
    TESTENTRY_WITH_FIXTURE ("Sample", test_sample, NAME, TestSampleFixture)

TESTLIST_BEGIN (sample)
  TESTENTRY (fixture_is_initialised)
  TESTENTRY (arithmetic)
TESTLIST_END ()

TESTCASE (fixture_is_initialised)
{
  (void) data;
  hx_assert_cmpint (fixture->value, ==, 41);
  fixture->value++;
  hx_assert_cmpint (fixture->value, ==, 42);
}

TESTCASE (arithmetic)
{
  (void) fixture;
  (void) data;
  hx_assert_cmpint (2 + 2, ==, 4);
  hx_assert_true (TRUE);
  hx_assert_null (NULL);
}

static void
test_sample_standalone (void)
{
  hx_assert_cmphex (0xdeadbeef, ==, 0xdeadbeef);
}

TESTLIST_BEGIN (sample_simple)
  TESTENTRY_SIMPLE ("SampleSimple", test_sample, standalone)
TESTLIST_END ()

int
main (int argc, char ** argv)
{
  hx_test_init (&argc, &argv, NULL);

  TESTLIST_REGISTER (sample);
  TESTLIST_REGISTER (sample_simple);

  return hx_test_run ();
}
