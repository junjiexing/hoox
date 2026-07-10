/*
 * hoox — minimal GumModule shim.
 *
 * frida-gum's GumModule is a GObject interface exposing rich module
 * introspection. hoox needs only a tiny module/range abstraction: gumprocess.h
 * references the GumModule type, and the tests query the main module's range.
 * The full introspection API (imports/exports/symbols/sections) is dropped.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __GUM_MODULE_H__
#define __GUM_MODULE_H__

#include "gumdefs.h"
#include "gummemory.h"

G_BEGIN_DECLS

typedef struct _GumModule GumModule;

GUM_API const gchar * gum_module_get_name (GumModule * self);
GUM_API const gchar * gum_module_get_path (GumModule * self);
GUM_API const GumMemoryRange * gum_module_get_range (GumModule * self);
GUM_API GumAddress gum_module_find_export_by_name (GumModule * self,
    const gchar * symbol_name);

G_END_DECLS

#endif
