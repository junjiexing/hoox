/*
 * Copyright (C) 2015-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxandroid.h"

#include <stdlib.h>
#include <sys/system_properties.h>

hx_uint
hoox_android_get_api_level (void)
{
  static hx_uint cached_api_level = 0;

  if (cached_api_level == 0)
  {
    char sdk_version[PROP_VALUE_MAX];

    /*
     * A real device is always API >= 1, so 0 doubles as the "not cached yet"
     * sentinel. When the property service is unavailable (e.g. a bare bionic
     * binary run under qemu-user in CI) the read yields an empty string and we
     * report 0, which callers treat as "pre-29" — the safe, no-op path.
     */
    if (__system_property_get ("ro.build.version.sdk", sdk_version) > 0)
      cached_api_level = (hx_uint) atoi (sdk_version);
  }

  return cached_api_level;
}
