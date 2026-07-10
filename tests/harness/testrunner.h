/*
 * hoox test harness — a GLib-free reimplementation of the frida-gum test
 * scaffolding (TESTLIST / TESTENTRY / TESTCASE + the g_test_* subset the
 * ported tests use).
 *
 * A single test binary registers many test modules; g_test_run() iterates the
 * registry, honouring an optional path filter. Assertion failures (g_assert*)
 * abort the process (matching GTest-on-Windows), so a non-zero exit means
 * failure — which is what ctest keys on.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_TEST_RUNNER_H__
#define __HOOX_TEST_RUNNER_H__

#include "hxglib.h"

G_BEGIN_DECLS

typedef void (* HxTestFixtureFunc) (gpointer fixture, gconstpointer user_data);
typedef void (* HxTestSimpleFunc) (void);

void g_test_init (int * argc, char *** argv, void * unused);
int g_test_run (void);
void g_test_message (const char * format, ...) G_GNUC_PRINTF (1, 2);
void g_test_fail (void);

void hx_test_add_vtable (const char * path, gsize fixture_size,
    gconstpointer fixture_data, HxTestFixtureFunc setup,
    HxTestFixtureFunc test, HxTestFixtureFunc teardown);
void hx_test_add_func (const char * path, HxTestSimpleFunc func);

#define g_test_add_func(path, func) \
    hx_test_add_func ((path), (HxTestSimpleFunc) (func))

#define g_test_add(testpath, Fixture, tdata, fsetup, ftest, fteardown) \
    hx_test_add_vtable ((testpath), sizeof (Fixture), \
        (gconstpointer) (tdata), \
        (HxTestFixtureFunc) (fsetup), \
        (HxTestFixtureFunc) (ftest), \
        (HxTestFixtureFunc) (fteardown))

/* ---- TESTLIST / TESTENTRY (mirrors frida testutil.h) -------------------- */

#define TESTLIST_BEGIN(NAME) \
    void test_ ##NAME## _add_tests (gpointer fixture_data) \
    { \
      G_GNUC_UNUSED const gchar * group = "/";
#define TESTLIST_END() \
    }

#define TESTENTRY_SIMPLE(NAME, PREFIX, FUNC) \
    G_STMT_START \
    { \
      gchar * path; \
      extern void PREFIX## _ ##FUNC (void); \
      path = g_strconcat ("/" NAME, group, #FUNC, NULL); \
      g_test_add_func (path, PREFIX## _ ##FUNC); \
      g_free (path); \
    } \
    G_STMT_END;

#define TESTENTRY_WITH_FIXTURE(NAME, PREFIX, FUNC, STRUCT) \
    G_STMT_START \
    { \
      gchar * path; \
      extern void PREFIX## _ ##FUNC (STRUCT * fixture, gconstpointer data); \
      path = g_strconcat ("/" NAME, group, #FUNC, NULL); \
      g_test_add (path, STRUCT, fixture_data, \
          PREFIX## _fixture_setup, PREFIX## _ ##FUNC, \
          PREFIX## _fixture_teardown); \
      g_free (path); \
    } \
    G_STMT_END;

#define TESTGROUP_BEGIN(NAME) group = "/" NAME "/";
#define TESTGROUP_END() group = "/";

#define TESTLIST_REGISTER(NAME) TESTLIST_REGISTER_WITH_DATA (NAME, NULL)
#define TESTLIST_REGISTER_WITH_DATA(NAME, FIXTURE_DATA) \
    G_STMT_START \
    { \
      extern void test_ ##NAME## _add_tests (gpointer fixture_data); \
      test_ ##NAME## _add_tests (FIXTURE_DATA); \
    } \
    G_STMT_END

#define GUM_ASSERT_CMPADDR(n1, cmp, n2) \
    g_assert_cmphex (GPOINTER_TO_SIZE (n1), cmp, GPOINTER_TO_SIZE (n2))

G_END_DECLS

#endif
