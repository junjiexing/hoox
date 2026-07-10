/*
 * hoox test harness runner.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "testrunner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _HxTest HxTest;

struct _HxTest
{
  char * path;
  gsize fixture_size;
  gconstpointer fixture_data;
  HxTestFixtureFunc setup;
  HxTestFixtureFunc test;
  HxTestFixtureFunc teardown;
  HxTestSimpleFunc simple;
  HxTest * next;
};

static HxTest * hx_tests_head = NULL;
static HxTest * hx_tests_tail = NULL;
static guint hx_test_count = 0;

static char ** hx_filters = NULL;
static int hx_n_filters = 0;

static gboolean hx_current_failed = FALSE;

static HxTest *
hx_test_new (const char * path)
{
  HxTest * t = g_new0 (HxTest, 1);

  t->path = g_strdup (path);

  if (hx_tests_tail != NULL)
    hx_tests_tail->next = t;
  else
    hx_tests_head = t;
  hx_tests_tail = t;
  hx_test_count++;

  return t;
}

void
hx_test_add_vtable (const char * path,
                    gsize fixture_size,
                    gconstpointer fixture_data,
                    HxTestFixtureFunc setup,
                    HxTestFixtureFunc test,
                    HxTestFixtureFunc teardown)
{
  HxTest * t = hx_test_new (path);

  t->fixture_size = fixture_size;
  t->fixture_data = fixture_data;
  t->setup = setup;
  t->test = test;
  t->teardown = teardown;
}

void
hx_test_add_func (const char * path,
                  HxTestSimpleFunc func)
{
  HxTest * t = hx_test_new (path);
  t->simple = func;
}

void
g_test_init (int * argc,
             char *** argv,
             void * unused)
{
  int i;
  int n = (argc != NULL) ? *argc : 0;
  char ** av = (argv != NULL) ? *argv : NULL;

  (void) unused;

  if (av == NULL || n <= 1)
    return;

  hx_filters = g_new0 (char *, n);
  hx_n_filters = 0;

  for (i = 1; i != n; i++)
  {
    const char * a = av[i];

    if (strcmp (a, "-p") == 0 && i + 1 < n)
    {
      hx_filters[hx_n_filters++] = g_strdup (av[++i]);
    }
    else if (a[0] == '/')
    {
      hx_filters[hx_n_filters++] = g_strdup (a);
    }
  }
}

static gboolean
hx_test_selected (const HxTest * t)
{
  int i;

  if (hx_n_filters == 0)
    return TRUE;

  for (i = 0; i != hx_n_filters; i++)
  {
    const char * f = hx_filters[i];
    gsize len = strlen (f);

    if (strncmp (t->path, f, len) == 0 &&
        (t->path[len] == '\0' || t->path[len] == '/'))
      return TRUE;
  }

  return FALSE;
}

void
g_test_fail (void)
{
  hx_current_failed = TRUE;
}

void
g_test_message (const char * format,
                ...)
{
  va_list args;
  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
  fprintf (stderr, "\n");
}

int
g_test_run (void)
{
  HxTest * t;
  guint ran = 0;
  guint failed = 0;

  for (t = hx_tests_head; t != NULL; t = t->next)
  {
    if (!hx_test_selected (t))
      continue;

    ran++;
    hx_current_failed = FALSE;
    fprintf (stderr, "  %s\n", t->path);

    if (t->simple != NULL)
    {
      t->simple ();
    }
    else
    {
      gpointer fixture = (t->fixture_size != 0)
          ? g_malloc0 (t->fixture_size)
          : NULL;

      if (t->setup != NULL)
        t->setup (fixture, t->fixture_data);

      t->test (fixture, t->fixture_data);

      if (t->teardown != NULL)
        t->teardown (fixture, t->fixture_data);

      g_free (fixture);
    }

    if (hx_current_failed)
      failed++;
  }

  fprintf (stderr, "\n%u/%u tests passed\n", ran - failed, ran);

  return (failed == 0) ? 0 : 1;
}
