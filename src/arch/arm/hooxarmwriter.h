/*
 * Copyright (C) 2010-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_ARM_WRITER_H__
#define __HOOX_ARM_WRITER_H__

#include <hx_disasm.h>
#include "hooxdefs.h"
#include "hooxmetalarray.h"
#include "hooxmetalhash.h"

#define HOOX_ARM_B_MAX_DISTANCE 0x01fffffc

HX_BEGIN_DECLS

typedef struct _HooxArmWriter HooxArmWriter;

struct _HooxArmWriter
{
  volatile hx_int ref_count;
  hx_boolean flush_on_destroy;

  HooxOS target_os;
  HooxCpuFeatures cpu_features;

  hx_uint32 * base;
  hx_uint32 * code;
  HooxAddress pc;

  HooxMetalHashTable * label_defs;
  HooxMetalArray label_refs;
  HooxMetalArray literal_refs;
  const hx_uint32 * earliest_literal_insn;
};

HOOX_API HooxArmWriter * hoox_arm_writer_new (hx_pointer code_address);
HOOX_API HooxArmWriter * hoox_arm_writer_ref (HooxArmWriter * writer);
HOOX_API void hoox_arm_writer_unref (HooxArmWriter * writer);

HOOX_API void hoox_arm_writer_init (HooxArmWriter * writer, hx_pointer code_address);
HOOX_API void hoox_arm_writer_clear (HooxArmWriter * writer);

HOOX_API void hoox_arm_writer_reset (HooxArmWriter * writer,
    hx_pointer code_address);
HOOX_API void hoox_arm_writer_set_target_os (HooxArmWriter * self, HooxOS os);

HOOX_API hx_pointer hoox_arm_writer_cur (HooxArmWriter * self);
HOOX_API hx_uint hoox_arm_writer_offset (HooxArmWriter * self);
HOOX_API void hoox_arm_writer_skip (HooxArmWriter * self, hx_uint n_bytes);

HOOX_API hx_boolean hoox_arm_writer_flush (HooxArmWriter * self);

HOOX_API hx_boolean hoox_arm_writer_put_label (HooxArmWriter * self,
    hx_constpointer id);

HOOX_API void hoox_arm_writer_put_call_address_with_arguments (
    HooxArmWriter * self, HooxAddress func, hx_uint n_args, ...);
HOOX_API void hoox_arm_writer_put_call_address_with_arguments_array (
    HooxArmWriter * self, HooxAddress func, hx_uint n_args,
    const HooxArgument * args);
HOOX_API void hoox_arm_writer_put_call_reg (HooxArmWriter * self, hx_arm_reg reg);
HOOX_API void hoox_arm_writer_put_call_reg_with_arguments (HooxArmWriter * self,
    hx_arm_reg reg, hx_uint n_args, ...);
HOOX_API void hoox_arm_writer_put_call_reg_with_arguments_array (
    HooxArmWriter * self, hx_arm_reg reg, hx_uint n_args, const HooxArgument * args);

HOOX_API void hoox_arm_writer_put_branch_address (HooxArmWriter * self,
    HooxAddress address);

HOOX_API hx_boolean hoox_arm_writer_can_branch_directly_between (
    HooxArmWriter * self, HooxAddress from, HooxAddress to);
HOOX_API hx_boolean hoox_arm_writer_put_b_imm (HooxArmWriter * self,
    HooxAddress target);
HOOX_API hx_boolean hoox_arm_writer_put_b_cond_imm (HooxArmWriter * self,
    hx_arm_cc cc, HooxAddress target);
HOOX_API void hoox_arm_writer_put_b_label (HooxArmWriter * self,
    hx_constpointer label_id);
HOOX_API void hoox_arm_writer_put_b_cond_label (HooxArmWriter * self,
    hx_arm_cc cc, hx_constpointer label_id);
HOOX_API hx_boolean hoox_arm_writer_put_bl_imm (HooxArmWriter * self,
    HooxAddress target);
HOOX_API hx_boolean hoox_arm_writer_put_blx_imm (HooxArmWriter * self,
    HooxAddress target);
HOOX_API void hoox_arm_writer_put_bl_label (HooxArmWriter * self,
    hx_constpointer label_id);
HOOX_API void hoox_arm_writer_put_bx_reg (HooxArmWriter * self, hx_arm_reg reg);
HOOX_API void hoox_arm_writer_put_bl_reg (HooxArmWriter * self, hx_arm_reg reg);
HOOX_API void hoox_arm_writer_put_blx_reg (HooxArmWriter * self, hx_arm_reg reg);
HOOX_API void hoox_arm_writer_put_ret (HooxArmWriter * self);

HOOX_API void hoox_arm_writer_put_push_regs (HooxArmWriter * self, hx_uint n, ...);
HOOX_API void hoox_arm_writer_put_pop_regs (HooxArmWriter * self, hx_uint n, ...);
HOOX_API hx_boolean hoox_arm_writer_put_vpush_range (HooxArmWriter * self,
    hx_arm_reg first_reg, hx_arm_reg last_reg);
HOOX_API hx_boolean hoox_arm_writer_put_vpop_range (HooxArmWriter * self,
    hx_arm_reg first_reg, hx_arm_reg last_reg);

HOOX_API hx_boolean hoox_arm_writer_put_ldr_reg_address (HooxArmWriter * self,
    hx_arm_reg reg, HooxAddress address);
HOOX_API hx_boolean hoox_arm_writer_put_ldr_reg_u32 (HooxArmWriter * self,
    hx_arm_reg reg, hx_uint32 val);
HOOX_API hx_boolean hoox_arm_writer_put_ldr_reg_reg (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg);
HOOX_API hx_boolean hoox_arm_writer_put_ldr_reg_reg_offset (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg, hx_ssize src_offset);
HOOX_API hx_boolean hoox_arm_writer_put_ldr_cond_reg_reg_offset (
    HooxArmWriter * self, hx_arm_cc cc, hx_arm_reg dst_reg, hx_arm_reg src_reg,
    hx_ssize src_offset);
HOOX_API void hoox_arm_writer_put_ldmia_reg_mask (HooxArmWriter * self,
    hx_arm_reg reg, hx_uint16 mask);
HOOX_API void hoox_arm_writer_put_ldmia_reg_mask_wb (HooxArmWriter * self,
    hx_arm_reg reg, hx_uint16 mask);
HOOX_API hx_boolean hoox_arm_writer_put_str_reg_reg (HooxArmWriter * self,
    hx_arm_reg src_reg, hx_arm_reg dst_reg);
HOOX_API hx_boolean hoox_arm_writer_put_str_reg_reg_offset (HooxArmWriter * self,
    hx_arm_reg src_reg, hx_arm_reg dst_reg, hx_ssize dst_offset);
HOOX_API hx_boolean hoox_arm_writer_put_str_cond_reg_reg_offset (
    HooxArmWriter * self, hx_arm_cc cc, hx_arm_reg src_reg,
    hx_arm_reg dst_reg, hx_ssize dst_offset);
HOOX_API void hoox_arm_writer_put_mov_reg_reg (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg);
HOOX_API void hoox_arm_writer_put_mov_reg_reg_shift (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg, hx_arm_shifter shift,
    hx_uint16 shift_value);
HOOX_API void hoox_arm_writer_put_mov_reg_cpsr (HooxArmWriter * self, hx_arm_reg reg);
HOOX_API void hoox_arm_writer_put_mov_cpsr_reg (HooxArmWriter * self, hx_arm_reg reg);
HOOX_API void hoox_arm_writer_put_add_reg_u16 (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_uint16 val);
HOOX_API void hoox_arm_writer_put_add_reg_u32 (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_uint32 val);
HOOX_API void hoox_arm_writer_put_add_reg_reg_imm (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg, hx_uint32 imm_val);
HOOX_API void hoox_arm_writer_put_add_reg_reg_reg (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg1, hx_arm_reg src_reg2);
HOOX_API void hoox_arm_writer_put_add_reg_reg_reg_shift (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg1, hx_arm_reg src_reg2, hx_arm_shifter shift,
    hx_uint16 shift_value);
HOOX_API void hoox_arm_writer_put_sub_reg_u16 (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_uint16 val);
HOOX_API void hoox_arm_writer_put_sub_reg_u32 (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_uint32 val);
HOOX_API void hoox_arm_writer_put_sub_reg_reg_imm (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg, hx_uint32 imm_val);
HOOX_API void hoox_arm_writer_put_sub_reg_reg_reg (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg1, hx_arm_reg src_reg2);
HOOX_API void hoox_arm_writer_put_rsb_reg_reg_imm (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg, hx_uint32 imm_val);
HOOX_API void hoox_arm_writer_put_ands_reg_reg_imm (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg, hx_uint32 imm_val);
HOOX_API void hoox_arm_writer_put_cmp_reg_imm (HooxArmWriter * self,
    hx_arm_reg dst_reg, hx_uint32 imm_val);

HOOX_API void hoox_arm_writer_put_nop (HooxArmWriter * self);
HOOX_API void hoox_arm_writer_put_breakpoint (HooxArmWriter * self);
HOOX_API void hoox_arm_writer_put_brk_imm (HooxArmWriter * self,
    hx_uint16 imm);

HOOX_API void hoox_arm_writer_put_instruction (HooxArmWriter * self, hx_uint32 insn);
HOOX_API hx_boolean hoox_arm_writer_put_bytes (HooxArmWriter * self,
    const hx_uint8 * data, hx_uint n);

HX_END_DECLS

#endif
