/*
 * hoox — ARM CPU feature query.
 *
 * The ARM writer/relocator/interceptor ask which VFP/interworking features are
 * available so the generated trampolines only use instructions the CPU has. On
 * Linux this is read from the ELF auxiliary vector (AT_HWCAP); ARMv7 always
 * supports Thumb interworking (BX), so that bit is set unconditionally.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxdefs.h"

#if defined (HAVE_LINUX) || defined (__linux__)
# include <sys/auxv.h>
# ifndef AT_HWCAP
#  define AT_HWCAP 16
# endif
# ifndef HWCAP_VFP
#  define HWCAP_VFP (1 << 6)
# endif
# ifndef HWCAP_VFPv3
#  define HWCAP_VFPv3 (1 << 13)
# endif
# ifndef HWCAP_VFPD32
#  define HWCAP_VFPD32 (1 << 19)
# endif
#endif

static HooxCpuFeatures hoox_do_query_cpu_features (void);

HooxCpuFeatures
hoox_query_cpu_features (void)
{
  static hx_size cached_result = 0;

  if (hx_once_init_enter (&cached_result))
  {
    HooxCpuFeatures features = hoox_do_query_cpu_features ();

    hx_once_init_leave (&cached_result, features + 1);
  }

  return (HooxCpuFeatures) (cached_result - 1);
}

static HooxCpuFeatures
hoox_do_query_cpu_features (void)
{
  HooxCpuFeatures features = HOOX_CPU_THUMB_INTERWORK;

#if defined (HAVE_LINUX) || defined (__linux__)
  unsigned long hwcap = getauxval (AT_HWCAP);

  if ((hwcap & HWCAP_VFP) != 0)
    features |= HOOX_CPU_VFP2;
  if ((hwcap & HWCAP_VFPv3) != 0)
    features |= HOOX_CPU_VFP3;
  if ((hwcap & HWCAP_VFPD32) != 0)
    features |= HOOX_CPU_VFPD32;
#else
  /* Conservative default for non-Linux ARM: assume a VFPv3-D32 core. */
  features |= HOOX_CPU_VFP2 | HOOX_CPU_VFP3 | HOOX_CPU_VFPD32;
#endif

  return features;
}
