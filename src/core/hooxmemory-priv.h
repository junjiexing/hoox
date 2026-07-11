/*
 * Copyright (C) 2010-2021 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_MEMORY_PRIV_H__
#define __HOOX_MEMORY_PRIV_H__

#include "hooxmemory.h"


HX_BEGIN_DECLS

HX_GNUC_INTERNAL void _hoox_memory_backend_init (void);
HX_GNUC_INTERNAL void _hoox_memory_backend_deinit (void);
HX_GNUC_INTERNAL hx_uint _hoox_memory_backend_query_page_size (void);
HX_GNUC_INTERNAL hx_int _hoox_page_protection_to_posix (HooxPageProtection prot);
#ifdef HAVE_LINUX
HX_GNUC_INTERNAL void _hoox_memory_query_protections (HxPtrArray * sorted_pages,
    HooxPageProtection * protections);
#endif

HX_GNUC_INTERNAL hx_pointer hoox_internal_malloc (size_t size);
HX_GNUC_INTERNAL hx_pointer hoox_internal_calloc (size_t count, size_t size);
HX_GNUC_INTERNAL hx_pointer hoox_internal_realloc (hx_pointer mem, size_t size);
HX_GNUC_INTERNAL void hoox_internal_free (hx_pointer mem);

HX_END_DECLS

#endif
