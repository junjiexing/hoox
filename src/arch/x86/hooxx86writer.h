/*
 * Copyright (C) 2009-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2023 Fabian Freyer <fabian.freyer@physik.tu-berlin.de>
 * Copyright (C) 2024 Yannis Juglaret <yjuglaret@mozilla.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_HX_WRITER_H__
#define __HOOX_HX_WRITER_H__

#include "hooxdefs.h"
#include "hooxmetalarray.h"
#include "hooxmetalhash.h"

#include <hx_disasm.h>

HX_BEGIN_DECLS

typedef struct _HooxX86Writer HooxX86Writer;
typedef hx_uint HooxX86Reg;
typedef hx_uint HooxX86PtrTarget;

struct _HooxX86Writer
{
  volatile hx_int ref_count;
  hx_boolean flush_on_destroy;

  HooxCpuType target_cpu;
  HooxAbiType target_abi;
  HooxCpuFeatures cpu_features;

  hx_uint8 * base;
  hx_uint8 * code;
  HooxAddress pc;

  HooxMetalHashTable * label_defs;
  HooxMetalArray label_refs;
};

enum _HooxX86Reg
{
  /* 32-bit */
  HOOX_HX_EAX = 0,
  HOOX_HX_ECX,
  HOOX_HX_EDX,
  HOOX_HX_EBX,
  HOOX_HX_ESP,
  HOOX_HX_EBP,
  HOOX_HX_ESI,
  HOOX_HX_EDI,

  HOOX_HX_R8D,
  HOOX_HX_R9D,
  HOOX_HX_R10D,
  HOOX_HX_R11D,
  HOOX_HX_R12D,
  HOOX_HX_R13D,
  HOOX_HX_R14D,
  HOOX_HX_R15D,

  HOOX_HX_EIP,

  /* 64-bit */
  HOOX_HX_RAX,
  HOOX_HX_RCX,
  HOOX_HX_RDX,
  HOOX_HX_RBX,
  HOOX_HX_RSP,
  HOOX_HX_RBP,
  HOOX_HX_RSI,
  HOOX_HX_RDI,

  HOOX_HX_R8,
  HOOX_HX_R9,
  HOOX_HX_R10,
  HOOX_HX_R11,
  HOOX_HX_R12,
  HOOX_HX_R13,
  HOOX_HX_R14,
  HOOX_HX_R15,

  HOOX_HX_RIP,

  /* Meta */
  HOOX_HX_XAX,
  HOOX_HX_XCX,
  HOOX_HX_XDX,
  HOOX_HX_XBX,
  HOOX_HX_XSP,
  HOOX_HX_XBP,
  HOOX_HX_XSI,
  HOOX_HX_XDI,

  HOOX_HX_XIP,

  HOOX_HX_NONE
};

enum _HooxX86PtrTarget
{
  HOOX_HX_PTR_BYTE,
  HOOX_HX_PTR_DWORD,
  HOOX_HX_PTR_QWORD
};

HOOX_API HooxX86Writer * hoox_x86_writer_ref (HooxX86Writer * writer);
HOOX_API void hoox_x86_writer_unref (HooxX86Writer * writer);

HOOX_API void hoox_x86_writer_init (HooxX86Writer * writer,
    hx_pointer code_address);
HOOX_API void hoox_x86_writer_clear (HooxX86Writer * writer);

HOOX_API void hoox_x86_writer_reset (HooxX86Writer * writer,
    hx_pointer code_address);
/* hoox:test-only-begin */
HOOX_API void hoox_x86_writer_set_target_cpu (HooxX86Writer * self,
    HooxCpuType cpu_type);
/* hoox:test-only-end */
/* hoox:test-only-begin */
HOOX_API void hoox_x86_writer_set_target_abi (HooxX86Writer * self,
    HooxAbiType abi_type);
/* hoox:test-only-end */

HOOX_API hx_uint hoox_x86_writer_offset (HooxX86Writer * self);

HOOX_API hx_boolean hoox_x86_writer_flush (HooxX86Writer * self);


HOOX_API hx_boolean hoox_x86_writer_put_label (HooxX86Writer * self,
    hx_constpointer id);

/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_call_address_with_arguments (
    HooxX86Writer * self, HooxCallingConvention conv, HooxAddress func,
    hx_uint n_args, ...);
/* hoox:test-only-end */
HOOX_API hx_boolean hoox_x86_writer_put_call_address_with_aligned_arguments (
    HooxX86Writer * self, HooxCallingConvention conv, HooxAddress func,
    hx_uint n_args, ...);
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_call_reg_with_arguments (
    HooxX86Writer * self, HooxCallingConvention conv, HooxX86Reg reg,
    hx_uint n_args, ...);
/* hoox:test-only-end */
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_call_reg_offset_ptr_with_arguments (
    HooxX86Writer * self, HooxCallingConvention conv, HooxX86Reg reg,
    hx_ssize offset, hx_uint n_args, ...);
/* hoox:test-only-end */
HOOX_API hx_boolean hoox_x86_writer_put_call_address (HooxX86Writer * self,
    HooxAddress address);
HOOX_API hx_boolean hoox_x86_writer_put_call_reg (HooxX86Writer * self,
    HooxX86Reg reg);
HOOX_API hx_boolean hoox_x86_writer_put_call_reg_offset_ptr (HooxX86Writer * self,
    HooxX86Reg reg, hx_ssize offset);
HOOX_API hx_boolean hoox_x86_writer_put_call_indirect (HooxX86Writer * self,
    HooxAddress addr);
HOOX_API hx_boolean hoox_x86_writer_put_call_indirect_label (HooxX86Writer * self,
    hx_constpointer label_id);
/* hoox:test-only-begin */
HOOX_API void hoox_x86_writer_put_call_near_label (HooxX86Writer * self,
    hx_constpointer label_id);
/* hoox:test-only-end */
HOOX_API void hoox_x86_writer_put_ret (HooxX86Writer * self);
HOOX_API hx_boolean hoox_x86_writer_put_jmp_address (HooxX86Writer * self,
    HooxAddress address);
HOOX_API void hoox_x86_writer_put_jmp_short_label (HooxX86Writer * self,
    hx_constpointer label_id);
HOOX_API hx_boolean hoox_x86_writer_put_jmp_reg (HooxX86Writer * self,
    HooxX86Reg reg);
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_jmp_reg_ptr (HooxX86Writer * self,
    HooxX86Reg reg);
/* hoox:test-only-end */
HOOX_API hx_boolean hoox_x86_writer_put_jmp_reg_offset_ptr (HooxX86Writer * self,
    HooxX86Reg reg, hx_ssize offset);
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_jmp_near_ptr (HooxX86Writer * self,
    HooxAddress address);
/* hoox:test-only-end */
HOOX_API hx_boolean hoox_x86_writer_put_jcc_short (HooxX86Writer * self,
    hx_x86_insn instruction_id, hx_constpointer target, HooxBranchHint hint);
HOOX_API hx_boolean hoox_x86_writer_put_jcc_near (HooxX86Writer * self,
    hx_x86_insn instruction_id, hx_constpointer target, HooxBranchHint hint);
HOOX_API void hoox_x86_writer_put_jcc_short_label (HooxX86Writer * self,
    hx_x86_insn instruction_id, hx_constpointer label_id, HooxBranchHint hint);
/* hoox:test-only-begin */
HOOX_API void hoox_x86_writer_put_jcc_near_label (HooxX86Writer * self,
    hx_x86_insn instruction_id, hx_constpointer label_id, HooxBranchHint hint);
/* hoox:test-only-end */

HOOX_API hx_boolean hoox_x86_writer_put_add_reg_imm (HooxX86Writer * self,
    HooxX86Reg reg, hx_ssize imm_value);
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_add_reg_reg (HooxX86Writer * self,
    HooxX86Reg dst_reg, HooxX86Reg src_reg);
/* hoox:test-only-end */
HOOX_API hx_boolean hoox_x86_writer_put_sub_reg_imm (HooxX86Writer * self,
    HooxX86Reg reg, hx_ssize imm_value);
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_inc_reg (HooxX86Writer * self,
    HooxX86Reg reg);
/* hoox:test-only-end */
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_dec_reg (HooxX86Writer * self,
    HooxX86Reg reg);
/* hoox:test-only-end */
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_lock_xadd_reg_ptr_reg (HooxX86Writer * self,
    HooxX86Reg dst_reg, HooxX86Reg src_reg);
/* hoox:test-only-end */
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_lock_inc_imm32_ptr (HooxX86Writer * self,
    hx_pointer target);
/* hoox:test-only-end */
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_lock_dec_imm32_ptr (HooxX86Writer * self,
    hx_pointer target);
/* hoox:test-only-end */

/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_and_reg_reg (HooxX86Writer * self,
    HooxX86Reg dst_reg, HooxX86Reg src_reg);
/* hoox:test-only-end */
HOOX_API hx_boolean hoox_x86_writer_put_and_reg_u32 (HooxX86Writer * self,
    HooxX86Reg reg, hx_uint32 imm_value);
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_shl_reg_u8 (HooxX86Writer * self,
    HooxX86Reg reg, hx_uint8 imm_value);
/* hoox:test-only-end */

HOOX_API hx_boolean hoox_x86_writer_put_mov_reg_reg (HooxX86Writer * self,
    HooxX86Reg dst_reg, HooxX86Reg src_reg);
HOOX_API hx_boolean hoox_x86_writer_put_mov_reg_u32 (HooxX86Writer * self,
    HooxX86Reg dst_reg, hx_uint32 imm_value);
HOOX_API hx_boolean hoox_x86_writer_put_mov_reg_u64 (HooxX86Writer * self,
    HooxX86Reg dst_reg, hx_uint64 imm_value);
HOOX_API void hoox_x86_writer_put_mov_reg_address (HooxX86Writer * self,
    HooxX86Reg dst_reg, HooxAddress address);
HOOX_API hx_boolean hoox_x86_writer_put_mov_reg_offset_ptr_u32 (HooxX86Writer * self,
    HooxX86Reg dst_reg, hx_ssize dst_offset, hx_uint32 imm_value);
HOOX_API void hoox_x86_writer_put_mov_reg_ptr_reg (HooxX86Writer * self,
    HooxX86Reg dst_reg, HooxX86Reg src_reg);
HOOX_API hx_boolean hoox_x86_writer_put_mov_reg_offset_ptr_reg (HooxX86Writer * self,
    HooxX86Reg dst_reg, hx_ssize dst_offset, HooxX86Reg src_reg);
HOOX_API void hoox_x86_writer_put_mov_reg_reg_ptr (HooxX86Writer * self,
    HooxX86Reg dst_reg, HooxX86Reg src_reg);
HOOX_API hx_boolean hoox_x86_writer_put_mov_reg_reg_offset_ptr (HooxX86Writer * self,
    HooxX86Reg dst_reg, HooxX86Reg src_reg, hx_ssize src_offset);

/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_mov_reg_near_ptr (HooxX86Writer * self,
    HooxX86Reg dst_reg, HooxAddress src_address);
/* hoox:test-only-end */
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_mov_near_ptr_reg (HooxX86Writer * self,
    HooxAddress dst_address, HooxX86Reg src_reg);
/* hoox:test-only-end */



HOOX_API hx_boolean hoox_x86_writer_put_lea_reg_reg_offset (HooxX86Writer * self,
    HooxX86Reg dst_reg, HooxX86Reg src_reg, hx_ssize src_offset);

HOOX_API hx_boolean hoox_x86_writer_put_xchg_reg_reg_ptr (HooxX86Writer * self,
    HooxX86Reg left_reg, HooxX86Reg right_reg);

HOOX_API void hoox_x86_writer_put_push_u32 (HooxX86Writer * self,
    hx_uint32 imm_value);
HOOX_API hx_boolean hoox_x86_writer_put_push_near_ptr (HooxX86Writer * self,
    HooxAddress address);
HOOX_API hx_boolean hoox_x86_writer_put_push_reg (HooxX86Writer * self,
    HooxX86Reg reg);
HOOX_API hx_boolean hoox_x86_writer_put_pop_reg (HooxX86Writer * self,
    HooxX86Reg reg);
HOOX_API void hoox_x86_writer_put_pushax (HooxX86Writer * self);
HOOX_API void hoox_x86_writer_put_popax (HooxX86Writer * self);
HOOX_API void hoox_x86_writer_put_pushfx (HooxX86Writer * self);
HOOX_API void hoox_x86_writer_put_popfx (HooxX86Writer * self);

HOOX_API hx_boolean hoox_x86_writer_put_test_reg_reg (HooxX86Writer * self,
    HooxX86Reg reg_a, HooxX86Reg reg_b);
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_cmp_reg_i32 (HooxX86Writer * self,
    HooxX86Reg reg, hx_int32 imm_value);
/* hoox:test-only-end */
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_cmp_reg_offset_ptr_reg (HooxX86Writer * self,
    HooxX86Reg reg_a, hx_ssize offset, HooxX86Reg reg_b);
/* hoox:test-only-end */
HOOX_API void hoox_x86_writer_put_cld (HooxX86Writer * self);

/* hoox:test-only-begin */
HOOX_API void hoox_x86_writer_put_nop (HooxX86Writer * self);
/* hoox:test-only-end */
/* hoox:test-only-begin */
HOOX_API void hoox_x86_writer_put_breakpoint (HooxX86Writer * self);
/* hoox:test-only-end */
HOOX_API void hoox_x86_writer_put_nop_padding (HooxX86Writer * self, hx_uint n);

/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_fxsave_reg_ptr (HooxX86Writer * self,
    HooxX86Reg reg);
/* hoox:test-only-end */
/* hoox:test-only-begin */
HOOX_API hx_boolean hoox_x86_writer_put_fxrstor_reg_ptr (HooxX86Writer * self,
    HooxX86Reg reg);
/* hoox:test-only-end */

HOOX_API void hoox_x86_writer_put_u8 (HooxX86Writer * self, hx_uint8 value);
HOOX_API void hoox_x86_writer_put_s8 (HooxX86Writer * self, hx_int8 value);
HOOX_API void hoox_x86_writer_put_bytes (HooxX86Writer * self, const hx_uint8 * data,
    hx_uint n);

HX_END_DECLS

#endif
