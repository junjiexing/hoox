/*
 * hoox — cloak: no-op stub.
 *
 * frida-gum's cloak hides Frida's own threads / memory ranges / fds from
 * enumeration. A standalone inline-hook library does not need this, so the
 * range API (all that gumcodeallocator uses) is a no-op here.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __GUM_CLOAK_H__
#define __GUM_CLOAK_H__

#include "gummemory.h"

G_BEGIN_DECLS

GUM_API void gum_cloak_add_range (const GumMemoryRange * range);
GUM_API void gum_cloak_remove_range (const GumMemoryRange * range);

G_END_DECLS

#endif
