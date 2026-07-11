/*
 * Copyright (C) 2015-2019 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxtls.h"

#include <pthread.h>

void
_hoox_tls_init (void)
{
}

void
_hoox_tls_realize (void)
{
}

void
_hoox_tls_deinit (void)
{
}

HooxTlsKey
hoox_tls_key_new (void)
{
  pthread_key_t key;

  pthread_key_create (&key, NULL);

  return key;
}

void
hoox_tls_key_free (HooxTlsKey key)
{
  pthread_key_delete ((pthread_key_t) key);
}

hx_pointer
hoox_tls_key_get_value (HooxTlsKey key)
{
  return pthread_getspecific ((pthread_key_t) key);
}

void
hoox_tls_key_set_value (HooxTlsKey key,
                       hx_pointer value)
{
  pthread_setspecific ((pthread_key_t) key, value);
}
