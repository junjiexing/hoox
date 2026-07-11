/*
 * Copyright (C) 2008-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_INVOCATION_LISTENER_H__
#define __HOOX_INVOCATION_LISTENER_H__

#include "hooxdefs.h"
#include "hooxinvocationcontext.h"

HX_BEGIN_DECLS

typedef struct _HooxInvocationListener HooxInvocationListener;
typedef struct _HooxInvocationListenerInterface HooxInvocationListenerInterface;

typedef void (* HooxInvocationCallback) (HooxInvocationContext * context,
    hx_pointer user_data);

struct _HooxInvocationListenerInterface
{
  void (* on_enter) (HooxInvocationListener * self,
      HooxInvocationContext * context);
  void (* on_leave) (HooxInvocationListener * self,
      HooxInvocationContext * context);
};

/*
 * Plain-C replacement for the former GObject interface: every listener
 * instance begins with this header (vtable + refcount). Implementers embed a
 * HooxInvocationListener as their first member and call
 * hoox_invocation_listener_init.
 */
struct _HooxInvocationListener
{
  const HooxInvocationListenerInterface * iface;
  hx_int ref_count;
  HxDestroyNotify finalize;   /* per-instance cleanup; may be NULL */
};

/* Compatibility cast (was a GObject type-check macro in frida). */
#define HOOX_INVOCATION_LISTENER(obj) ((HooxInvocationListener *) (obj))

HOOX_API void hoox_invocation_listener_init (HooxInvocationListener * self,
    const HooxInvocationListenerInterface * iface, HxDestroyNotify finalize);

HOOX_API HooxInvocationListener * hoox_make_call_listener (
    HooxInvocationCallback on_enter, HooxInvocationCallback on_leave,
    hx_pointer data, HxDestroyNotify data_destroy);
HOOX_API HooxInvocationListener * hoox_make_probe_listener (
    HooxInvocationCallback on_hit, hx_pointer data, HxDestroyNotify data_destroy);

HOOX_API HooxInvocationListener * hoox_invocation_listener_ref (
    HooxInvocationListener * self);
HOOX_API void hoox_invocation_listener_unref (HooxInvocationListener * self);

HOOX_API void hoox_invocation_listener_on_enter (HooxInvocationListener * self,
    HooxInvocationContext * context);
HOOX_API void hoox_invocation_listener_on_leave (HooxInvocationListener * self,
    HooxInvocationContext * context);

HX_END_DECLS

#endif
