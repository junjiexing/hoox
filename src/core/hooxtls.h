/*
 * Copyright (C) 2010-2017 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_TLS_H__
#define __HOOX_TLS_H__

#include "hooxdefs.h"

HX_BEGIN_DECLS

typedef hx_size HooxTlsKey;

HOOX_API HooxTlsKey hoox_tls_key_new (void);
HOOX_API void hoox_tls_key_free (HooxTlsKey key);

HOOX_API hx_pointer hoox_tls_key_get_value (HooxTlsKey key);
HOOX_API void hoox_tls_key_set_value (HooxTlsKey key, hx_pointer value);

HX_END_DECLS

#endif
