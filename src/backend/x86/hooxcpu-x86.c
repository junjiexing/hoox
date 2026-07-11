/*
 * hoox — x86 CPU feature query (extracted from frida-gum hoox.c).
 *
 * Licence: wxWindows Library Licence, Version 3.1
 * Copyright (C) Ole André Vadla Ravnås and contributors.
 */

#include "hooxdefs.h"

#ifdef _MSC_VER
# include <intrin.h>
#else
# include <cpuid.h>
#endif

static HooxCpuFeatures hoox_do_query_cpu_features (void);
static hx_boolean hoox_get_cpuid (hx_uint level, hx_uint * a, hx_uint * b, hx_uint * c,
    hx_uint * d);

HooxCpuFeatures
hoox_query_cpu_features (void)
{
  static hx_size cached_result = 0;

  if (hx_once_init_enter (&cached_result))
  {
    HooxCpuFeatures features = hoox_do_query_cpu_features ();

    hx_once_init_leave (&cached_result, features + 1);
  }

  return cached_result - 1;
}

static HooxCpuFeatures
hoox_do_query_cpu_features (void)
{
  HooxCpuFeatures features = 0;
  hx_boolean cpu_supports_avx2 = FALSE;
  hx_boolean cpu_supports_cet_ss = FALSE;
  hx_boolean os_enabled_xsave = FALSE;
  hx_uint a, b, c, d;

  if (hoox_get_cpuid (7, &a, &b, &c, &d))
  {
    cpu_supports_avx2 = (b & (1 << 5)) != 0;
    cpu_supports_cet_ss = (c & (1 << 7)) != 0;
  }

  if (hoox_get_cpuid (1, &a, &b, &c, &d))
    os_enabled_xsave = (c & (1 << 27)) != 0;

  if (cpu_supports_avx2 && os_enabled_xsave)
    features |= HOOX_CPU_AVX2;

  if (cpu_supports_cet_ss)
    features |= HOOX_CPU_CET_SS;

  return features;
}

static hx_boolean
hoox_get_cpuid (hx_uint level,
               hx_uint * a,
               hx_uint * b,
               hx_uint * c,
               hx_uint * d)
{
#ifdef _MSC_VER
  hx_int info[4];
  hx_uint n;

  __cpuid (info, 0);
  n = info[0];
  if (n < level)
    return FALSE;

  __cpuid (info, level);

  *a = info[0];
  *b = info[1];
  *c = info[2];
  *d = info[3];

  return TRUE;
#else
  hx_uint n;

  n = __get_cpuid_max (0, NULL);
  if (n < level)
    return FALSE;

  __cpuid_count (level, 0, *a, *b, *c, *d);

  return TRUE;
#endif
}

