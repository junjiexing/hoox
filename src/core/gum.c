/*
 * hoox — library init/deinit (minimal replacement for frida-gum gum.c).
 *
 * Brings up exactly what the inline-hook engine needs: the internal heap /
 * memory backend, TLS, and the interceptor's global state. No GLib/GObject
 * type-system bootstrap, no capstone allocator hooks, no JS/Stalker bits.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 * Copyright (C) Ole André Vadla Ravnås and contributors.
 */

#include "gum.h"

#include "guminterceptor-priv.h"

#include "hxthread.h"

/* From gummemory.c */
extern void gum_internal_heap_ref (void);
extern void gum_internal_heap_unref (void);
/* From backend gumtls-<os>.c */
G_GNUC_INTERNAL void _gum_tls_init (void);
G_GNUC_INTERNAL void _gum_tls_deinit (void);
G_GNUC_INTERNAL void _gum_tls_realize (void);

static gint gum_initialized = 0;

void
gum_init (void)
{
  if (g_atomic_int_add (&gum_initialized, 1) != 0)
    return;

  gum_internal_heap_ref ();
  _gum_tls_init ();
  _gum_interceptor_init ();
  _gum_tls_realize ();
}

void
gum_deinit (void)
{
  if (g_atomic_int_add (&gum_initialized, -1) != 1)
    return;

  _gum_interceptor_deinit ();
  _gum_tls_deinit ();
  gum_internal_heap_unref ();
}

void
gum_init_embedded (void)
{
  gum_init ();
}

void
gum_deinit_embedded (void)
{
  gum_deinit ();
}

void
gum_shutdown (void)
{
}

void
gum_prepare_to_fork (void)
{
}

void
gum_recover_from_fork_in_parent (void)
{
}

void
gum_recover_from_fork_in_child (void)
{
}
