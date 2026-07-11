/*
 * Copyright (C) 2009-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_HX_RELOCATOR_H__
#define __HOOX_HX_RELOCATOR_H__

#include "hooxx86writer.h"

#include <hx_disasm.h>

HX_BEGIN_DECLS

typedef struct _HooxX86Relocator HooxX86Relocator;

struct _HooxX86Relocator
{
  volatile hx_int ref_count;

  hx_csh capstone;

  const hx_uint8 * input_start;
  const hx_uint8 * input_cur;
  HooxAddress input_pc;
  hx_insn ** input_insns;
  HooxX86Writer * output;

  hx_uint inpos;
  hx_uint outpos;

  hx_boolean eob;
  hx_boolean eoi;
};


HOOX_API void hoox_x86_relocator_init (HooxX86Relocator * relocator,
    hx_constpointer input_code, HooxX86Writer * output);
HOOX_API void hoox_x86_relocator_clear (HooxX86Relocator * relocator);

HOOX_API void hoox_x86_relocator_reset (HooxX86Relocator * relocator,
    hx_constpointer input_code, HooxX86Writer * output);

HOOX_API hx_uint hoox_x86_relocator_read_one (HooxX86Relocator * self,
    const hx_insn ** instruction);

HOOX_API hx_insn * hoox_x86_relocator_peek_next_write_insn (
    HooxX86Relocator * self);
/* hoox:test-only-begin */
HOOX_API hx_pointer hoox_x86_relocator_peek_next_write_source (
    HooxX86Relocator * self);
/* hoox:test-only-end */
/* hoox:test-only-begin */
HOOX_API void hoox_x86_relocator_skip_one (HooxX86Relocator * self);
/* hoox:test-only-end */
HOOX_API hx_boolean hoox_x86_relocator_write_one (HooxX86Relocator * self);
HOOX_API void hoox_x86_relocator_write_all (HooxX86Relocator * self);

/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_relocator_eob (HooxX86Relocator * self);
/* hoox:test-only-end */
HOOX_API hx_boolean hoox_x86_relocator_eoi (HooxX86Relocator * self);

HOOX_API hx_boolean hoox_x86_relocator_can_relocate (hx_pointer address,
    hx_uint min_bytes, HooxRelocationScenario scenario, hx_uint * maximum);

HX_END_DECLS

#endif
