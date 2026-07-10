/*
 * hoox nano-glib: umbrella header.
 *
 * Extracted frida-gum sources include this in place of <glib.h>. It pulls in
 * the full GLib-compatible surface hoox reimplements (types, macros, memory,
 * atomics, threading, containers, strings) — with no external dependency.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_COMPAT_GLIB_H__
#define __HOOX_COMPAT_GLIB_H__

#include "hxdefs.h"
#include "hxmessages.h"
#include "hxmem.h"
#include "hxatomic.h"
#include "hxthread.h"
#include "hxarray.h"
#include "hxhash.h"
#include "hxlist.h"
#include "hxstring.h"
#include "hxstrfuncs.h"

#endif
