/*
 * Copyright (C) 2010-2025 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_WINDOWS_H__
#define __HOOX_WINDOWS_H__

#include "hooxmemory.h"

#include "hxglib.h"
#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

HX_BEGIN_DECLS

HOOX_API HooxCpuType hoox_windows_query_native_cpu_type (void);
HOOX_API HooxCpuType hoox_windows_cpu_type_from_pid (hx_uint pid, HxError ** error);

HOOX_API hx_char * hoox_windows_query_thread_name (HANDLE thread);
HOOX_API HooxAddress hoox_windows_query_thread_entrypoint_routine (HANDLE thread);
HOOX_API void hoox_windows_parse_context (const CONTEXT * context,
    HooxCpuContext * cpu_context);
HOOX_API void hoox_windows_unparse_context (const HooxCpuContext * cpu_context,
    CONTEXT * context);
HOOX_API CONTEXT * hoox_windows_get_active_exceptor_context (void);

HOOX_API HooxPageProtection hoox_page_protection_from_windows (DWORD native_prot);
HOOX_API DWORD hoox_page_protection_to_windows (HooxPageProtection prot);

HX_END_DECLS

#endif
