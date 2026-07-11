/*
 * Copyright (C) 2014-2023 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2017 Antonio Ken Iannillo <ak.iannillo@gmail.com>
 * Copyright (C) 2019 Jon Wilson <jonwilson@zepler.net>
 * Copyright (C) 2023 Håvard Sørbø <havard@hsorbo.no>
 * Copyright (C) 2023 Fabian Freyer <fabian.freyer@physik.tu-berlin.de>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxarm64writer.h"

#include "hooxlibc.h"
#include "hooxmemory.h"
#include "hooxprocess.h"

#ifdef _MSC_VER
# include <intrin.h>
#endif

typedef hx_uint HooxArm64LabelRefType;
typedef struct _HooxArm64LabelRef HooxArm64LabelRef;
typedef struct _HooxArm64LiteralRef HooxArm64LiteralRef;
typedef hx_uint HooxArm64LiteralWidth;
typedef hx_uint HooxArm64MemOperationType;
typedef hx_uint HooxArm64MemOperandType;
typedef hx_uint HooxArm64MetaReg;
typedef struct _HooxArm64RegInfo HooxArm64RegInfo;

enum _HooxArm64LabelRefType
{
  HOOX_ARM64_B,
  HOOX_ARM64_B_COND,
  HOOX_ARM64_BL,
  HOOX_ARM64_CBZ,
  HOOX_ARM64_CBNZ,
  HOOX_ARM64_TBZ,
  HOOX_ARM64_TBNZ,
};

struct _HooxArm64LabelRef
{
  hx_constpointer id;
  HooxArm64LabelRefType type;
  hx_uint32 * insn;
};

struct _HooxArm64LiteralRef
{
  hx_uint32 * insn;
  hx_int64 val;
  HooxArm64LiteralWidth width;
};

enum _HooxArm64LiteralWidth
{
  HOOX_LITERAL_32BIT,
  HOOX_LITERAL_64BIT
};

enum _HooxArm64MemOperationType
{
  HOOX_MEM_OPERATION_STORE = 0,
  HOOX_MEM_OPERATION_LOAD = 1
};

enum _HooxArm64MemOperandType
{
  HOOX_MEM_OPERAND_I32,
  HOOX_MEM_OPERAND_I64,
  HOOX_MEM_OPERAND_S32,
  HOOX_MEM_OPERAND_D64,
  HOOX_MEM_OPERAND_Q128
};

enum _HooxArm64MetaReg
{
  HOOX_MREG_R0,
  HOOX_MREG_R1,
  HOOX_MREG_R2,
  HOOX_MREG_R3,
  HOOX_MREG_R4,
  HOOX_MREG_R5,
  HOOX_MREG_R6,
  HOOX_MREG_R7,
  HOOX_MREG_R8,
  HOOX_MREG_R9,
  HOOX_MREG_R10,
  HOOX_MREG_R11,
  HOOX_MREG_R12,
  HOOX_MREG_R13,
  HOOX_MREG_R14,
  HOOX_MREG_R15,
  HOOX_MREG_R16,
  HOOX_MREG_R17,
  HOOX_MREG_R18,
  HOOX_MREG_R19,
  HOOX_MREG_R20,
  HOOX_MREG_R21,
  HOOX_MREG_R22,
  HOOX_MREG_R23,
  HOOX_MREG_R24,
  HOOX_MREG_R25,
  HOOX_MREG_R26,
  HOOX_MREG_R27,
  HOOX_MREG_R28,
  HOOX_MREG_R29,
  HOOX_MREG_R30,
  HOOX_MREG_R31,

  HOOX_MREG_FP = HOOX_MREG_R29,
  HOOX_MREG_LR = HOOX_MREG_R30,
  HOOX_MREG_SP = HOOX_MREG_R31,
  HOOX_MREG_ZR = HOOX_MREG_R31
};

struct _HooxArm64RegInfo
{
  HooxArm64MetaReg meta;
  hx_boolean is_integer;
  hx_uint width;
  hx_uint index;
  hx_uint32 sf;
  HooxArm64MemOperandType operand_type;
};

static void hoox_arm64_writer_reset_refs (HooxArm64Writer * self);

static void hoox_arm64_writer_put_argument_list_setup (HooxArm64Writer * self,
    hx_uint n_args, const HooxArgument * args);
static void hoox_arm64_writer_put_argument_list_setup_va (HooxArm64Writer * self,
    hx_uint n_args, va_list args);
static void hoox_arm64_writer_put_argument_list_teardown (HooxArm64Writer * self,
    hx_uint n_args);
static hx_boolean hoox_arm64_writer_put_br_reg_with_extra (HooxArm64Writer * self,
    hx_arm64_reg reg, hx_uint32 extra);
static hx_boolean hoox_arm64_writer_put_blr_reg_with_extra (HooxArm64Writer * self,
    hx_arm64_reg reg, hx_uint32 extra);
static hx_boolean hoox_arm64_writer_put_cbx_op_reg_imm (HooxArm64Writer * self,
    hx_uint8 op, hx_arm64_reg reg, HooxAddress target);
static hx_boolean hoox_arm64_writer_put_tbx_op_reg_imm_imm (HooxArm64Writer * self,
    hx_uint8 op, hx_arm64_reg reg, hx_uint bit, HooxAddress target);
static hx_boolean hoox_arm64_writer_put_ldr_reg_pcrel (HooxArm64Writer * self,
    const HooxArm64RegInfo * ri, HooxAddress src_address);
static void hoox_arm64_writer_put_load_store_pair (HooxArm64Writer * self,
    HooxArm64MemOperationType operation_type,
    HooxArm64MemOperandType operand_type, hx_uint rt, hx_uint rt2, hx_uint rn,
    hx_ssize rn_offset, HooxArm64IndexMode mode);

static HooxAddress hoox_arm64_writer_strip (HooxArm64Writer * self,
    HooxAddress value);

static hx_boolean hoox_arm64_writer_try_commit_label_refs (HooxArm64Writer * self);
static void hoox_arm64_writer_maybe_commit_literals (HooxArm64Writer * self);
static void hoox_arm64_writer_commit_literals (HooxArm64Writer * self);

static void hoox_arm64_writer_describe_reg (HooxArm64Writer * self,
    hx_arm64_reg reg, HooxArm64RegInfo * ri);

static HooxArm64MemOperandType hoox_arm64_mem_operand_type_from_reg_info (
    const HooxArm64RegInfo * ri);

static hx_boolean hoox_arm64_try_encode_logical_immediate (hx_uint64 imm_value,
    hx_uint reg_width, hx_uint * imm_enc);
static hx_uint hoox_arm64_determine_logical_element_size (hx_uint64 imm_value,
    hx_uint reg_width);
static hx_boolean hoox_arm64_try_determine_logical_rotation (hx_uint64 imm_value,
    hx_uint element_size, hx_uint * num_rotations, hx_uint * num_trailing_ones);

static hx_boolean hoox_is_shifted_mask_64 (hx_uint64 value);
static hx_boolean hoox_is_mask_64 (hx_uint64 value);

static hx_uint hoox_count_leading_zeros (hx_uint64 value);
static hx_uint hoox_count_trailing_zeros (hx_uint64 value);
static hx_uint hoox_count_leading_ones (hx_uint64 value);
static hx_uint hoox_count_trailing_ones (hx_uint64 value);

HooxArm64Writer *
hoox_arm64_writer_new (hx_pointer code_address)
{
  HooxArm64Writer * writer;

  writer = hx_slice_new (HooxArm64Writer);

  hoox_arm64_writer_init (writer, code_address);

  return writer;
}

HooxArm64Writer *
hoox_arm64_writer_ref (HooxArm64Writer * writer)
{
  hx_atomic_int_inc (&writer->ref_count);

  return writer;
}

void
hoox_arm64_writer_unref (HooxArm64Writer * writer)
{
  if (hx_atomic_int_dec_and_test (&writer->ref_count))
  {
    hoox_arm64_writer_clear (writer);

    hx_slice_free (HooxArm64Writer, writer);
  }
}

void
hoox_arm64_writer_init (HooxArm64Writer * writer,
                       hx_pointer code_address)
{
  writer->ref_count = 1;
  writer->flush_on_destroy = TRUE;

  writer->data_endian = HX_BYTE_ORDER;
  writer->target_os = hoox_process_get_native_os ();
  writer->ptrauth_support = hoox_query_ptrauth_support ();
  writer->sign = hoox_sign_code_address;

  writer->label_defs = NULL;
  writer->label_refs.data = NULL;
  writer->literal_refs.data = NULL;

  hoox_arm64_writer_reset (writer, code_address);
}

static hx_boolean
hoox_arm64_writer_has_label_defs (HooxArm64Writer * self)
{
  return self->label_defs != NULL;
}

static hx_boolean
hoox_arm64_writer_has_label_refs (HooxArm64Writer * self)
{
  return self->label_refs.data != NULL;
}

static hx_boolean
hoox_arm64_writer_has_literal_refs (HooxArm64Writer * self)
{
  return self->literal_refs.data != NULL;
}

void
hoox_arm64_writer_clear (HooxArm64Writer * writer)
{
  if (writer->flush_on_destroy)
    hoox_arm64_writer_flush (writer);

  if (hoox_arm64_writer_has_label_defs (writer))
    hoox_metal_hash_table_unref (writer->label_defs);

  if (hoox_arm64_writer_has_label_refs (writer))
    hoox_metal_array_free (&writer->label_refs);

  if (hoox_arm64_writer_has_literal_refs (writer))
    hoox_metal_array_free (&writer->literal_refs);
}

void
hoox_arm64_writer_reset (HooxArm64Writer * writer,
                        hx_pointer code_address)
{
  writer->base = code_address;
  writer->code = code_address;
  writer->pc = HOOX_ADDRESS (code_address);

  if (hoox_arm64_writer_has_label_defs (writer))
    hoox_metal_hash_table_remove_all (writer->label_defs);

  hoox_arm64_writer_reset_refs (writer);
}

static void
hoox_arm64_writer_reset_refs (HooxArm64Writer * self)
{
  if (hoox_arm64_writer_has_label_refs (self))
    hoox_metal_array_remove_all (&self->label_refs);

  if (hoox_arm64_writer_has_literal_refs (self))
    hoox_metal_array_remove_all (&self->literal_refs);

  self->earliest_literal_insn = NULL;
}

hx_pointer
hoox_arm64_writer_cur (HooxArm64Writer * self)
{
  return self->code;
}

hx_uint
hoox_arm64_writer_offset (HooxArm64Writer * self)
{
  return (hx_uint) (self->code - self->base) * sizeof (hx_uint32);
}

void
hoox_arm64_writer_skip (HooxArm64Writer * self,
                       hx_uint n_bytes)
{
  self->code = (hx_uint32 *) (((hx_uint8 *) self->code) + n_bytes);
  self->pc += n_bytes;
}

hx_boolean
hoox_arm64_writer_flush (HooxArm64Writer * self)
{
  if (!hoox_arm64_writer_try_commit_label_refs (self))
    goto error;

  hoox_arm64_writer_commit_literals (self);

  return TRUE;

error:
  {
    hoox_arm64_writer_reset_refs (self);

    return FALSE;
  }
}

hx_boolean
hoox_arm64_writer_put_label (HooxArm64Writer * self,
                            hx_constpointer id)
{
  if (!hoox_arm64_writer_has_label_defs (self))
    self->label_defs = hoox_metal_hash_table_new (NULL, NULL);

  if (hoox_metal_hash_table_lookup (self->label_defs, id) != NULL)
    return FALSE;

  hoox_metal_hash_table_insert (self->label_defs, (hx_pointer) id, self->code);

  return TRUE;
}

static void
hoox_arm64_writer_add_label_reference_here (HooxArm64Writer * self,
                                           hx_constpointer id,
                                           HooxArm64LabelRefType type)
{
  HooxArm64LabelRef * r;

  if (!hoox_arm64_writer_has_label_refs (self))
    hoox_metal_array_init (&self->label_refs, sizeof (HooxArm64LabelRef));

  r = hoox_metal_array_append (&self->label_refs);
  r->id = id;
  r->type = type;
  r->insn = self->code;
}

static void
hoox_arm64_writer_add_literal_reference_here (HooxArm64Writer * self,
                                             hx_uint64 val,
                                             HooxArm64LiteralWidth width)
{
  HooxArm64LiteralRef * r;

  if (!hoox_arm64_writer_has_literal_refs (self))
    hoox_metal_array_init (&self->literal_refs, sizeof (HooxArm64LiteralRef));

  r = hoox_metal_array_append (&self->literal_refs);
  r->insn = self->code;
  r->val = val;
  r->width = width;

  if (self->earliest_literal_insn == NULL)
    self->earliest_literal_insn = r->insn;
}

void
hoox_arm64_writer_put_call_address_with_arguments (HooxArm64Writer * self,
                                                  HooxAddress func,
                                                  hx_uint n_args,
                                                  ...)
{
  va_list args;

  va_start (args, n_args);
  hoox_arm64_writer_put_argument_list_setup_va (self, n_args, args);
  va_end (args);

  if (hoox_arm64_writer_can_branch_directly_between (self, self->pc, func))
  {
    hoox_arm64_writer_put_bl_imm (self, func);
  }
  else
  {
    const hx_arm64_reg target = HX_ARM64_REG_X0 + n_args;
    hoox_arm64_writer_put_ldr_reg_address (self, target, func);
    hoox_arm64_writer_put_blr_reg (self, target);
  }

  hoox_arm64_writer_put_argument_list_teardown (self, n_args);
}

void
hoox_arm64_writer_put_call_address_with_arguments_array (
    HooxArm64Writer * self,
    HooxAddress func,
    hx_uint n_args,
    const HooxArgument * args)
{
  hoox_arm64_writer_put_argument_list_setup (self, n_args, args);

  if (hoox_arm64_writer_can_branch_directly_between (self, self->pc, func))
  {
    hoox_arm64_writer_put_bl_imm (self, func);
  }
  else
  {
    const hx_arm64_reg target = HX_ARM64_REG_X0 + n_args;
    hoox_arm64_writer_put_ldr_reg_address (self, target, func);
    hoox_arm64_writer_put_blr_reg (self, target);
  }

  hoox_arm64_writer_put_argument_list_teardown (self, n_args);
}

void
hoox_arm64_writer_put_call_reg_with_arguments (HooxArm64Writer * self,
                                              hx_arm64_reg reg,
                                              hx_uint n_args,
                                              ...)
{
  va_list args;

  va_start (args, n_args);
  hoox_arm64_writer_put_argument_list_setup_va (self, n_args, args);
  va_end (args);

  hoox_arm64_writer_put_blr_reg (self, reg);

  hoox_arm64_writer_put_argument_list_teardown (self, n_args);
}

void
hoox_arm64_writer_put_call_reg_with_arguments_array (HooxArm64Writer * self,
                                                    hx_arm64_reg reg,
                                                    hx_uint n_args,
                                                    const HooxArgument * args)
{
  hoox_arm64_writer_put_argument_list_setup (self, n_args, args);

  hoox_arm64_writer_put_blr_reg (self, reg);

  hoox_arm64_writer_put_argument_list_teardown (self, n_args);
}

static void
hoox_arm64_writer_put_argument_list_setup (HooxArm64Writer * self,
                                          hx_uint n_args,
                                          const HooxArgument * args)
{
  hx_int arg_index;

  for (arg_index = (hx_int) n_args - 1; arg_index >= 0; arg_index--)
  {
    const HooxArgument * arg = &args[arg_index];
    hx_arm64_reg dst_reg = HX_ARM64_REG_X0 + arg_index;

    if (arg->type == HOOX_ARG_ADDRESS)
    {
      hoox_arm64_writer_put_ldr_reg_address (self, dst_reg, arg->value.address);
    }
    else
    {
      hx_arm64_reg src_reg = arg->value.reg;
      HooxArm64RegInfo rs;

      hoox_arm64_writer_describe_reg (self, src_reg, &rs);

      if (rs.width == 64)
      {
        if (src_reg != dst_reg)
          hoox_arm64_writer_put_mov_reg_reg (self, dst_reg, arg->value.reg);
      }
      else
      {
        hoox_arm64_writer_put_uxtw_reg_reg (self, dst_reg, src_reg);
      }
    }
  }
}

static void
hoox_arm64_writer_put_argument_list_setup_va (HooxArm64Writer * self,
                                             hx_uint n_args,
                                             va_list args)
{
  HooxArgument * arg_values;
  hx_uint arg_index;

  arg_values = hx_newa (HooxArgument, n_args);

  for (arg_index = 0; arg_index != n_args; arg_index++)
  {
    HooxArgument * arg = &arg_values[arg_index];

    arg->type = va_arg (args, HooxArgType);
    if (arg->type == HOOX_ARG_ADDRESS)
      arg->value.address = va_arg (args, HooxAddress);
    else if (arg->type == HOOX_ARG_REGISTER)
      arg->value.reg = va_arg (args, hx_arm64_reg);
    else
      hx_assert_not_reached ();
  }

  hoox_arm64_writer_put_argument_list_setup (self, n_args, arg_values);
}

static void
hoox_arm64_writer_put_argument_list_teardown (HooxArm64Writer * self,
                                             hx_uint n_args)
{
}

void
hoox_arm64_writer_put_branch_address (HooxArm64Writer * self,
                                     HooxAddress address)
{
  if (!hoox_arm64_writer_can_branch_directly_between (self, self->pc, address))
  {
    const hx_arm64_reg target = HX_ARM64_REG_X16;

    hoox_arm64_writer_put_ldr_reg_address (self, target, address);
    hoox_arm64_writer_put_br_reg (self, target);

    return;
  }

  hoox_arm64_writer_put_b_imm (self, address);
}

hx_boolean
hoox_arm64_writer_can_branch_directly_between (HooxArm64Writer * self,
                                              HooxAddress from,
                                              HooxAddress to)
{
  hx_int64 distance = (hx_int64) hoox_arm64_writer_strip (self, to) -
      (hx_int64) hoox_arm64_writer_strip (self, from);

  return HOOX_IS_WITHIN_INT28_RANGE (distance);
}

hx_boolean
hoox_arm64_writer_put_b_imm (HooxArm64Writer * self,
                            HooxAddress address)
{
  hx_int64 distance =
      (hx_int64) hoox_arm64_writer_strip (self, address) - (hx_int64) self->pc;

  if (!HOOX_IS_WITHIN_INT28_RANGE (distance) || distance % 4 != 0)
    return FALSE;

  hoox_arm64_writer_put_instruction (self,
      0x14000000 | ((distance / 4) & HOOX_INT26_MASK));

  return TRUE;
}

void
hoox_arm64_writer_put_b_label (HooxArm64Writer * self,
                              hx_constpointer label_id)
{
  hoox_arm64_writer_add_label_reference_here (self, label_id, HOOX_ARM64_B);
  hoox_arm64_writer_put_instruction (self, 0x14000000);
}

void
hoox_arm64_writer_put_b_cond_label (HooxArm64Writer * self,
                                   hx_arm64_cc cc,
                                   hx_constpointer label_id)
{
  hoox_arm64_writer_add_label_reference_here (self, label_id, HOOX_ARM64_B_COND);
  hoox_arm64_writer_put_instruction (self, 0x54000000 | (cc - 1));
}

hx_boolean
hoox_arm64_writer_put_bl_imm (HooxArm64Writer * self,
                             HooxAddress address)
{
  hx_int64 distance =
      (hx_int64) hoox_arm64_writer_strip (self, address) - (hx_int64) self->pc;

  if (!HOOX_IS_WITHIN_INT28_RANGE (distance) || distance % 4 != 0)
    return FALSE;

  hoox_arm64_writer_put_instruction (self,
      0x94000000 | ((distance / 4) & HOOX_INT26_MASK));

  return TRUE;
}

void
hoox_arm64_writer_put_bl_label (HooxArm64Writer * self,
                               hx_constpointer label_id)
{
  hoox_arm64_writer_add_label_reference_here (self, label_id, HOOX_ARM64_BL);
  hoox_arm64_writer_put_instruction (self, 0x94000000);
}

hx_boolean
hoox_arm64_writer_put_br_reg (HooxArm64Writer * self,
                             hx_arm64_reg reg)
{
  return hoox_arm64_writer_put_br_reg_with_extra (self, reg,
      (self->ptrauth_support == HOOX_PTRAUTH_SUPPORTED) ? 0x81f : 0);
}

hx_boolean
hoox_arm64_writer_put_br_reg_no_auth (HooxArm64Writer * self,
                                     hx_arm64_reg reg)
{
  return hoox_arm64_writer_put_br_reg_with_extra (self, reg, 0);
}

static hx_boolean
hoox_arm64_writer_put_br_reg_with_extra (HooxArm64Writer * self,
                                        hx_arm64_reg reg,
                                        hx_uint32 extra)
{
  HooxArm64RegInfo ri;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (ri.width != 64)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, 0xd61f0000 | (ri.index << 5) | extra);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_blr_reg (HooxArm64Writer * self,
                              hx_arm64_reg reg)
{
  return hoox_arm64_writer_put_blr_reg_with_extra (self, reg,
      (self->ptrauth_support == HOOX_PTRAUTH_SUPPORTED) ? 0x81f : 0);
}

hx_boolean
hoox_arm64_writer_put_blr_reg_no_auth (HooxArm64Writer * self,
                                      hx_arm64_reg reg)
{
  return hoox_arm64_writer_put_blr_reg_with_extra (self, reg, 0);
}

static hx_boolean
hoox_arm64_writer_put_blr_reg_with_extra (HooxArm64Writer * self,
                                         hx_arm64_reg reg,
                                         hx_uint32 extra)
{
  HooxArm64RegInfo ri;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (ri.width != 64)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, 0xd63f0000 | (ri.index << 5) | extra);

  return TRUE;
}

void
hoox_arm64_writer_put_ret (HooxArm64Writer * self)
{
  hoox_arm64_writer_put_instruction (self, 0xd65f0000 | (HOOX_MREG_LR << 5));
}

hx_boolean
hoox_arm64_writer_put_ret_reg (HooxArm64Writer * self,
                              hx_arm64_reg reg)
{
  HooxArm64RegInfo ri;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (ri.width != 64)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, 0xd65f0000 | (ri.index << 5));

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_cbz_reg_imm (HooxArm64Writer * self,
                                  hx_arm64_reg reg,
                                  HooxAddress target)
{
  return hoox_arm64_writer_put_cbx_op_reg_imm (self, 0, reg, target);
}

hx_boolean
hoox_arm64_writer_put_cbnz_reg_imm (HooxArm64Writer * self,
                                   hx_arm64_reg reg,
                                   HooxAddress target)
{
  return hoox_arm64_writer_put_cbx_op_reg_imm (self, 1, reg, target);
}

void
hoox_arm64_writer_put_cbz_reg_label (HooxArm64Writer * self,
                                    hx_arm64_reg reg,
                                    hx_constpointer label_id)
{
  hoox_arm64_writer_add_label_reference_here (self, label_id, HOOX_ARM64_CBZ);
  hoox_arm64_writer_put_cbx_op_reg_imm (self, 0, reg, 0);
}

void
hoox_arm64_writer_put_cbnz_reg_label (HooxArm64Writer * self,
                                     hx_arm64_reg reg,
                                     hx_constpointer label_id)
{
  hoox_arm64_writer_add_label_reference_here (self, label_id, HOOX_ARM64_CBNZ);
  hoox_arm64_writer_put_cbx_op_reg_imm (self, 1, reg, 0);
}

static hx_boolean
hoox_arm64_writer_put_cbx_op_reg_imm (HooxArm64Writer * self,
                                     hx_uint8 op,
                                     hx_arm64_reg reg,
                                     HooxAddress target)
{
  HooxArm64RegInfo ri;
  hx_int64 imm19;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (target != 0)
  {
    const hx_int64 distance = (hx_int64) target - (hx_int64) self->pc;
    imm19 = distance / 4;
    if (distance % 4 != 0 || !HOOX_IS_WITHIN_INT19_RANGE (imm19))
      return FALSE;
  }
  else
  {
    imm19 = 0;
  }

  hoox_arm64_writer_put_instruction (self,
      ri.sf |
      0x34000000 |
      (hx_uint32) op << 24 |
      (imm19 & HOOX_INT19_MASK) << 5 |
      ri.index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_tbz_reg_imm_imm (HooxArm64Writer * self,
                                      hx_arm64_reg reg,
                                      hx_uint bit,
                                      HooxAddress target)
{
  return hoox_arm64_writer_put_tbx_op_reg_imm_imm (self, 0, reg, bit, target);
}

hx_boolean
hoox_arm64_writer_put_tbnz_reg_imm_imm (HooxArm64Writer * self,
                                       hx_arm64_reg reg,
                                       hx_uint bit,
                                       HooxAddress target)
{
  return hoox_arm64_writer_put_tbx_op_reg_imm_imm (self, 1, reg, bit, target);
}

void
hoox_arm64_writer_put_tbz_reg_imm_label (HooxArm64Writer * self,
                                        hx_arm64_reg reg,
                                        hx_uint bit,
                                        hx_constpointer label_id)
{
  hoox_arm64_writer_add_label_reference_here (self, label_id, HOOX_ARM64_TBZ);
  hoox_arm64_writer_put_tbx_op_reg_imm_imm (self, 0, reg, bit, 0);
}

void
hoox_arm64_writer_put_tbnz_reg_imm_label (HooxArm64Writer * self,
                                         hx_arm64_reg reg,
                                         hx_uint bit,
                                         hx_constpointer label_id)
{
  hoox_arm64_writer_add_label_reference_here (self, label_id, HOOX_ARM64_TBNZ);
  hoox_arm64_writer_put_tbx_op_reg_imm_imm (self, 1, reg, bit, 0);
}

static hx_boolean
hoox_arm64_writer_put_tbx_op_reg_imm_imm (HooxArm64Writer * self,
                                         hx_uint8 op,
                                         hx_arm64_reg reg,
                                         hx_uint bit,
                                         HooxAddress target)
{
  HooxArm64RegInfo ri;
  hx_int64 imm14;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (bit >= ri.width)
    return FALSE;

  if (target != 0)
  {
    const hx_int64 distance = (hx_int64) target - (hx_int64) self->pc;
    imm14 = distance / 4;
    if (distance % 4 != 0 || !HOOX_IS_WITHIN_INT14_RANGE (imm14))
      return FALSE;
  }
  else
  {
    imm14 = 0;
  }

  hoox_arm64_writer_put_instruction (self,
      ((bit >> 5) << 31) |
      0x36000000 |
      (hx_uint32) op << 24 |
      ((bit & HOOX_INT5_MASK) << 19) |
      (imm14 & HOOX_INT14_MASK) << 5 |
      ri.index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_push_reg_reg (HooxArm64Writer * self,
                                   hx_arm64_reg reg_a,
                                   hx_arm64_reg reg_b)
{
  HooxArm64RegInfo ra, rb, sp;

  hoox_arm64_writer_describe_reg (self, reg_a, &ra);
  hoox_arm64_writer_describe_reg (self, reg_b, &rb);
  hoox_arm64_writer_describe_reg (self, HX_ARM64_REG_SP, &sp);

  if (ra.width != rb.width)
    return FALSE;

  hoox_arm64_writer_put_load_store_pair (self, HOOX_MEM_OPERATION_STORE,
      hoox_arm64_mem_operand_type_from_reg_info (&ra), ra.index, rb.index,
      sp.index, -(2 * ((hx_int) ra.width / 8)), HOOX_INDEX_PRE_ADJUST);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_pop_reg_reg (HooxArm64Writer * self,
                                  hx_arm64_reg reg_a,
                                  hx_arm64_reg reg_b)
{
  HooxArm64RegInfo ra, rb, sp;

  hoox_arm64_writer_describe_reg (self, reg_a, &ra);
  hoox_arm64_writer_describe_reg (self, reg_b, &rb);
  hoox_arm64_writer_describe_reg (self, HX_ARM64_REG_SP, &sp);

  if (ra.width != rb.width)
    return FALSE;

  hoox_arm64_writer_put_load_store_pair (self, HOOX_MEM_OPERATION_LOAD,
      hoox_arm64_mem_operand_type_from_reg_info (&ra), ra.index, rb.index,
      sp.index, 2 * (ra.width / 8), HOOX_INDEX_POST_ADJUST);

  return TRUE;
}

void
hoox_arm64_writer_put_push_all_x_registers (HooxArm64Writer * self)
{
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X0, HX_ARM64_REG_X1);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X2, HX_ARM64_REG_X3);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X4, HX_ARM64_REG_X5);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X6, HX_ARM64_REG_X7);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X8, HX_ARM64_REG_X9);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X10, HX_ARM64_REG_X11);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X12, HX_ARM64_REG_X13);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X14, HX_ARM64_REG_X15);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X16, HX_ARM64_REG_X17);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X18, HX_ARM64_REG_X19);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X20, HX_ARM64_REG_X21);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X22, HX_ARM64_REG_X23);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X24, HX_ARM64_REG_X25);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X26, HX_ARM64_REG_X27);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X28, HX_ARM64_REG_X29);
  hoox_arm64_writer_put_mov_reg_nzcv (self, HX_ARM64_REG_X15);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_X30, HX_ARM64_REG_X15);
}

void
hoox_arm64_writer_put_pop_all_x_registers (HooxArm64Writer * self)
{
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X30, HX_ARM64_REG_X15);
  hoox_arm64_writer_put_mov_nzcv_reg (self, HX_ARM64_REG_X15);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X28, HX_ARM64_REG_X29);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X26, HX_ARM64_REG_X27);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X24, HX_ARM64_REG_X25);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X22, HX_ARM64_REG_X23);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X20, HX_ARM64_REG_X21);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X18, HX_ARM64_REG_X19);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X16, HX_ARM64_REG_X17);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X14, HX_ARM64_REG_X15);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X12, HX_ARM64_REG_X13);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X10, HX_ARM64_REG_X11);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X8, HX_ARM64_REG_X9);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X6, HX_ARM64_REG_X7);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X4, HX_ARM64_REG_X5);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X2, HX_ARM64_REG_X3);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_X0, HX_ARM64_REG_X1);
}

void
hoox_arm64_writer_put_push_all_q_registers (HooxArm64Writer * self)
{
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q0, HX_ARM64_REG_Q1);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q2, HX_ARM64_REG_Q3);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q4, HX_ARM64_REG_Q5);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q6, HX_ARM64_REG_Q7);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q8, HX_ARM64_REG_Q9);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q10, HX_ARM64_REG_Q11);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q12, HX_ARM64_REG_Q13);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q14, HX_ARM64_REG_Q15);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q16, HX_ARM64_REG_Q17);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q18, HX_ARM64_REG_Q19);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q20, HX_ARM64_REG_Q21);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q22, HX_ARM64_REG_Q23);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q24, HX_ARM64_REG_Q25);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q26, HX_ARM64_REG_Q27);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q28, HX_ARM64_REG_Q29);
  hoox_arm64_writer_put_push_reg_reg (self, HX_ARM64_REG_Q30, HX_ARM64_REG_Q31);
}

void
hoox_arm64_writer_put_pop_all_q_registers (HooxArm64Writer * self)
{
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q30, HX_ARM64_REG_Q31);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q28, HX_ARM64_REG_Q29);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q26, HX_ARM64_REG_Q27);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q24, HX_ARM64_REG_Q25);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q22, HX_ARM64_REG_Q23);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q20, HX_ARM64_REG_Q21);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q18, HX_ARM64_REG_Q19);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q16, HX_ARM64_REG_Q17);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q14, HX_ARM64_REG_Q15);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q12, HX_ARM64_REG_Q13);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q10, HX_ARM64_REG_Q11);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q8, HX_ARM64_REG_Q9);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q6, HX_ARM64_REG_Q7);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q4, HX_ARM64_REG_Q5);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q2, HX_ARM64_REG_Q3);
  hoox_arm64_writer_put_pop_reg_reg (self, HX_ARM64_REG_Q0, HX_ARM64_REG_Q1);
}

hx_boolean
hoox_arm64_writer_put_ldr_reg_address (HooxArm64Writer * self,
                                      hx_arm64_reg reg,
                                      HooxAddress address)
{
  return hoox_arm64_writer_put_ldr_reg_u64 (self, reg, (hx_uint64) address);
}

hx_boolean
hoox_arm64_writer_put_ldr_reg_u32 (HooxArm64Writer * self,
                                  hx_arm64_reg reg,
                                  hx_uint32 val)
{
  HooxArm64RegInfo ri;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (ri.is_integer && val == 0)
    return hoox_arm64_writer_put_mov_reg_reg (self, reg, HX_ARM64_REG_WZR);

  if (ri.width != 32)
    return FALSE;

  hoox_arm64_writer_add_literal_reference_here (self, val, HOOX_LITERAL_32BIT);
  hoox_arm64_writer_put_ldr_reg_pcrel (self, &ri, 0);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_ldr_reg_u64 (HooxArm64Writer * self,
                                  hx_arm64_reg reg,
                                  hx_uint64 val)
{
  HooxArm64RegInfo ri;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (ri.is_integer && val == 0)
    return hoox_arm64_writer_put_mov_reg_reg (self, reg, HX_ARM64_REG_XZR);

  if (ri.width != 64)
    return FALSE;

  hoox_arm64_writer_add_literal_reference_here (self, val, HOOX_LITERAL_64BIT);
  hoox_arm64_writer_put_ldr_reg_pcrel (self, &ri, 0);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_ldr_reg_u32_ptr (HooxArm64Writer * self,
                                      hx_arm64_reg reg,
                                      HooxAddress src_address)
{
  HooxArm64RegInfo ri;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (ri.width != 32)
    return FALSE;

  return hoox_arm64_writer_put_ldr_reg_pcrel (self, &ri, src_address);
}

hx_boolean
hoox_arm64_writer_put_ldr_reg_u64_ptr (HooxArm64Writer * self,
                                      hx_arm64_reg reg,
                                      HooxAddress src_address)
{
  HooxArm64RegInfo ri;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (ri.width != 64)
    return FALSE;

  return hoox_arm64_writer_put_ldr_reg_pcrel (self, &ri, src_address);
}

hx_uint
hoox_arm64_writer_put_ldr_reg_ref (HooxArm64Writer * self,
                                  hx_arm64_reg reg)
{
  HooxArm64RegInfo ri;
  hx_uint ref;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  ref = hoox_arm64_writer_offset (self);

  hoox_arm64_writer_put_ldr_reg_pcrel (self, &ri, 0);

  return ref;
}

void
hoox_arm64_writer_put_ldr_reg_value (HooxArm64Writer * self,
                                    hx_uint ref,
                                    HooxAddress value)
{
  hx_uint distance;
  hx_uint32 * insn;

  distance = hoox_arm64_writer_offset (self) - ref;

  insn = self->base + (ref / 4);
  *insn = HX_UINT32_TO_LE (HX_UINT32_FROM_LE (*insn) |
      (((distance / 4) & HOOX_INT19_MASK) << 5));

  *((hx_uint64 *) self->code) = HX_UINT64_TO_LE (value);
  self->code += 2;
  self->pc += 8;
}

static hx_boolean
hoox_arm64_writer_put_ldr_reg_pcrel (HooxArm64Writer * self,
                                    const HooxArm64RegInfo * ri,
                                    HooxAddress src_address)
{
  hx_int64 imm19;

  if (src_address != 0)
  {
    const hx_int64 distance = (hx_int64) src_address - (hx_int64) self->pc;
    imm19 = distance / 4;
    if (distance % 4 != 0 || !HOOX_IS_WITHIN_INT19_RANGE (imm19))
      return FALSE;
  }
  else
  {
    imm19 = 0;
  }

  hoox_arm64_writer_put_instruction (self,
      (ri->width == 64 ? 0x50000000 : 0x10000000) |
      (ri->is_integer  ? 0x08000000 : 0x0c000000) |
      (imm19 & HOOX_INT19_MASK) << 5 |
      ri->index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_ldr_reg_reg (HooxArm64Writer * self,
                                  hx_arm64_reg dst_reg,
                                  hx_arm64_reg src_reg)
{
  return hoox_arm64_writer_put_ldr_reg_reg_offset (self, dst_reg, src_reg, 0);
}

hx_boolean
hoox_arm64_writer_put_ldr_reg_reg_offset (HooxArm64Writer * self,
                                         hx_arm64_reg dst_reg,
                                         hx_arm64_reg src_reg,
                                         hx_size src_offset)
{
  HooxArm64RegInfo rd, rs;
  hx_uint32 size, v, opc;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, src_reg, &rs);

  opc = 1;
  if (rd.is_integer)
  {
    size = (rd.width == 64) ? 3 : 2;
    v = 0;
  }
  else
  {
    if (rd.width == 128)
    {
      size = 0;
      opc |= 2;
    }
    else
    {
      size = (rd.width == 64) ? 3 : 2;
    }
    v = 1;
  }

  if (rs.width != 64)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, 0x39000000 |
      (size << 30) | (v << 26) | (opc << 22) |
      ((hx_uint32) src_offset / (rd.width / 8)) << 10 |
      (rs.index << 5) | rd.index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_ldr_reg_reg_offset_mode (HooxArm64Writer * self,
                                              hx_arm64_reg dst_reg,
                                              hx_arm64_reg src_reg,
                                              hx_ssize src_offset,
                                              HooxArm64IndexMode mode)
{
  HooxArm64RegInfo rd, rs;
  hx_uint32 opc, size, v;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, src_reg, &rs);

  opc = 1;
  if (rd.is_integer)
  {
    size = (rd.width == 64) ? 3 : 2;
    v = 0;
  }
  else
  {
    if (rd.width == 128)
    {
      size = 0;
      opc |= 2;
    }
    else
    {
      size = (rd.width == 64) ? 3 : 2;
    }
    v = 1;
  }

  if (rs.width != 64)
    return FALSE;

  if (src_offset < -256 || src_offset > 255)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, 0x38000000 |
      (size << 30) | (v << 26) | (opc << 22) |
      (((hx_uint32) src_offset) & 0x1ff) << 12 |
      mode << 10 |
      (rs.index << 5) | rd.index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_ldrsw_reg_reg_offset (HooxArm64Writer * self,
                                           hx_arm64_reg dst_reg,
                                           hx_arm64_reg src_reg,
                                           hx_size src_offset)
{
  HooxArm64RegInfo rd, rs;
  hx_size immediate;
  hx_boolean immediate_fits_in_12_bits;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, src_reg, &rs);

  if (rd.width != 64 || rs.width != 64)
    return FALSE;
  if (!rd.is_integer || !rs.is_integer)
    return FALSE;

  immediate = src_offset / sizeof (hx_uint32);

  immediate_fits_in_12_bits = (immediate >> 12) == 0;
  if (!immediate_fits_in_12_bits)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, 0xb9800000 | (immediate << 10) |
      (rs.index << 5) | rd.index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_adrp_reg_address (HooxArm64Writer * self,
                                       hx_arm64_reg reg,
                                       HooxAddress address)
{
  HooxArm64RegInfo ri;
  union
  {
    hx_int64 i;
    hx_uint64 u;
  } distance;
  hx_uint32 imm_hi, imm_lo;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (ri.width != 64)
    return FALSE;

  distance.i = (hx_int64) hoox_arm64_writer_strip (self, address) -
      (hx_int64) (self->pc & ~((HooxAddress) (4096 - 1)));
  if (distance.i % 4096 != 0)
    return FALSE;
  distance.i /= 4096;

  if (!HOOX_IS_WITHIN_INT21_RANGE (distance.i))
    return FALSE;

  imm_hi = (distance.u & HX_UINT64_CONSTANT (0x1ffffc)) >> 2;
  imm_lo = (distance.u & HX_UINT64_CONSTANT (0x3));

  hoox_arm64_writer_put_instruction (self, 0x90000000 |
      (imm_lo << 29) | (imm_hi << 5) | ri.index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_str_reg_reg (HooxArm64Writer * self,
                                  hx_arm64_reg src_reg,
                                  hx_arm64_reg dst_reg)
{
  return hoox_arm64_writer_put_str_reg_reg_offset (self, src_reg, dst_reg, 0);
}

hx_boolean
hoox_arm64_writer_put_str_reg_reg_offset (HooxArm64Writer * self,
                                         hx_arm64_reg src_reg,
                                         hx_arm64_reg dst_reg,
                                         hx_size dst_offset)
{
  HooxArm64RegInfo rs, rd;
  hx_uint32 size, v, opc;

  hoox_arm64_writer_describe_reg (self, src_reg, &rs);
  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);

  opc = 0;
  if (rs.is_integer)
  {
    size = (rs.width == 64) ? 3 : 2;
    v = 0;
  }
  else
  {
    if (rs.width == 128)
    {
      size = 0;
      opc |= 2;
    }
    else
    {
      size = (rs.width == 64) ? 3 : 2;
    }
    v = 1;
  }

  if (rd.width != 64)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, 0x39000000 |
      (size << 30) | (v << 26) | (opc << 22) |
      ((hx_uint32) dst_offset / (rs.width / 8)) << 10 |
      (rd.index << 5) | rs.index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_str_reg_reg_offset_mode (HooxArm64Writer * self,
                                              hx_arm64_reg src_reg,
                                              hx_arm64_reg dst_reg,
                                              hx_ssize dst_offset,
                                              HooxArm64IndexMode mode)
{
  HooxArm64RegInfo rs, rd;
  hx_uint32 opc, size, v;

  hoox_arm64_writer_describe_reg (self, src_reg, &rs);
  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);

  opc = 0;
  if (rs.is_integer)
  {
    size = (rs.width == 64) ? 3 : 2;
    v = 0;
  }
  else
  {
    if (rs.width == 128)
    {
      size = 0;
      opc |= 2;
    }
    else
    {
      size = (rs.width == 64) ? 3 : 2;
    }
    v = 1;
  }

  if (rd.width != 64)
    return FALSE;

  if (dst_offset < -256 || dst_offset > 255)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, 0x38000000 |
      (size << 30) | (v << 26) | (opc << 22) |
      (((hx_uint32) dst_offset) & 0x1ff) << 12 |
      mode << 10 |
      (rd.index << 5) | rs.index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_ldp_reg_reg_reg_offset (HooxArm64Writer * self,
                                             hx_arm64_reg reg_a,
                                             hx_arm64_reg reg_b,
                                             hx_arm64_reg reg_src,
                                             hx_ssize src_offset,
                                             HooxArm64IndexMode mode)
{
  HooxArm64RegInfo ra, rb, rs;

  hoox_arm64_writer_describe_reg (self, reg_a, &ra);
  hoox_arm64_writer_describe_reg (self, reg_b, &rb);
  hoox_arm64_writer_describe_reg (self, reg_src, &rs);

  if (ra.width != rb.width)
    return FALSE;

  hoox_arm64_writer_put_load_store_pair (self, HOOX_MEM_OPERATION_LOAD,
      hoox_arm64_mem_operand_type_from_reg_info (&ra), ra.index, rb.index,
      rs.index, src_offset, mode);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_stp_reg_reg_reg_offset (HooxArm64Writer * self,
                                             hx_arm64_reg reg_a,
                                             hx_arm64_reg reg_b,
                                             hx_arm64_reg reg_dst,
                                             hx_ssize dst_offset,
                                             HooxArm64IndexMode mode)
{
  HooxArm64RegInfo ra, rb, rd;

  hoox_arm64_writer_describe_reg (self, reg_a, &ra);
  hoox_arm64_writer_describe_reg (self, reg_b, &rb);
  hoox_arm64_writer_describe_reg (self, reg_dst, &rd);

  if (ra.width != rb.width)
    return FALSE;

  hoox_arm64_writer_put_load_store_pair (self, HOOX_MEM_OPERATION_STORE,
      hoox_arm64_mem_operand_type_from_reg_info (&ra), ra.index, rb.index,
      rd.index, dst_offset, mode);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_mov_reg_reg (HooxArm64Writer * self,
                                  hx_arm64_reg dst_reg,
                                  hx_arm64_reg src_reg)
{
  HooxArm64RegInfo rd, rs;
  hx_boolean src_is_zero_reg;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, src_reg, &rs);

  if (rd.width != rs.width)
    return FALSE;

  src_is_zero_reg = src_reg == HX_ARM64_REG_XZR || src_reg == HX_ARM64_REG_WZR;

  if (rd.meta == HOOX_MREG_SP || (!src_is_zero_reg && rs.meta == HOOX_MREG_SP))
  {
    hoox_arm64_writer_put_instruction (self, 0x91000000 | rd.index |
        (rs.index << 5));
  }
  else
  {
    hoox_arm64_writer_put_instruction (self, rd.sf | 0x2a000000 | rd.index |
        (HOOX_MREG_ZR << 5) | (rs.index << 16));
  }

  return TRUE;
}

void
hoox_arm64_writer_put_mov_reg_nzcv (HooxArm64Writer * self,
                                   hx_arm64_reg reg)
{
  HooxArm64RegInfo ri;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  hoox_arm64_writer_put_instruction (self, 0xd53b4200 | ri.index);
}

void
hoox_arm64_writer_put_mov_nzcv_reg (HooxArm64Writer * self,
                                   hx_arm64_reg reg)
{
  HooxArm64RegInfo ri;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  hoox_arm64_writer_put_instruction (self, 0xd51b4200 | ri.index);
}

hx_boolean
hoox_arm64_writer_put_uxtw_reg_reg (HooxArm64Writer * self,
                                   hx_arm64_reg dst_reg,
                                   hx_arm64_reg src_reg)
{
  HooxArm64RegInfo rd, rs;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, src_reg, &rs);

  if (rd.width != 64 || rs.width != 32)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, 0xd3407c00 | (rs.index << 5) |
      rd.index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_add_reg_reg_imm (HooxArm64Writer * self,
                                      hx_arm64_reg dst_reg,
                                      hx_arm64_reg left_reg,
                                      hx_size right_value)
{
  HooxArm64RegInfo rd, rl;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, left_reg, &rl);

  if (rd.width != rl.width)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, rd.sf | 0x11000000 | rd.index |
      (rl.index << 5) | (right_value << 10));

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_add_reg_reg_reg (HooxArm64Writer * self,
                                      hx_arm64_reg dst_reg,
                                      hx_arm64_reg left_reg,
                                      hx_arm64_reg right_reg)
{
  HooxArm64RegInfo rd, rl, rr;
  hx_uint32 flags = 0;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, left_reg, &rl);
  hoox_arm64_writer_describe_reg (self, right_reg, &rr);

  if (rd.width != rl.width || rd.width != rr.width)
    return FALSE;

  if (rd.width == 64)
    flags |= 0x8000000;

  hoox_arm64_writer_put_instruction (self, rd.sf | 0xb000000 | flags | rd.index |
      (rl.index << 5) | (rr.index << 16));

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_sub_reg_reg_imm (HooxArm64Writer * self,
                                      hx_arm64_reg dst_reg,
                                      hx_arm64_reg left_reg,
                                      hx_size right_value)
{
  HooxArm64RegInfo rd, rl;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, left_reg, &rl);

  if (rd.width != rl.width)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, rd.sf | 0x51000000 | rd.index |
      (rl.index << 5) | (right_value << 10));

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_sub_reg_reg_reg (HooxArm64Writer * self,
                                      hx_arm64_reg dst_reg,
                                      hx_arm64_reg left_reg,
                                      hx_arm64_reg right_reg)
{
  HooxArm64RegInfo rd, rl, rr;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, left_reg, &rl);
  hoox_arm64_writer_describe_reg (self, right_reg, &rr);

  if (rd.width != rl.width || rd.width != rr.width)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, rd.sf | 0x4b000000 | rd.index |
      (rl.index << 5) | (rr.index << 16));

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_and_reg_reg_imm (HooxArm64Writer * self,
                                      hx_arm64_reg dst_reg,
                                      hx_arm64_reg left_reg,
                                      hx_uint64 right_value)
{
  HooxArm64RegInfo rd, rl;
  hx_uint right_value_encoded;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, left_reg, &rl);

  if (rd.width != rl.width)
    return FALSE;

  if (!hoox_arm64_try_encode_logical_immediate (right_value, rd.width,
      &right_value_encoded))
    return FALSE;

  hoox_arm64_writer_put_instruction (self, rd.sf | 0x12000000 | rd.index |
      (rl.index << 5) | (right_value_encoded << 10));

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_eor_reg_reg_reg (HooxArm64Writer * self,
                                      hx_arm64_reg dst_reg,
                                      hx_arm64_reg left_reg,
                                      hx_arm64_reg right_reg)
{
  HooxArm64RegInfo rd, rl, rr;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, left_reg, &rl);
  hoox_arm64_writer_describe_reg (self, right_reg, &rr);

  if (rl.width != rd.width || rr.width != rd.width)
    return FALSE;

  hoox_arm64_writer_put_instruction (self,
      (rd.width == 64 ? 0x80000000 : 0x00000000) |
      0x4a000000 |
      (rr.index << 16) |
      (rl.index << 5) |
      rd.index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_ubfm (HooxArm64Writer * self,
                           hx_arm64_reg dst_reg,
                           hx_arm64_reg src_reg,
                           hx_uint8 immr,
                           hx_uint8 imms)
{
  HooxArm64RegInfo rd, rn;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);
  hoox_arm64_writer_describe_reg (self, src_reg, &rn);

  if (rn.width != rd.width)
    return FALSE;

  if (((imms | immr) & 0xc0) != 0)
    return FALSE;

  hoox_arm64_writer_put_instruction (self,
      (rd.width == 64 ? 0x80400000 : 0x00000000) |
      0x53000000 |
      (immr << 16) |
      (imms << 10) |
      (rn.index << 5) |
      rd.index);

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_lsl_reg_imm (HooxArm64Writer * self,
                                  hx_arm64_reg dst_reg,
                                  hx_arm64_reg src_reg,
                                  hx_uint8 shift)
{
  HooxArm64RegInfo rd;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);

  if (rd.width == 32 && (shift & 0xe0) != 0)
    return FALSE;

  if (rd.width == 64 && (shift & 0xc0) != 0)
    return FALSE;

  return hoox_arm64_writer_put_ubfm (self, dst_reg, src_reg,
      -shift % rd.width, (rd.width - 1) - shift);
}

hx_boolean
hoox_arm64_writer_put_lsr_reg_imm (HooxArm64Writer * self,
                                  hx_arm64_reg dst_reg,
                                  hx_arm64_reg src_reg,
                                  hx_uint8 shift)
{
  HooxArm64RegInfo rd;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rd);

  if (rd.width == 32 && (shift & 0xe0) != 0)
    return FALSE;

  return hoox_arm64_writer_put_ubfm (self, dst_reg, src_reg,
      shift, rd.width - 1);
}

hx_boolean
hoox_arm64_writer_put_tst_reg_imm (HooxArm64Writer * self,
                                  hx_arm64_reg reg,
                                  hx_uint64 imm_value)
{
  HooxArm64RegInfo ri;
  hx_uint imm_value_encoded;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (!hoox_arm64_try_encode_logical_immediate (imm_value, ri.width,
      &imm_value_encoded))
    return FALSE;

  hoox_arm64_writer_put_instruction (self, ri.sf | 0x7200001f | (ri.index << 5) |
      (imm_value_encoded << 10));

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_cmp_reg_reg (HooxArm64Writer * self,
                                  hx_arm64_reg reg_a,
                                  hx_arm64_reg reg_b)
{
  HooxArm64RegInfo ra, rb;

  hoox_arm64_writer_describe_reg (self, reg_a, &ra);
  hoox_arm64_writer_describe_reg (self, reg_b, &rb);

  if (ra.width != rb.width)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, ra.sf | 0x6b00001f | (ra.index << 5) |
      (rb.index << 16));

  return TRUE;
}

hx_boolean
hoox_arm64_writer_put_xpaci_reg (HooxArm64Writer * self,
                                hx_arm64_reg reg)
{
  HooxArm64RegInfo ri;

  hoox_arm64_writer_describe_reg (self, reg, &ri);

  if (ri.width != 64)
    return FALSE;

  hoox_arm64_writer_put_instruction (self, 0xdac143e0 | ri.index);

  return TRUE;
}

void
hoox_arm64_writer_put_nop (HooxArm64Writer * self)
{
  hoox_arm64_writer_put_instruction (self, 0xd503201f);
}

void
hoox_arm64_writer_put_brk_imm (HooxArm64Writer * self,
                              hx_uint16 imm)
{
  hoox_arm64_writer_put_instruction (self, 0xd4200000 | (imm << 5));
}

hx_boolean
hoox_arm64_writer_put_mrs (HooxArm64Writer * self,
                          hx_arm64_reg dst_reg,
                          hx_uint16 system_reg)
{
  HooxArm64RegInfo rt;

  hoox_arm64_writer_describe_reg (self, dst_reg, &rt);

  if (rt.width != 64 || (system_reg & 0x8000) != 0)
    return FALSE;

  hoox_arm64_writer_put_instruction (self,
      0xd5300000 |
      (system_reg << 5) |
      rt.index);

  return TRUE;
}

static void
hoox_arm64_writer_put_load_store_pair (HooxArm64Writer * self,
                                      HooxArm64MemOperationType operation_type,
                                      HooxArm64MemOperandType operand_type,
                                      hx_uint rt,
                                      hx_uint rt2,
                                      hx_uint rn,
                                      hx_ssize rn_offset,
                                      HooxArm64IndexMode mode)
{
  hx_uint opc;
  hx_boolean is_vector;
  hx_size shift;

  switch (operand_type)
  {
    case HOOX_MEM_OPERAND_I32:
      opc = 0;
      is_vector = FALSE;
      shift = 2;
      break;
    case HOOX_MEM_OPERAND_I64:
      opc = 2;
      is_vector = FALSE;
      shift = 3;
      break;
    case HOOX_MEM_OPERAND_S32:
      opc = 0;
      is_vector = TRUE;
      shift = 2;
      break;
    case HOOX_MEM_OPERAND_D64:
      opc = 1;
      is_vector = TRUE;
      shift = 3;
      break;
    case HOOX_MEM_OPERAND_Q128:
      opc = 2;
      is_vector = TRUE;
      shift = 4;
      break;
    default:
      opc = 0;
      is_vector = FALSE;
      shift = 0;
      hx_assert_not_reached ();
  }

  hoox_arm64_writer_put_instruction (self, (opc << 30) | (5 << 27) |
      (is_vector << 26) | (mode << 23) | (operation_type << 22) |
      (((rn_offset >> shift) & 0x7f) << 15) |
      (rt2 << 10) | (rn << 5) | rt);
}

void
hoox_arm64_writer_put_instruction (HooxArm64Writer * self,
                                  hx_uint32 insn)
{
  *self->code++ = HX_UINT32_TO_LE (insn);
  self->pc += 4;

  hoox_arm64_writer_maybe_commit_literals (self);
}

hx_boolean
hoox_arm64_writer_put_bytes (HooxArm64Writer * self,
                            const hx_uint8 * data,
                            hx_uint n)
{
  if (n % 4 != 0)
    return FALSE;

  hoox_memcpy (self->code, data, n);
  self->code += n / sizeof (hx_uint32);
  self->pc += n;

  hoox_arm64_writer_maybe_commit_literals (self);

  return TRUE;
}

HooxAddress
hoox_arm64_writer_sign (HooxArm64Writer * self,
                       HooxAddress value)
{
  if (self->ptrauth_support != HOOX_PTRAUTH_SUPPORTED)
    return value;

  return self->sign (value);
}

static HooxAddress
hoox_arm64_writer_strip (HooxArm64Writer * self,
                        HooxAddress value)
{
  if (self->ptrauth_support != HOOX_PTRAUTH_SUPPORTED)
    return value;

  switch (self->target_os)
  {
    case HOOX_OS_MACOS:
    case HOOX_OS_IOS:
    case HOOX_OS_XROS:
      return value & HX_UINT64_CONSTANT (0x7fffffffff);
    default:
      break;
  }

  return value;
}

static hx_boolean
hoox_arm64_writer_try_commit_label_refs (HooxArm64Writer * self)
{
  hx_uint num_refs, ref_index;

  if (!hoox_arm64_writer_has_label_refs (self))
    return TRUE;

  if (!hoox_arm64_writer_has_label_defs (self))
    return FALSE;

  num_refs = self->label_refs.length;

  for (ref_index = 0; ref_index != num_refs; ref_index++)
  {
    HooxArm64LabelRef * r;
    const hx_uint32 * target_insn;
    hx_ssize distance;
    hx_uint32 insn;

    r = hoox_metal_array_element_at (&self->label_refs, ref_index);

    target_insn = hoox_metal_hash_table_lookup (self->label_defs, r->id);
    if (target_insn == NULL)
      return FALSE;

    distance = target_insn - r->insn;

    insn = HX_UINT32_FROM_LE (*r->insn);
    switch (r->type)
    {
      case HOOX_ARM64_B:
      case HOOX_ARM64_BL:
        if (!HOOX_IS_WITHIN_INT26_RANGE (distance))
          return FALSE;
        insn |= distance & HOOX_INT26_MASK;
        break;
      case HOOX_ARM64_B_COND:
      case HOOX_ARM64_CBZ:
      case HOOX_ARM64_CBNZ:
        if (!HOOX_IS_WITHIN_INT19_RANGE (distance))
          return FALSE;
        insn |= (distance & HOOX_INT19_MASK) << 5;
        break;
      case HOOX_ARM64_TBZ:
      case HOOX_ARM64_TBNZ:
        if (!HOOX_IS_WITHIN_INT14_RANGE (distance))
          return FALSE;
        insn |= (distance & HOOX_INT14_MASK) << 5;
        break;
      default:
        hx_assert_not_reached ();
    }

    *r->insn = HX_UINT32_TO_LE (insn);
  }

  hoox_metal_array_remove_all (&self->label_refs);

  return TRUE;
}

static void
hoox_arm64_writer_maybe_commit_literals (HooxArm64Writer * self)
{
  hx_size space_used;
  hx_constpointer after_literals = self->code;

  if (self->earliest_literal_insn == NULL)
    return;

  space_used = (self->code - self->earliest_literal_insn) * sizeof (hx_uint32);
  space_used += self->literal_refs.length * sizeof (hx_uint64);
  if (space_used <= 1048572)
    return;

  self->earliest_literal_insn = NULL;

  hoox_arm64_writer_put_b_label (self, after_literals);
  hoox_arm64_writer_commit_literals (self);
  hoox_arm64_writer_put_label (self, after_literals);
}

static void
hoox_arm64_writer_commit_literals (HooxArm64Writer * self)
{
  hx_uint num_refs, ref_index;
  hx_pointer first_slot, last_slot;
  HooxArm64DataEndian data_endian;

  if (!hoox_arm64_writer_has_literal_refs (self))
    return;

  num_refs = self->literal_refs.length;
  if (num_refs == 0)
    return;

  first_slot = self->code;
  last_slot = first_slot;

  data_endian = self->data_endian;

  for (ref_index = 0; ref_index != num_refs; ref_index++)
  {
    HooxArm64LiteralRef * r;
    hx_int64 literal, * slot, distance;
    hx_uint32 insn;

    r = hoox_metal_array_element_at (&self->literal_refs, ref_index);

    if (r->width != HOOX_LITERAL_64BIT)
      continue;

    literal = r->val;

    for (slot = first_slot; slot != last_slot; slot++)
    {
      hx_int64 candidate = (data_endian == HX_LITTLE_ENDIAN)
          ? HX_INT64_FROM_LE (*slot)
          : HX_INT64_FROM_BE (*slot);
      if (candidate == literal)
        break;
    }

    if (slot == last_slot)
    {
      *slot = (data_endian == HX_LITTLE_ENDIAN)
          ? HX_INT64_TO_LE (literal)
          : HX_INT64_TO_BE (literal);
      last_slot = slot + 1;
    }

    distance = (hx_int64) HX_POINTER_TO_SIZE (slot) -
        (hx_int64) HX_POINTER_TO_SIZE (r->insn);

    insn = HX_UINT32_FROM_LE (*r->insn);
    insn |= ((distance / 4) & HOOX_INT19_MASK) << 5;
    *r->insn = HX_UINT32_TO_LE (insn);
  }

  for (ref_index = 0; ref_index != num_refs; ref_index++)
  {
    HooxArm64LiteralRef * r;
    hx_int32 literal, * slot;
    hx_int64 distance;
    hx_uint32 insn;

    r = hoox_metal_array_element_at (&self->literal_refs, ref_index);

    if (r->width != HOOX_LITERAL_32BIT)
      continue;

    literal = r->val;

    for (slot = first_slot; slot != last_slot; slot++)
    {
      hx_int32 candidate = (data_endian == HX_LITTLE_ENDIAN)
          ? HX_INT32_FROM_LE (*slot)
          : HX_INT32_FROM_BE (*slot);
      if (candidate == literal)
        break;
    }

    if (slot == last_slot)
    {
      *slot = (data_endian == HX_LITTLE_ENDIAN)
          ? HX_INT32_TO_LE (literal)
          : HX_INT32_TO_BE (literal);
      last_slot = slot + 1;
    }

    distance = (hx_int64) HX_POINTER_TO_SIZE (slot) -
        (hx_int64) HX_POINTER_TO_SIZE (r->insn);

    insn = HX_UINT32_FROM_LE (*r->insn);
    insn |= ((distance / 4) & HOOX_INT19_MASK) << 5;
    *r->insn = HX_UINT32_TO_LE (insn);
  }

  self->code = (hx_uint32 *) last_slot;
  self->pc += (hx_uint8 *) last_slot - (hx_uint8 *) first_slot;

  hoox_metal_array_remove_all (&self->literal_refs);
}

static void
hoox_arm64_writer_describe_reg (HooxArm64Writer * self,
                               hx_arm64_reg reg,
                               HooxArm64RegInfo * ri)
{
  if (reg >= HX_ARM64_REG_X0 && reg <= HX_ARM64_REG_X28)
  {
    ri->meta = HOOX_MREG_R0 + (reg - HX_ARM64_REG_X0);
    ri->is_integer = TRUE;
    ri->width = 64;
    ri->sf = 0x80000000;
  }
  else if (reg == HX_ARM64_REG_X29)
  {
    ri->meta = HOOX_MREG_R29;
    ri->is_integer = TRUE;
    ri->width = 64;
    ri->sf = 0x80000000;
  }
  else if (reg == HX_ARM64_REG_X30)
  {
    ri->meta = HOOX_MREG_R30;
    ri->is_integer = TRUE;
    ri->width = 64;
    ri->sf = 0x80000000;
  }
  else if (reg == HX_ARM64_REG_SP)
  {
    ri->meta = HOOX_MREG_SP;
    ri->is_integer = TRUE;
    ri->width = 64;
    ri->sf = 0x80000000;
  }
  else if (reg >= HX_ARM64_REG_W0 && reg <= HX_ARM64_REG_W30)
  {
    ri->meta = HOOX_MREG_R0 + (reg - HX_ARM64_REG_W0);
    ri->is_integer = TRUE;
    ri->width = 32;
    ri->sf = 0x00000000;
  }
  else if (reg >= HX_ARM64_REG_S0 && reg <= HX_ARM64_REG_S31)
  {
    ri->meta = HOOX_MREG_R0 + (reg - HX_ARM64_REG_S0);
    ri->is_integer = FALSE;
    ri->width = 32;
    ri->sf = 0x00000000;
  }
  else if (reg >= HX_ARM64_REG_D0 && reg <= HX_ARM64_REG_D31)
  {
    ri->meta = HOOX_MREG_R0 + (reg - HX_ARM64_REG_D0);
    ri->is_integer = FALSE;
    ri->width = 64;
    ri->sf = 0x00000000;
  }
  else if (reg >= HX_ARM64_REG_Q0 && reg <= HX_ARM64_REG_Q31)
  {
    ri->meta = HOOX_MREG_R0 + (reg - HX_ARM64_REG_Q0);
    ri->is_integer = FALSE;
    ri->width = 128;
    ri->sf = 0x00000000;
  }
  else if (reg == HX_ARM64_REG_XZR)
  {
    ri->meta = HOOX_MREG_ZR;
    ri->is_integer = TRUE;
    ri->width = 64;
    ri->sf = 0x80000000;
  }
  else if (reg == HX_ARM64_REG_WZR)
  {
    ri->meta = HOOX_MREG_ZR;
    ri->is_integer = TRUE;
    ri->width = 32;
    ri->sf = 0x00000000;
  }
  else
  {
    hx_assert_not_reached ();
  }
  ri->index = ri->meta - HOOX_MREG_R0;
}

static HooxArm64MemOperandType
hoox_arm64_mem_operand_type_from_reg_info (const HooxArm64RegInfo * ri)
{
  if (ri->is_integer)
  {
    switch (ri->width)
    {
      case 32: return HOOX_MEM_OPERAND_I32;
      case 64: return HOOX_MEM_OPERAND_I64;
    }
  }
  else
  {
    switch (ri->width)
    {
      case 32: return HOOX_MEM_OPERAND_S32;
      case 64: return HOOX_MEM_OPERAND_D64;
      case 128: return HOOX_MEM_OPERAND_Q128;
    }
  }

  hx_assert_not_reached ();
  return HOOX_MEM_OPERAND_I32;
}

static hx_boolean
hoox_arm64_try_encode_logical_immediate (hx_uint64 imm_value,
                                        hx_uint reg_width,
                                        hx_uint * imm_enc)
{
  hx_uint element_size, num_rotations, num_trailing_ones;
  hx_uint immr, imms, n;

  if (imm_value == 0 || imm_value == ~HX_UINT64_CONSTANT (0))
    return FALSE;
  if (reg_width == 32)
  {
    if ((imm_value >> 32) != 0 || imm_value == ~0U)
      return FALSE;
  }

  element_size =
      hoox_arm64_determine_logical_element_size (imm_value, reg_width);

  if (!hoox_arm64_try_determine_logical_rotation (imm_value, element_size,
      &num_rotations, &num_trailing_ones))
    return FALSE;

  immr = (element_size - num_rotations) & (element_size - 1);

  imms = ~(element_size - 1) << 1;
  imms |= num_trailing_ones - 1;

  n = ((imms >> 6) & 1) ^ 1;

  *imm_enc = (n << 12) | (immr << 6) | (imms & 0x3f);

  return TRUE;
}

static hx_uint
hoox_arm64_determine_logical_element_size (hx_uint64 imm_value,
                                          hx_uint reg_width)
{
  hx_uint size = reg_width;

  do
  {
    hx_uint next_size;
    hx_uint64 mask;

    next_size = size / 2;

    mask = (HX_UINT64_CONSTANT (1) << next_size) - 1;
    if ((imm_value & mask) != ((imm_value >> next_size) & mask))
      break;

    size = next_size;
  }
  while (size > 2);

  return size;
}

static hx_boolean
hoox_arm64_try_determine_logical_rotation (hx_uint64 imm_value,
                                          hx_uint element_size,
                                          hx_uint * num_rotations,
                                          hx_uint * num_trailing_ones)
{
  hx_uint64 mask;

  mask = ((hx_uint64) HX_INT64_CONSTANT (-1)) >> (64 - element_size);

  imm_value &= mask;

  if (hoox_is_shifted_mask_64 (imm_value))
  {
    *num_rotations = hoox_count_trailing_zeros (imm_value);
    *num_trailing_ones = hoox_count_trailing_ones (imm_value >> *num_rotations);
  }
  else
  {
    hx_uint num_leading_ones;

    imm_value |= ~mask;
    if (!hoox_is_shifted_mask_64 (~imm_value))
      return FALSE;

    num_leading_ones = hoox_count_leading_ones (imm_value);
    *num_rotations = 64 - num_leading_ones;
    *num_trailing_ones = num_leading_ones +
        hoox_count_trailing_ones (imm_value) -
        (64 - element_size);
  }

  return TRUE;
}

static hx_boolean
hoox_is_shifted_mask_64 (hx_uint64 value)
{
  if (value == 0)
    return FALSE;

  return hoox_is_mask_64 ((value - 1) | value);
}

static hx_boolean
hoox_is_mask_64 (hx_uint64 value)
{
  if (value == 0)
    return FALSE;

  return ((value + 1) & value) == 0;
}

static hx_uint
hoox_count_leading_zeros (hx_uint64 value)
{
  if (value == 0)
    return 64;

#if defined (_MSC_VER) && HX_SIZEOF_VOID_P == 4
  {
    unsigned long index;

    if (_BitScanReverse (&index, value >> 32))
      return 31 - index;

    _BitScanReverse (&index, value & 0xffffffff);

    return 63 - index;
  }
#elif defined (_MSC_VER) && HX_SIZEOF_VOID_P == 8
  {
    unsigned long index;

    _BitScanReverse64 (&index, value);

    return 63 - index;
  }
#elif defined (HAVE_CLTZ)
  return __builtin_clzll (value);
#else
  hx_uint num_zeros = 0;
  hx_uint64 bits = value;

  while ((bits & (HX_UINT64_CONSTANT (1) << 63)) == 0)
  {
    num_zeros++;
    bits <<= 1;
  }

  return num_zeros;
#endif
}

static hx_uint
hoox_count_trailing_zeros (hx_uint64 value)
{
  if (value == 0)
    return 64;

#if defined (_MSC_VER) && HX_SIZEOF_VOID_P == 4
  {
    unsigned long index;

    if (_BitScanForward (&index, value & 0xffffffff))
      return index;

    _BitScanForward (&index, value >> 32);

    return 32 + index;
  }
#elif defined (_MSC_VER) && HX_SIZEOF_VOID_P == 8
  {
    unsigned long index;

    _BitScanForward64 (&index, value);

    return index;
  }
#elif defined (HAVE_CLTZ)
  return __builtin_ctzll (value);
#else
  hx_uint num_zeros = 0;
  hx_uint64 bits = value;

  while ((bits & HX_UINT64_CONSTANT (1)) == 0)
  {
    num_zeros++;
    bits >>= 1;
  }

  return num_zeros;
#endif
}

static hx_uint
hoox_count_leading_ones (hx_uint64 value)
{
  return hoox_count_leading_zeros (~value);
}

static hx_uint
hoox_count_trailing_ones (hx_uint64 value)
{
  return hoox_count_trailing_zeros (~value);
}
