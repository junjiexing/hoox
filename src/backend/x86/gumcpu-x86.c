/*
 * hoox — x86 CPU feature query (extracted from frida-gum gum.c).
 *
 * Licence: wxWindows Library Licence, Version 3.1
 * Copyright (C) Ole André Vadla Ravnås and contributors.
 */

#include "gumdefs.h"

#ifdef _MSC_VER
# include <intrin.h>
#else
# include <cpuid.h>
#endif

static GumCpuFeatures gum_do_query_cpu_features (void);
static gboolean gum_get_cpuid (guint level, guint * a, guint * b, guint * c,
    guint * d);

GumCpuFeatures
gum_query_cpu_features (void)
{
  static gsize cached_result = 0;

  if (g_once_init_enter (&cached_result))
  {
    GumCpuFeatures features = gum_do_query_cpu_features ();

    g_once_init_leave (&cached_result, features + 1);
  }

  return cached_result - 1;
}

static GumCpuFeatures
gum_do_query_cpu_features (void)
{
  GumCpuFeatures features = 0;
  gboolean cpu_supports_avx2 = FALSE;
  gboolean cpu_supports_cet_ss = FALSE;
  gboolean os_enabled_xsave = FALSE;
  guint a, b, c, d;

  if (gum_get_cpuid (7, &a, &b, &c, &d))
  {
    cpu_supports_avx2 = (b & (1 << 5)) != 0;
    cpu_supports_cet_ss = (c & (1 << 7)) != 0;
  }

  if (gum_get_cpuid (1, &a, &b, &c, &d))
    os_enabled_xsave = (c & (1 << 27)) != 0;

  if (cpu_supports_avx2 && os_enabled_xsave)
    features |= GUM_CPU_AVX2;

  if (cpu_supports_cet_ss)
    features |= GUM_CPU_CET_SS;

  return features;
}

static gboolean
gum_get_cpuid (guint level,
               guint * a,
               guint * b,
               guint * c,
               guint * d)
{
#ifdef _MSC_VER
  gint info[4];
  guint n;

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
  guint n;

  n = __get_cpuid_max (0, NULL);
  if (n < level)
    return FALSE;

  __cpuid_count (level, 0, *a, *b, *c, *d);

  return TRUE;
#endif
}

