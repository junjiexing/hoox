/*
 * hoox — cloak: no-op stub.
 *
 * frida-gum's cloak hides Frida's own threads / memory ranges / fds from
 * enumeration. A standalone inline-hook library does not need this, so the
 * range API (all that hooxcodeallocator uses) is a no-op here.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_CLOAK_H__
#define __HOOX_CLOAK_H__

#include "hooxmemory.h"

HX_BEGIN_DECLS

HOOX_API void hoox_cloak_add_range (const HooxMemoryRange * range);
HOOX_API void hoox_cloak_remove_range (const HooxMemoryRange * range);

HX_END_DECLS

#endif
