/*
 * Copyright (C) 2010-2017 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_ARM_RELOCATOR_H__
#define __HOOX_ARM_RELOCATOR_H__

#include "hooxarmwriter.h"

#include <hx_disasm.h>

HX_BEGIN_DECLS

typedef struct _HooxArmRelocator HooxArmRelocator;

struct _HooxArmRelocator
{
  volatile hx_int ref_count;

  hx_csh capstone;

  const hx_uint8 * input_start;
  const hx_uint8 * input_cur;
  HooxAddress input_pc;
  hx_insn ** input_insns;
  HooxArmWriter * output;

  hx_uint inpos;
  hx_uint outpos;

  hx_boolean eob;
  hx_boolean eoi;
};

HOOX_API HooxArmRelocator * hoox_arm_relocator_new (hx_constpointer input_code,
    HooxArmWriter * output);
HOOX_API HooxArmRelocator * hoox_arm_relocator_ref (HooxArmRelocator * relocator);
HOOX_API void hoox_arm_relocator_unref (HooxArmRelocator * relocator);

HOOX_API void hoox_arm_relocator_init (HooxArmRelocator * relocator,
    hx_constpointer input_code, HooxArmWriter * output);
HOOX_API void hoox_arm_relocator_clear (HooxArmRelocator * relocator);

HOOX_API void hoox_arm_relocator_reset (HooxArmRelocator * relocator,
    hx_constpointer input_code, HooxArmWriter * output);

HOOX_API hx_uint hoox_arm_relocator_read_one (HooxArmRelocator * self,
    const hx_insn ** instruction);

HOOX_API hx_insn * hoox_arm_relocator_peek_next_write_insn (
    HooxArmRelocator * self);
HOOX_API hx_pointer hoox_arm_relocator_peek_next_write_source (
    HooxArmRelocator * self);
HOOX_API void hoox_arm_relocator_skip_one (HooxArmRelocator * self);
HOOX_API hx_boolean hoox_arm_relocator_write_one (HooxArmRelocator * self);
HOOX_API void hoox_arm_relocator_write_all (HooxArmRelocator * self);

HOOX_API hx_boolean hoox_arm_relocator_eob (HooxArmRelocator * self);
HOOX_API hx_boolean hoox_arm_relocator_eoi (HooxArmRelocator * self);

HOOX_API hx_boolean hoox_arm_relocator_can_relocate (hx_pointer address,
    hx_uint min_bytes, hx_uint * maximum);
HOOX_API hx_uint hoox_arm_relocator_relocate (hx_pointer from, hx_uint min_bytes,
    hx_pointer to);

HX_END_DECLS

#endif
