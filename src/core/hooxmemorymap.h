/*
 * Copyright (C) 2013-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_MEMORY_MAP_H__
#define __HOOX_MEMORY_MAP_H__

#include <hoox/hooxmemory.h>

HX_BEGIN_DECLS

#define HOOX_TYPE_MEMORY_MAP (hoox_memory_map_get_type ())
HX_DECLARE_FINAL_TYPE (HooxMemoryMap, hoox_memory_map, HOOX, MEMORY_MAP, GObject)

HOOX_API HooxMemoryMap * hoox_memory_map_new (HooxPageProtection prot);

HOOX_API hx_boolean hoox_memory_map_contains (HooxMemoryMap * self,
    const HooxMemoryRange * range);

HOOX_API void hoox_memory_map_update (HooxMemoryMap * self);

HX_END_DECLS

#endif
