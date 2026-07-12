/*
 * Copyright (C) 2015-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_ANDROID_H__
#define __HOOX_ANDROID_H__

#include "hooxdefs.h"

HX_BEGIN_DECLS

/*
 * hoox needs only the API level from Android's runtime: on API 29+ executable
 * code pages may be mapped execute-only, so the engine must add READ before
 * the decoder reads a target's prologue (see hoox_ensure_code_readable). The
 * full frida gumandroid linker/module machinery is intentionally not ported.
 */
hx_uint hoox_android_get_api_level (void);

HX_END_DECLS

#endif
