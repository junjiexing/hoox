/*
 * Copyright (C) 2014-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_ARM64_RELOCATOR_H__
#define __HOOX_ARM64_RELOCATOR_H__

#include "hooxarm64writer.h"

#include <hx_disasm.h>

HX_BEGIN_DECLS

typedef struct _HooxArm64Relocator HooxArm64Relocator;

struct _HooxArm64Relocator
{
  volatile hx_int ref_count;

  hx_csh capstone;

  const hx_uint8 * input_start;
  const hx_uint8 * input_cur;
  HooxAddress input_pc;
  hx_insn ** input_insns;
  HooxArm64Writer * output;

  hx_uint inpos;
  hx_uint outpos;

  hx_boolean eob;
  hx_boolean eoi;
};

HOOX_API HooxArm64Relocator * hoox_arm64_relocator_new (hx_constpointer input_code,
    HooxArm64Writer * output);
HOOX_API HooxArm64Relocator * hoox_arm64_relocator_ref (
    HooxArm64Relocator * relocator);
HOOX_API void hoox_arm64_relocator_unref (HooxArm64Relocator * relocator);

HOOX_API void hoox_arm64_relocator_init (HooxArm64Relocator * relocator,
    hx_constpointer input_code, HooxArm64Writer * output);
HOOX_API void hoox_arm64_relocator_clear (HooxArm64Relocator * relocator);

HOOX_API void hoox_arm64_relocator_reset (HooxArm64Relocator * relocator,
    hx_constpointer input_code, HooxArm64Writer * output);

HOOX_API hx_uint hoox_arm64_relocator_read_one (HooxArm64Relocator * self,
    const hx_insn ** instruction);

HOOX_API hx_insn * hoox_arm64_relocator_peek_next_write_insn (
    HooxArm64Relocator * self);
HOOX_API hx_pointer hoox_arm64_relocator_peek_next_write_source (
    HooxArm64Relocator * self);
HOOX_API void hoox_arm64_relocator_skip_one (HooxArm64Relocator * self);
HOOX_API hx_boolean hoox_arm64_relocator_write_one (HooxArm64Relocator * self);
HOOX_API void hoox_arm64_relocator_write_all (HooxArm64Relocator * self);

HOOX_API hx_boolean hoox_arm64_relocator_eob (HooxArm64Relocator * self);
HOOX_API hx_boolean hoox_arm64_relocator_eoi (HooxArm64Relocator * self);

HOOX_API hx_boolean hoox_arm64_relocator_can_relocate (hx_pointer address,
    hx_uint min_bytes, HooxRelocationScenario scenario, HooxRelocationPolicy policy,
    hx_uint * maximum, hx_arm64_reg * available_scratch_reg);
HOOX_API hx_uint hoox_arm64_relocator_relocate (hx_pointer from, hx_uint min_bytes,
    hx_pointer to);

HX_END_DECLS

#endif
