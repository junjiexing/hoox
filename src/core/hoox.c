/*
 * hoox — library init/deinit (minimal replacement for frida-gum hoox.c).
 *
 * Brings up exactly what the inline-hook engine needs: the internal heap /
 * memory backend, TLS, and the interceptor's global state. No GLib/GObject
 * type-system bootstrap, no capstone allocator hooks, no JS/Stalker bits.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 * Copyright (C) Ole André Vadla Ravnås and contributors.
 */

#include "hoox.h"

#include "hooxinterceptor-priv.h"

#include "hxthread.h"

/* From hooxmemory.c */
extern void hoox_internal_heap_ref (void);
extern void hoox_internal_heap_unref (void);
/* From backend hooxtls-<os>.c */
HX_GNUC_INTERNAL void _hoox_tls_init (void);
HX_GNUC_INTERNAL void _hoox_tls_deinit (void);
HX_GNUC_INTERNAL void _hoox_tls_realize (void);

static hx_int hoox_initialized = 0;

void
hoox_init (void)
{
  if (hx_atomic_int_add (&hoox_initialized, 1) != 0)
    return;

  hoox_internal_heap_ref ();
  _hoox_tls_init ();
  _hoox_interceptor_init ();
  _hoox_tls_realize ();
}

void
hoox_deinit (void)
{
  if (hx_atomic_int_add (&hoox_initialized, -1) != 1)
    return;

  _hoox_interceptor_deinit ();
  _hoox_tls_deinit ();
  hoox_internal_heap_unref ();
}

void
hoox_init_embedded (void)
{
  hoox_init ();
}

void
hoox_deinit_embedded (void)
{
  hoox_deinit ();
}

void
hoox_shutdown (void)
{
}

void
hoox_prepare_to_fork (void)
{
}

void
hoox_recover_from_fork_in_parent (void)
{
}

void
hoox_recover_from_fork_in_child (void)
{
}
