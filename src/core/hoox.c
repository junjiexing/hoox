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

static HxMutex hoox_lifecycle_lock;
static hx_uint hoox_ref_count;

void
hoox_init (void)
{
  hx_mutex_lock (&hoox_lifecycle_lock);

  if (hoox_ref_count++ != 0)
    goto beach;

  hoox_internal_heap_ref ();
  _hoox_tls_init ();
  _hoox_interceptor_init ();
  _hoox_tls_realize ();

beach:
  hx_mutex_unlock (&hoox_lifecycle_lock);
}

void
hoox_deinit (void)
{
  hx_mutex_lock (&hoox_lifecycle_lock);

  hx_assert (hoox_ref_count != 0);
  if (--hoox_ref_count != 0)
    goto beach;

  _hoox_interceptor_deinit ();
  _hoox_tls_deinit ();
  hoox_internal_heap_unref ();

beach:
  hx_mutex_unlock (&hoox_lifecycle_lock);
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
  hx_mutex_lock (&hoox_lifecycle_lock);
  _hoox_interceptor_prepare_to_fork ();
}

void
hoox_recover_from_fork_in_parent (void)
{
  _hoox_interceptor_recover_from_fork_in_parent ();
  hx_mutex_unlock (&hoox_lifecycle_lock);
}

void
hoox_recover_from_fork_in_child (void)
{
  _hoox_interceptor_recover_from_fork_in_child ();
  hx_mutex_unlock (&hoox_lifecycle_lock);
}
