/*
 * Copyright (C) 2008-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __GUM_INVOCATION_LISTENER_H__
#define __GUM_INVOCATION_LISTENER_H__

#include "gumdefs.h"
#include "guminvocationcontext.h"

G_BEGIN_DECLS

typedef struct _GumInvocationListener GumInvocationListener;
typedef struct _GumInvocationListenerInterface GumInvocationListenerInterface;

typedef void (* GumInvocationCallback) (GumInvocationContext * context,
    gpointer user_data);

struct _GumInvocationListenerInterface
{
  void (* on_enter) (GumInvocationListener * self,
      GumInvocationContext * context);
  void (* on_leave) (GumInvocationListener * self,
      GumInvocationContext * context);
};

/*
 * Plain-C replacement for the former GObject interface: every listener
 * instance begins with this header (vtable + refcount). Implementers embed a
 * GumInvocationListener as their first member and call
 * gum_invocation_listener_init.
 */
struct _GumInvocationListener
{
  const GumInvocationListenerInterface * iface;
  gint ref_count;
  GDestroyNotify finalize;   /* per-instance cleanup; may be NULL */
};

GUM_API void gum_invocation_listener_init (GumInvocationListener * self,
    const GumInvocationListenerInterface * iface, GDestroyNotify finalize);

GUM_API GumInvocationListener * gum_make_call_listener (
    GumInvocationCallback on_enter, GumInvocationCallback on_leave,
    gpointer data, GDestroyNotify data_destroy);
GUM_API GumInvocationListener * gum_make_probe_listener (
    GumInvocationCallback on_hit, gpointer data, GDestroyNotify data_destroy);

GUM_API GumInvocationListener * gum_invocation_listener_ref (
    GumInvocationListener * self);
GUM_API void gum_invocation_listener_unref (GumInvocationListener * self);

GUM_API void gum_invocation_listener_on_enter (GumInvocationListener * self,
    GumInvocationContext * context);
GUM_API void gum_invocation_listener_on_leave (GumInvocationListener * self,
    GumInvocationContext * context);

G_END_DECLS

#endif
