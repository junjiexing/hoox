/*
 * Copyright (C) 2010-2017 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_THUMB_RELOCATOR_H__
#define __HOOX_THUMB_RELOCATOR_H__

#include "hooxthumbwriter.h"

#include <hx_disasm.h>

HX_BEGIN_DECLS

typedef struct _HooxThumbRelocator HooxThumbRelocator;
typedef struct _HooxITBlock HooxITBlock;
typedef hx_uint HooxITBranchType;

struct _HooxITBlock
{
  hx_boolean active;
  hx_arm_cc cc;

  const hx_insn * insns[4];
  hx_uint8 offset;
  hx_uint8 size;
  hx_uint8 else_region_size;

  hx_pointer then_label;
  hx_pointer end_label;
};

struct _HooxThumbRelocator
{
  volatile hx_int ref_count;

  hx_csh capstone;

  const hx_uint8 * input_start;
  const hx_uint8 * input_cur;
  HooxAddress input_pc;
  hx_insn ** input_insns;
  HooxThumbWriter * output;

  hx_uint inpos;
  hx_uint outpos;

  hx_boolean eob;
  hx_boolean eoi;
  hx_boolean unsupported;

  HooxITBlock it_block;
  HooxITBranchType it_branch_type;
};

enum _HooxITBranchType
{
  HOOX_IT_BRANCH_INVALID,
  HOOX_IT_BRANCH_SHORT,
  HOOX_IT_BRANCH_LONG
};

HOOX_API HooxThumbRelocator * hoox_thumb_relocator_new (hx_constpointer input_code,
    HooxThumbWriter * output);
HOOX_API HooxThumbRelocator * hoox_thumb_relocator_ref (
    HooxThumbRelocator * relocator);
HOOX_API void hoox_thumb_relocator_unref (HooxThumbRelocator * relocator);

HOOX_API void hoox_thumb_relocator_init (HooxThumbRelocator * relocator,
    hx_constpointer input_code, HooxThumbWriter * output);
HOOX_API void hoox_thumb_relocator_clear (HooxThumbRelocator * relocator);

HOOX_API void hoox_thumb_relocator_reset (HooxThumbRelocator * relocator,
    hx_constpointer input_code, HooxThumbWriter * output);
void hoox_thumb_relocator_set_it_branch_type (HooxThumbRelocator * self,
    HooxITBranchType type);

HOOX_API hx_uint hoox_thumb_relocator_read_one (HooxThumbRelocator * self,
    const hx_insn ** instruction);

HOOX_API hx_boolean hoox_thumb_relocator_is_eob_instruction (
    const hx_insn * instruction);

HOOX_API hx_insn * hoox_thumb_relocator_peek_next_write_insn (
    HooxThumbRelocator * self);
HOOX_API hx_pointer hoox_thumb_relocator_peek_next_write_source (
    HooxThumbRelocator * self);
HOOX_API void hoox_thumb_relocator_skip_one (HooxThumbRelocator * self);
HOOX_API hx_boolean hoox_thumb_relocator_write_one (HooxThumbRelocator * self);
HOOX_API hx_boolean hoox_thumb_relocator_copy_one (HooxThumbRelocator * self);
HOOX_API void hoox_thumb_relocator_write_all (HooxThumbRelocator * self);

HOOX_API hx_boolean hoox_thumb_relocator_eob (HooxThumbRelocator * self);
HOOX_API hx_boolean hoox_thumb_relocator_eoi (HooxThumbRelocator * self);

HOOX_API hx_boolean hoox_thumb_relocator_can_relocate (hx_pointer address,
    hx_uint min_bytes, HooxRelocationScenario scenario, hx_uint * maximum);
HOOX_API hx_uint hoox_thumb_relocator_relocate (hx_pointer from, hx_uint min_bytes,
    hx_pointer to);

HX_END_DECLS

#endif
