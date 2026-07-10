/*
 * hoox — cloak: no-op stub implementation. See gumcloak.h.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gumcloak-priv.h"

void
_gum_cloak_init (void)
{
}

void
_gum_cloak_deinit (void)
{
}

void
gum_cloak_add_range (const GumMemoryRange * range)
{
  (void) range;
}

void
gum_cloak_remove_range (const GumMemoryRange * range)
{
  (void) range;
}
