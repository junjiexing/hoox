/*
 * Runner for the arch-x86 code-writer / relocator test suites.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "testrunner.h"

extern void gum_internal_heap_ref (void);
extern void gum_internal_heap_unref (void);

int
main (int argc, char ** argv)
{
  int result;

  gum_internal_heap_ref ();

  g_test_init (&argc, &argv, NULL);

  TESTLIST_REGISTER (x86writer);
  TESTLIST_REGISTER (x86relocator);

  result = g_test_run ();

  gum_internal_heap_unref ();

  return result;
}
