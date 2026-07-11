/*
 * hoox — minimal HooxModule shim.
 *
 * frida-gum's HooxModule is a GObject interface exposing rich module
 * introspection. hoox needs only a tiny module/range abstraction: hooxprocess.h
 * references the HooxModule type, and the tests query the main module's range.
 * The full introspection API (imports/exports/symbols/sections) is dropped.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_MODULE_H__
#define __HOOX_MODULE_H__

#include "hooxdefs.h"
#include "hooxmemory.h"

HX_BEGIN_DECLS

typedef struct _HooxModule HooxModule;

HOOX_API const HooxMemoryRange * hoox_module_get_range (HooxModule * self);

HX_END_DECLS

#endif
