/*
 * Copyright (C) 2008-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxinvocationlistener.h"

#include "hxmem.h"

typedef struct _HooxCallListener HooxCallListener;
typedef struct _HooxProbeListener HooxProbeListener;

struct _HooxCallListener
{
  HooxInvocationListener base;

  HooxInvocationCallback on_enter;
  HooxInvocationCallback on_leave;

  hx_pointer data;
  HxDestroyNotify data_destroy;
};

struct _HooxProbeListener
{
  HooxInvocationListener base;

  HooxInvocationCallback on_hit;

  hx_pointer data;
  HxDestroyNotify data_destroy;
};

void
hoox_invocation_listener_init (HooxInvocationListener * self,
                              const HooxInvocationListenerInterface * iface,
                              HxDestroyNotify finalize)
{
  self->iface = iface;
  self->ref_count = 1;
  self->finalize = finalize;
}

HooxInvocationListener *
hoox_invocation_listener_ref (HooxInvocationListener * self)
{
  if (self != NULL)
    hx_atomic_int_inc (&self->ref_count);
  return self;
}

void
hoox_invocation_listener_unref (HooxInvocationListener * self)
{
  if (self == NULL)
    return;

  if (hx_atomic_int_dec_and_test (&self->ref_count))
  {
    if (self->finalize != NULL)
      self->finalize (self);
    hx_free (self);
  }
}

void
hoox_invocation_listener_on_enter (HooxInvocationListener * self,
                                  HooxInvocationContext * context)
{
  if (self->iface->on_enter != NULL)
    self->iface->on_enter (self, context);
}

void
hoox_invocation_listener_on_leave (HooxInvocationListener * self,
                                  HooxInvocationContext * context)
{
  if (self->iface->on_leave != NULL)
    self->iface->on_leave (self, context);
}

/* ---- built-in call listener --------------------------------------------- */

static void
hoox_call_listener_on_enter (HooxInvocationListener * listener,
                            HooxInvocationContext * context)
{
  HooxCallListener * self = (HooxCallListener *) listener;

  if (self->on_enter != NULL)
    self->on_enter (context, self->data);
}

static void
hoox_call_listener_on_leave (HooxInvocationListener * listener,
                            HooxInvocationContext * context)
{
  HooxCallListener * self = (HooxCallListener *) listener;

  if (self->on_leave != NULL)
    self->on_leave (context, self->data);
}

static void
hoox_call_listener_finalize (hx_pointer instance)
{
  HooxCallListener * self = instance;

  if (self->data_destroy != NULL)
    self->data_destroy (self->data);
}

static const HooxInvocationListenerInterface hoox_call_listener_iface =
{
  hoox_call_listener_on_enter,
  hoox_call_listener_on_leave
};

HooxInvocationListener *
hoox_make_call_listener (HooxInvocationCallback on_enter,
                        HooxInvocationCallback on_leave,
                        hx_pointer data,
                        HxDestroyNotify data_destroy)
{
  HooxCallListener * listener = hx_new0 (HooxCallListener, 1);

  hoox_invocation_listener_init (&listener->base, &hoox_call_listener_iface,
      hoox_call_listener_finalize);
  listener->on_enter = on_enter;
  listener->on_leave = on_leave;
  listener->data = data;
  listener->data_destroy = data_destroy;

  return &listener->base;
}

/* ---- built-in probe listener -------------------------------------------- */

static void
hoox_probe_listener_on_enter (HooxInvocationListener * listener,
                             HooxInvocationContext * context)
{
  HooxProbeListener * self = (HooxProbeListener *) listener;

  self->on_hit (context, self->data);
}

static void
hoox_probe_listener_finalize (hx_pointer instance)
{
  HooxProbeListener * self = instance;

  if (self->data_destroy != NULL)
    self->data_destroy (self->data);
}

static const HooxInvocationListenerInterface hoox_probe_listener_iface =
{
  hoox_probe_listener_on_enter,
  NULL
};

HooxInvocationListener *
hoox_make_probe_listener (HooxInvocationCallback on_hit,
                         hx_pointer data,
                         HxDestroyNotify data_destroy)
{
  HooxProbeListener * listener = hx_new0 (HooxProbeListener, 1);

  hoox_invocation_listener_init (&listener->base, &hoox_probe_listener_iface,
      hoox_probe_listener_finalize);
  listener->on_hit = on_hit;
  listener->data = data;
  listener->data_destroy = data_destroy;

  return &listener->base;
}
