/*
 * Copyright (C) 2010-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_THUMB_WRITER_H__
#define __HOOX_THUMB_WRITER_H__

#include <hx_disasm.h>
#include "hooxdefs.h"
#include "hooxmetalarray.h"
#include "hooxmetalhash.h"

#define HOOX_THUMB_B_MAX_DISTANCE 0x00fffffe

HX_BEGIN_DECLS

typedef struct _HooxThumbWriter HooxThumbWriter;

struct _HooxThumbWriter
{
  volatile hx_int ref_count;
  hx_boolean flush_on_destroy;

  HooxOS target_os;

  hx_uint16 * base;
  hx_uint16 * code;
  HooxAddress pc;

  HooxMetalHashTable * label_defs;
  HooxMetalArray label_refs;
  HooxMetalArray literal_refs;
  const hx_uint16 * earliest_literal_insn;
};

HOOX_API HooxThumbWriter * hoox_thumb_writer_new (hx_pointer code_address);
HOOX_API HooxThumbWriter * hoox_thumb_writer_ref (HooxThumbWriter * writer);
HOOX_API void hoox_thumb_writer_unref (HooxThumbWriter * writer);

HOOX_API void hoox_thumb_writer_init (HooxThumbWriter * writer,
    hx_pointer code_address);
HOOX_API void hoox_thumb_writer_clear (HooxThumbWriter * writer);

HOOX_API void hoox_thumb_writer_reset (HooxThumbWriter * writer,
    hx_pointer code_address);
HOOX_API void hoox_thumb_writer_set_target_os (HooxThumbWriter * self, HooxOS os);

HOOX_API hx_pointer hoox_thumb_writer_cur (HooxThumbWriter * self);
HOOX_API hx_uint hoox_thumb_writer_offset (HooxThumbWriter * self);
HOOX_API void hoox_thumb_writer_skip (HooxThumbWriter * self, hx_uint n_bytes);

HOOX_API hx_boolean hoox_thumb_writer_flush (HooxThumbWriter * self);

HOOX_API hx_boolean hoox_thumb_writer_put_label (HooxThumbWriter * self,
    hx_constpointer id);
HOOX_API hx_boolean hoox_thumb_writer_commit_label (HooxThumbWriter * self,
    hx_constpointer id);

HOOX_API void hoox_thumb_writer_put_call_address_with_arguments (
    HooxThumbWriter * self, HooxAddress func, hx_uint n_args, ...);
HOOX_API void hoox_thumb_writer_put_call_address_with_arguments_array (
    HooxThumbWriter * self, HooxAddress func, hx_uint n_args,
    const HooxArgument * args);
HOOX_API void hoox_thumb_writer_put_call_reg_with_arguments (
    HooxThumbWriter * self, hx_arm_reg reg, hx_uint n_args, ...);
HOOX_API void hoox_thumb_writer_put_call_reg_with_arguments_array (
    HooxThumbWriter * self, hx_arm_reg reg, hx_uint n_args, const HooxArgument * args);

HOOX_API void hoox_thumb_writer_put_branch_address (HooxThumbWriter * self,
    HooxAddress address);

HOOX_API hx_boolean hoox_thumb_writer_can_branch_directly_between (
    HooxThumbWriter * self, HooxAddress from, HooxAddress to);
HOOX_API void hoox_thumb_writer_put_b_imm (HooxThumbWriter * self,
    HooxAddress target);
HOOX_API void hoox_thumb_writer_put_b_label (HooxThumbWriter * self,
    hx_constpointer label_id);
HOOX_API void hoox_thumb_writer_put_b_label_wide (HooxThumbWriter * self,
    hx_constpointer label_id);
HOOX_API void hoox_thumb_writer_put_bx_reg (HooxThumbWriter * self, hx_arm_reg reg);
HOOX_API void hoox_thumb_writer_put_bl_imm (HooxThumbWriter * self,
    HooxAddress target);
HOOX_API void hoox_thumb_writer_put_bl_label (HooxThumbWriter * self,
    hx_constpointer label_id);
HOOX_API void hoox_thumb_writer_put_blx_imm (HooxThumbWriter * self,
    HooxAddress target);
HOOX_API void hoox_thumb_writer_put_blx_reg (HooxThumbWriter * self, hx_arm_reg reg);
HOOX_API void hoox_thumb_writer_put_cmp_reg_imm (HooxThumbWriter * self,
    hx_arm_reg reg, hx_uint8 imm_value);
HOOX_API void hoox_thumb_writer_put_beq_label (HooxThumbWriter * self,
    hx_constpointer label_id);
HOOX_API void hoox_thumb_writer_put_bne_label (HooxThumbWriter * self,
    hx_constpointer label_id);
HOOX_API void hoox_thumb_writer_put_b_cond_label (HooxThumbWriter * self,
    hx_arm_cc cc, hx_constpointer label_id);
HOOX_API void hoox_thumb_writer_put_b_cond_label_wide (HooxThumbWriter * self,
    hx_arm_cc cc, hx_constpointer label_id);
HOOX_API void hoox_thumb_writer_put_cbz_reg_label (HooxThumbWriter * self,
    hx_arm_reg reg, hx_constpointer label_id);
HOOX_API void hoox_thumb_writer_put_cbnz_reg_label (HooxThumbWriter * self,
    hx_arm_reg reg, hx_constpointer label_id);

HOOX_API hx_boolean hoox_thumb_writer_put_push_regs (HooxThumbWriter * self,
    hx_uint n_regs, hx_arm_reg first_reg, ...);
HOOX_API hx_boolean hoox_thumb_writer_put_push_regs_array (HooxThumbWriter * self,
    hx_uint n_regs, const hx_arm_reg * regs);
HOOX_API hx_boolean hoox_thumb_writer_put_pop_regs (HooxThumbWriter * self,
    hx_uint n_regs, hx_arm_reg first_reg, ...);
HOOX_API hx_boolean hoox_thumb_writer_put_pop_regs_array (HooxThumbWriter * self,
    hx_uint n_regs, const hx_arm_reg * regs);
HOOX_API hx_boolean hoox_thumb_writer_put_vpush_range (HooxThumbWriter * self,
    hx_arm_reg first_reg, hx_arm_reg last_reg);
HOOX_API hx_boolean hoox_thumb_writer_put_vpop_range (HooxThumbWriter * self,
    hx_arm_reg first_reg, hx_arm_reg last_reg);
HOOX_API hx_boolean hoox_thumb_writer_put_ldr_reg_address (HooxThumbWriter * self,
    hx_arm_reg reg, HooxAddress address);
HOOX_API hx_boolean hoox_thumb_writer_put_ldr_reg_u32 (HooxThumbWriter * self,
    hx_arm_reg reg, hx_uint32 val);
HOOX_API void hoox_thumb_writer_put_ldr_reg_reg (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg);
HOOX_API hx_boolean hoox_thumb_writer_put_ldr_reg_reg_offset (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg, hx_size src_offset);
HOOX_API void hoox_thumb_writer_put_ldrb_reg_reg (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg);
void hoox_thumb_writer_put_ldrh_reg_reg (HooxThumbWriter * self, hx_arm_reg dst_reg,
    hx_arm_reg src_reg);
HOOX_API hx_boolean hoox_thumb_writer_put_vldr_reg_reg_offset (
    HooxThumbWriter * self, hx_arm_reg dst_reg, hx_arm_reg src_reg, hx_ssize src_offset);
HOOX_API void hoox_thumb_writer_put_ldmia_reg_mask (HooxThumbWriter * self,
    hx_arm_reg reg, hx_uint16 mask);
HOOX_API void hoox_thumb_writer_put_str_reg_reg (HooxThumbWriter * self,
    hx_arm_reg src_reg, hx_arm_reg dst_reg);
HOOX_API hx_boolean hoox_thumb_writer_put_str_reg_reg_offset (HooxThumbWriter * self,
    hx_arm_reg src_reg, hx_arm_reg dst_reg, hx_size dst_offset);
HOOX_API void hoox_thumb_writer_put_mov_reg_reg (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg);
HOOX_API void hoox_thumb_writer_put_mov_reg_u8 (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_uint8 imm_value);
HOOX_API void hoox_thumb_writer_put_mov_reg_cpsr (HooxThumbWriter * self,
    hx_arm_reg reg);
HOOX_API void hoox_thumb_writer_put_mov_cpsr_reg (HooxThumbWriter * self,
    hx_arm_reg reg);
HOOX_API hx_boolean hoox_thumb_writer_put_add_reg_imm (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_ssize imm_value);
HOOX_API void hoox_thumb_writer_put_add_reg_reg (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg);
HOOX_API void hoox_thumb_writer_put_add_reg_reg_reg (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg left_reg, hx_arm_reg right_reg);
HOOX_API hx_boolean hoox_thumb_writer_put_add_reg_reg_imm (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg left_reg, hx_ssize right_value);
HOOX_API hx_boolean hoox_thumb_writer_put_sub_reg_imm (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_ssize imm_value);
HOOX_API void hoox_thumb_writer_put_sub_reg_reg (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg src_reg);
HOOX_API void hoox_thumb_writer_put_sub_reg_reg_reg (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg left_reg, hx_arm_reg right_reg);
HOOX_API hx_boolean hoox_thumb_writer_put_sub_reg_reg_imm (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg left_reg, hx_ssize right_value);
HOOX_API hx_boolean hoox_thumb_writer_put_and_reg_reg_imm (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg left_reg, hx_ssize right_value);
HOOX_API hx_boolean hoox_thumb_writer_put_or_reg_reg_imm (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg left_reg, hx_ssize right_value);
HOOX_API hx_boolean hoox_thumb_writer_put_lsl_reg_reg_imm (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg left_reg, hx_uint8 right_value);
HOOX_API hx_boolean hoox_thumb_writer_put_lsls_reg_reg_imm (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg left_reg, hx_uint8 right_value);
HOOX_API hx_boolean hoox_thumb_writer_put_lsrs_reg_reg_imm (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_reg left_reg, hx_uint8 right_value);
HOOX_API hx_boolean hoox_thumb_writer_put_mrs_reg_reg (HooxThumbWriter * self,
    hx_arm_reg dst_reg, hx_arm_sysreg src_reg);
HOOX_API hx_boolean hoox_thumb_writer_put_msr_reg_reg (HooxThumbWriter * self,
    hx_arm_sysreg dst_reg, hx_arm_reg src_reg);

HOOX_API void hoox_thumb_writer_put_nop (HooxThumbWriter * self);
HOOX_API void hoox_thumb_writer_put_bkpt_imm (HooxThumbWriter * self, hx_uint8 imm);
HOOX_API void hoox_thumb_writer_put_breakpoint (HooxThumbWriter * self);

HOOX_API void hoox_thumb_writer_put_instruction (HooxThumbWriter * self,
    hx_uint16 insn);
HOOX_API void hoox_thumb_writer_put_instruction_wide (HooxThumbWriter * self,
    hx_uint16 upper, hx_uint16 lower);
HOOX_API hx_boolean hoox_thumb_writer_put_bytes (HooxThumbWriter * self,
    const hx_uint8 * data, hx_uint n);

HX_END_DECLS

#endif
