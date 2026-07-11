/*
 * Copyright (C) 2014-2023 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2017 Antonio Ken Iannillo <ak.iannillo@gmail.com>
 * Copyright (C) 2023 Håvard Sørbø <havard@hsorbo.no>
 * Copyright (C) 2023 Fabian Freyer <fabian.freyer@physik.tu-berlin.de>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_ARM64_WRITER_H__
#define __HOOX_ARM64_WRITER_H__

#include <hx_disasm.h>
#include "hooxdefs.h"
#include "hooxmemory.h"
#include "hooxmetalarray.h"
#include "hooxmetalhash.h"

#define HOOX_ARM64_ADRP_MAX_DISTANCE 0xfffff000
#define HOOX_ARM64_B_MAX_DISTANCE 0x07fffffc

#define HOOX_ARM64_SYSREG(op0, op1, crn, crm, op2) \
    ( \
      (((op0 == 2) ? 0 : 1) << 14) | \
      (op1 << 11) | \
      (crn << 7) | \
      (crm << 3) | \
      op2 \
    )
#define HOOX_ARM64_SYSREG_TPIDRRO_EL0 HOOX_ARM64_SYSREG (3, 3, 13, 0, 3)

HX_BEGIN_DECLS

typedef struct _HooxArm64Writer HooxArm64Writer;
typedef hx_uint HooxArm64IndexMode;

/*
 * Valid values:
 * - HX_LITTLE_ENDIAN
 * - HX_BIG_ENDIAN
 * - HX_BYTE_ORDER (an alias for one of the above)
 */
typedef int HooxArm64DataEndian;

struct _HooxArm64Writer
{
  volatile hx_int ref_count;
  hx_boolean flush_on_destroy;

  /*
   * Whilst instructions in AArch64 are always in little endian (even on
   * big-endian systems), the data is in native endian. Thus since we wish to
   * support writing code for big-endian systems on little-endian targets and
   * vice versa, we need to check the writer configuration before writing data.
   */
  HooxArm64DataEndian data_endian;
  HooxOS target_os;
  HooxPtrauthSupport ptrauth_support;
  HooxAddress (* sign) (HooxAddress value);

  hx_uint32 * base;
  hx_uint32 * code;
  HooxAddress pc;

  HooxMetalHashTable * label_defs;
  HooxMetalArray label_refs;
  HooxMetalArray literal_refs;
  const hx_uint32 * earliest_literal_insn;
};

enum _HooxArm64IndexMode
{
  HOOX_INDEX_POST_ADJUST   = 1,
  HOOX_INDEX_SIGNED_OFFSET = 2,
  HOOX_INDEX_PRE_ADJUST    = 3,
};

HOOX_API HooxArm64Writer * hoox_arm64_writer_new (hx_pointer code_address);
HOOX_API HooxArm64Writer * hoox_arm64_writer_ref (HooxArm64Writer * writer);
HOOX_API void hoox_arm64_writer_unref (HooxArm64Writer * writer);

HOOX_API void hoox_arm64_writer_init (HooxArm64Writer * writer,
    hx_pointer code_address);
HOOX_API void hoox_arm64_writer_clear (HooxArm64Writer * writer);

HOOX_API void hoox_arm64_writer_reset (HooxArm64Writer * writer,
    hx_pointer code_address);

HOOX_API hx_pointer hoox_arm64_writer_cur (HooxArm64Writer * self);
HOOX_API hx_uint hoox_arm64_writer_offset (HooxArm64Writer * self);
HOOX_API void hoox_arm64_writer_skip (HooxArm64Writer * self, hx_uint n_bytes);

HOOX_API hx_boolean hoox_arm64_writer_flush (HooxArm64Writer * self);

HOOX_API hx_boolean hoox_arm64_writer_put_label (HooxArm64Writer * self,
    hx_constpointer id);

HOOX_API void hoox_arm64_writer_put_call_address_with_arguments (
    HooxArm64Writer * self, HooxAddress func, hx_uint n_args, ...);
HOOX_API void hoox_arm64_writer_put_call_address_with_arguments_array (
    HooxArm64Writer * self, HooxAddress func, hx_uint n_args,
    const HooxArgument * args);
HOOX_API void hoox_arm64_writer_put_call_reg_with_arguments (
    HooxArm64Writer * self, hx_arm64_reg reg, hx_uint n_args, ...);
HOOX_API void hoox_arm64_writer_put_call_reg_with_arguments_array (
    HooxArm64Writer * self, hx_arm64_reg reg, hx_uint n_args,
    const HooxArgument * args);

HOOX_API void hoox_arm64_writer_put_branch_address (HooxArm64Writer * self,
    HooxAddress address);

HOOX_API hx_boolean hoox_arm64_writer_can_branch_directly_between (
    HooxArm64Writer * self, HooxAddress from, HooxAddress to);
HOOX_API hx_boolean hoox_arm64_writer_put_b_imm (HooxArm64Writer * self,
    HooxAddress address);
HOOX_API void hoox_arm64_writer_put_b_label (HooxArm64Writer * self,
    hx_constpointer label_id);
HOOX_API void hoox_arm64_writer_put_b_cond_label (HooxArm64Writer * self,
    hx_arm64_cc cc, hx_constpointer label_id);
HOOX_API hx_boolean hoox_arm64_writer_put_bl_imm (HooxArm64Writer * self,
    HooxAddress address);
HOOX_API void hoox_arm64_writer_put_bl_label (HooxArm64Writer * self,
    hx_constpointer label_id);
HOOX_API hx_boolean hoox_arm64_writer_put_br_reg (HooxArm64Writer * self,
    hx_arm64_reg reg);
HOOX_API hx_boolean hoox_arm64_writer_put_br_reg_no_auth (HooxArm64Writer * self,
    hx_arm64_reg reg);
HOOX_API hx_boolean hoox_arm64_writer_put_blr_reg (HooxArm64Writer * self,
    hx_arm64_reg reg);
HOOX_API hx_boolean hoox_arm64_writer_put_blr_reg_no_auth (HooxArm64Writer * self,
    hx_arm64_reg reg);
HOOX_API void hoox_arm64_writer_put_ret (HooxArm64Writer * self);
HOOX_API hx_boolean hoox_arm64_writer_put_ret_reg (HooxArm64Writer * self,
    hx_arm64_reg reg);
HOOX_API hx_boolean hoox_arm64_writer_put_cbz_reg_imm (HooxArm64Writer * self,
    hx_arm64_reg reg, HooxAddress target);
HOOX_API hx_boolean hoox_arm64_writer_put_cbnz_reg_imm (HooxArm64Writer * self,
    hx_arm64_reg reg, HooxAddress target);
HOOX_API void hoox_arm64_writer_put_cbz_reg_label (HooxArm64Writer * self,
    hx_arm64_reg reg, hx_constpointer label_id);
HOOX_API void hoox_arm64_writer_put_cbnz_reg_label (HooxArm64Writer * self,
    hx_arm64_reg reg, hx_constpointer label_id);
HOOX_API hx_boolean hoox_arm64_writer_put_tbz_reg_imm_imm (HooxArm64Writer * self,
    hx_arm64_reg reg, hx_uint bit, HooxAddress target);
HOOX_API hx_boolean hoox_arm64_writer_put_tbnz_reg_imm_imm (HooxArm64Writer * self,
    hx_arm64_reg reg, hx_uint bit, HooxAddress target);
HOOX_API void hoox_arm64_writer_put_tbz_reg_imm_label (HooxArm64Writer * self,
    hx_arm64_reg reg, hx_uint bit, hx_constpointer label_id);
HOOX_API void hoox_arm64_writer_put_tbnz_reg_imm_label (HooxArm64Writer * self,
    hx_arm64_reg reg, hx_uint bit, hx_constpointer label_id);

HOOX_API hx_boolean hoox_arm64_writer_put_push_reg_reg (HooxArm64Writer * self,
    hx_arm64_reg reg_a, hx_arm64_reg reg_b);
HOOX_API hx_boolean hoox_arm64_writer_put_pop_reg_reg (HooxArm64Writer * self,
    hx_arm64_reg reg_a, hx_arm64_reg reg_b);
HOOX_API void hoox_arm64_writer_put_push_all_x_registers (HooxArm64Writer * self);
HOOX_API void hoox_arm64_writer_put_pop_all_x_registers (HooxArm64Writer * self);
HOOX_API void hoox_arm64_writer_put_push_all_q_registers (HooxArm64Writer * self);
HOOX_API void hoox_arm64_writer_put_pop_all_q_registers (HooxArm64Writer * self);

HOOX_API hx_boolean hoox_arm64_writer_put_ldr_reg_address (HooxArm64Writer * self,
    hx_arm64_reg reg, HooxAddress address);
HOOX_API hx_boolean hoox_arm64_writer_put_ldr_reg_u32 (HooxArm64Writer * self,
    hx_arm64_reg reg, hx_uint32 val);
HOOX_API hx_boolean hoox_arm64_writer_put_ldr_reg_u64 (HooxArm64Writer * self,
    hx_arm64_reg reg, hx_uint64 val);
HOOX_API hx_boolean hoox_arm64_writer_put_ldr_reg_u32_ptr (HooxArm64Writer * self,
    hx_arm64_reg reg, HooxAddress src_address);
HOOX_API hx_boolean hoox_arm64_writer_put_ldr_reg_u64_ptr (HooxArm64Writer * self,
    hx_arm64_reg reg, HooxAddress src_address);
HOOX_API hx_uint hoox_arm64_writer_put_ldr_reg_ref (HooxArm64Writer * self,
    hx_arm64_reg reg);
HOOX_API void hoox_arm64_writer_put_ldr_reg_value (HooxArm64Writer * self,
    hx_uint ref, HooxAddress value);
HOOX_API hx_boolean hoox_arm64_writer_put_ldr_reg_reg (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg src_reg);
HOOX_API hx_boolean hoox_arm64_writer_put_ldr_reg_reg_offset (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg src_reg, hx_size src_offset);
HOOX_API hx_boolean hoox_arm64_writer_put_ldr_reg_reg_offset_mode (
    HooxArm64Writer * self, hx_arm64_reg dst_reg, hx_arm64_reg src_reg,
    hx_ssize src_offset, HooxArm64IndexMode mode);
HOOX_API hx_boolean hoox_arm64_writer_put_ldrsw_reg_reg_offset (
    HooxArm64Writer * self, hx_arm64_reg dst_reg, hx_arm64_reg src_reg,
    hx_size src_offset);
HOOX_API hx_boolean hoox_arm64_writer_put_adrp_reg_address (HooxArm64Writer * self,
    hx_arm64_reg reg, HooxAddress address);
HOOX_API hx_boolean hoox_arm64_writer_put_str_reg_reg (HooxArm64Writer * self,
    hx_arm64_reg src_reg, hx_arm64_reg dst_reg);
HOOX_API hx_boolean hoox_arm64_writer_put_str_reg_reg_offset (HooxArm64Writer * self,
    hx_arm64_reg src_reg, hx_arm64_reg dst_reg, hx_size dst_offset);
HOOX_API hx_boolean hoox_arm64_writer_put_str_reg_reg_offset_mode (
    HooxArm64Writer * self, hx_arm64_reg src_reg, hx_arm64_reg dst_reg,
    hx_ssize dst_offset, HooxArm64IndexMode mode);
HOOX_API hx_boolean hoox_arm64_writer_put_ldp_reg_reg_reg_offset (
    HooxArm64Writer * self, hx_arm64_reg reg_a, hx_arm64_reg reg_b, hx_arm64_reg reg_src,
    hx_ssize src_offset, HooxArm64IndexMode mode);
HOOX_API hx_boolean hoox_arm64_writer_put_stp_reg_reg_reg_offset (
    HooxArm64Writer * self, hx_arm64_reg reg_a, hx_arm64_reg reg_b, hx_arm64_reg reg_dst,
    hx_ssize dst_offset, HooxArm64IndexMode mode);
HOOX_API hx_boolean hoox_arm64_writer_put_mov_reg_reg (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg src_reg);
HOOX_API void hoox_arm64_writer_put_mov_reg_nzcv (HooxArm64Writer * self,
    hx_arm64_reg reg);
HOOX_API void hoox_arm64_writer_put_mov_nzcv_reg (HooxArm64Writer * self,
    hx_arm64_reg reg);
HOOX_API hx_boolean hoox_arm64_writer_put_uxtw_reg_reg (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg src_reg);
HOOX_API hx_boolean hoox_arm64_writer_put_add_reg_reg_imm (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg left_reg, hx_size right_value);
HOOX_API hx_boolean hoox_arm64_writer_put_add_reg_reg_reg (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg left_reg, hx_arm64_reg right_reg);
HOOX_API hx_boolean hoox_arm64_writer_put_sub_reg_reg_imm (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg left_reg, hx_size right_value);
HOOX_API hx_boolean hoox_arm64_writer_put_sub_reg_reg_reg (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg left_reg, hx_arm64_reg right_reg);
HOOX_API hx_boolean hoox_arm64_writer_put_and_reg_reg_imm (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg left_reg, hx_uint64 right_value);
HOOX_API hx_boolean hoox_arm64_writer_put_eor_reg_reg_reg (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg left_reg, hx_arm64_reg right_reg);
HOOX_API hx_boolean hoox_arm64_writer_put_ubfm (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg src_reg, hx_uint8 imms, hx_uint8 immr);
HOOX_API hx_boolean hoox_arm64_writer_put_lsl_reg_imm (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg src_reg, hx_uint8 shift);
HOOX_API hx_boolean hoox_arm64_writer_put_lsr_reg_imm (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_arm64_reg src_reg, hx_uint8 shift);
HOOX_API hx_boolean hoox_arm64_writer_put_tst_reg_imm (HooxArm64Writer * self,
    hx_arm64_reg reg, hx_uint64 imm_value);
HOOX_API hx_boolean hoox_arm64_writer_put_cmp_reg_reg (HooxArm64Writer * self,
    hx_arm64_reg reg_a, hx_arm64_reg reg_b);

HOOX_API hx_boolean hoox_arm64_writer_put_xpaci_reg (HooxArm64Writer * self,
    hx_arm64_reg reg);

HOOX_API void hoox_arm64_writer_put_nop (HooxArm64Writer * self);
HOOX_API void hoox_arm64_writer_put_brk_imm (HooxArm64Writer * self, hx_uint16 imm);
HOOX_API hx_boolean hoox_arm64_writer_put_mrs (HooxArm64Writer * self,
    hx_arm64_reg dst_reg, hx_uint16 system_reg);

HOOX_API void hoox_arm64_writer_put_instruction (HooxArm64Writer * self,
    hx_uint32 insn);
HOOX_API hx_boolean hoox_arm64_writer_put_bytes (HooxArm64Writer * self,
    const hx_uint8 * data, hx_uint n);

HOOX_API HooxAddress hoox_arm64_writer_sign (HooxArm64Writer * self,
    HooxAddress value);

HX_END_DECLS

#endif
