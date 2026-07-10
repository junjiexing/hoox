/*
 * Copyright (C) 2008-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "guminvocationlistener.h"

#include "hxmem.h"

typedef struct _GumCallListener GumCallListener;
typedef struct _GumProbeListener GumProbeListener;

struct _GumCallListener
{
  GumInvocationListener base;

  GumInvocationCallback on_enter;
  GumInvocationCallback on_leave;

  gpointer data;
  GDestroyNotify data_destroy;
};

struct _GumProbeListener
{
  GumInvocationListener base;

  GumInvocationCallback on_hit;

  gpointer data;
  GDestroyNotify data_destroy;
};

void
gum_invocation_listener_init (GumInvocationListener * self,
                              const GumInvocationListenerInterface * iface,
                              GDestroyNotify finalize)
{
  self->iface = iface;
  self->ref_count = 1;
  self->finalize = finalize;
}

GumInvocationListener *
gum_invocation_listener_ref (GumInvocationListener * self)
{
  if (self != NULL)
    g_atomic_int_inc (&self->ref_count);
  return self;
}

void
gum_invocation_listener_unref (GumInvocationListener * self)
{
  if (self == NULL)
    return;

  if (g_atomic_int_dec_and_test (&self->ref_count))
  {
    if (self->finalize != NULL)
      self->finalize (self);
    g_free (self);
  }
}

void
gum_invocation_listener_on_enter (GumInvocationListener * self,
                                  GumInvocationContext * context)
{
  if (self->iface->on_enter != NULL)
    self->iface->on_enter (self, context);
}

void
gum_invocation_listener_on_leave (GumInvocationListener * self,
                                  GumInvocationContext * context)
{
  if (self->iface->on_leave != NULL)
    self->iface->on_leave (self, context);
}

/* ---- built-in call listener --------------------------------------------- */

static void
gum_call_listener_on_enter (GumInvocationListener * listener,
                            GumInvocationContext * context)
{
  GumCallListener * self = (GumCallListener *) listener;

  if (self->on_enter != NULL)
    self->on_enter (context, self->data);
}

static void
gum_call_listener_on_leave (GumInvocationListener * listener,
                            GumInvocationContext * context)
{
  GumCallListener * self = (GumCallListener *) listener;

  if (self->on_leave != NULL)
    self->on_leave (context, self->data);
}

static void
gum_call_listener_finalize (gpointer instance)
{
  GumCallListener * self = instance;

  if (self->data_destroy != NULL)
    self->data_destroy (self->data);
}

static const GumInvocationListenerInterface gum_call_listener_iface =
{
  gum_call_listener_on_enter,
  gum_call_listener_on_leave
};

GumInvocationListener *
gum_make_call_listener (GumInvocationCallback on_enter,
                        GumInvocationCallback on_leave,
                        gpointer data,
                        GDestroyNotify data_destroy)
{
  GumCallListener * listener = g_new0 (GumCallListener, 1);

  gum_invocation_listener_init (&listener->base, &gum_call_listener_iface,
      gum_call_listener_finalize);
  listener->on_enter = on_enter;
  listener->on_leave = on_leave;
  listener->data = data;
  listener->data_destroy = data_destroy;

  return &listener->base;
}

/* ---- built-in probe listener -------------------------------------------- */

static void
gum_probe_listener_on_enter (GumInvocationListener * listener,
                             GumInvocationContext * context)
{
  GumProbeListener * self = (GumProbeListener *) listener;

  self->on_hit (context, self->data);
}

static void
gum_probe_listener_finalize (gpointer instance)
{
  GumProbeListener * self = instance;

  if (self->data_destroy != NULL)
    self->data_destroy (self->data);
}

static const GumInvocationListenerInterface gum_probe_listener_iface =
{
  gum_probe_listener_on_enter,
  NULL
};

GumInvocationListener *
gum_make_probe_listener (GumInvocationCallback on_hit,
                         gpointer data,
                         GDestroyNotify data_destroy)
{
  GumProbeListener * listener = g_new0 (GumProbeListener, 1);

  gum_invocation_listener_init (&listener->base, &gum_probe_listener_iface,
      gum_probe_listener_finalize);
  listener->on_hit = on_hit;
  listener->data = data;
  listener->data_destroy = data_destroy;

  return &listener->base;
}
