/*
 * hoox test harness — a GLib-free reimplementation of the frida-gum test
 * scaffolding (TESTLIST / TESTENTRY / TESTCASE + the hx_test_* subset the
 * ported tests use).
 *
 * A single test binary registers many test modules; hx_test_run() iterates the
 * registry, honouring an optional path filter. Assertion failures (hx_assert*)
 * abort the process (matching GTest-on-Windows), so a non-zero exit means
 * failure — which is what ctest keys on.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_TEST_RUNNER_H__
#define __HOOX_TEST_RUNNER_H__

#include "hxglib.h"

HX_BEGIN_DECLS

typedef void (* HxTestFixtureFunc) (hx_pointer fixture, hx_constpointer user_data);
typedef void (* HxTestSimpleFunc) (void);

void hx_test_init (int * argc, char *** argv, void * unused);
int hx_test_run (void);
void hx_test_message (const char * format, ...) HX_GNUC_PRINTF (1, 2);
void hx_test_fail (void);

void hx_test_add_vtable (const char * path, hx_size fixture_size,
    hx_constpointer fixture_data, HxTestFixtureFunc setup,
    HxTestFixtureFunc test, HxTestFixtureFunc teardown);
void hx_test_add_simple (const char * path, HxTestSimpleFunc func);

#define hx_test_add_func(path, func) \
    hx_test_add_simple ((path), (HxTestSimpleFunc) (func))

#define hx_test_add(testpath, Fixture, tdata, fsetup, ftest, fteardown) \
    hx_test_add_vtable ((testpath), sizeof (Fixture), \
        (hx_constpointer) (tdata), \
        (HxTestFixtureFunc) (fsetup), \
        (HxTestFixtureFunc) (ftest), \
        (HxTestFixtureFunc) (fteardown))

/* ---- TESTLIST / TESTENTRY (mirrors frida testutil.h) -------------------- */

#define TESTLIST_BEGIN(NAME) \
    void test_ ##NAME## _add_tests (hx_pointer fixture_data) \
    { \
      HX_GNUC_UNUSED const hx_char * group = "/";
#define TESTLIST_END() \
    }

#define TESTENTRY_SIMPLE(NAME, PREFIX, FUNC) \
    HX_STMT_START \
    { \
      hx_char * path; \
      extern void PREFIX## _ ##FUNC (void); \
      path = hx_strconcat ("/" NAME, group, #FUNC, NULL); \
      hx_test_add_func (path, PREFIX## _ ##FUNC); \
      hx_free (path); \
    } \
    HX_STMT_END;

#define TESTENTRY_WITH_FIXTURE(NAME, PREFIX, FUNC, STRUCT) \
    HX_STMT_START \
    { \
      hx_char * path; \
      extern void PREFIX## _ ##FUNC (STRUCT * fixture, hx_constpointer data); \
      path = hx_strconcat ("/" NAME, group, #FUNC, NULL); \
      hx_test_add (path, STRUCT, fixture_data, \
          PREFIX## _fixture_setup, PREFIX## _ ##FUNC, \
          PREFIX## _fixture_teardown); \
      hx_free (path); \
    } \
    HX_STMT_END;

#define TESTGROUP_BEGIN(NAME) group = "/" NAME "/";
#define TESTGROUP_END() group = "/";

#define TESTLIST_REGISTER(NAME) TESTLIST_REGISTER_WITH_DATA (NAME, NULL)
#define TESTLIST_REGISTER_WITH_DATA(NAME, FIXTURE_DATA) \
    HX_STMT_START \
    { \
      extern void test_ ##NAME## _add_tests (hx_pointer fixture_data); \
      test_ ##NAME## _add_tests (FIXTURE_DATA); \
    } \
    HX_STMT_END

#define HOOX_ASSERT_CMPADDR(n1, cmp, n2) \
    hx_assert_cmphex (HX_POINTER_TO_SIZE (n1), cmp, HX_POINTER_TO_SIZE (n2))

HX_END_DECLS

#endif
