/*
 * Copyright (C) 2010-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C)      2019 Jon Wilson <jonwilson@zepler.net>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxarmwriter.h"

#include "hooxarmreg.h"
#include <string.h>
#include "hooxmemory.h"
#include "hooxprocess.h"

typedef struct _HooxArmLabelRef HooxArmLabelRef;
typedef struct _HooxArmLiteralRef HooxArmLiteralRef;

struct _HooxArmLabelRef
{
  hx_constpointer id;
  hx_uint32 * insn;
};

struct _HooxArmLiteralRef
{
  hx_uint32 * insn;
  hx_uint32 val;
};

static void hoox_arm_writer_reset_refs (HooxArmWriter * self);

static void hoox_arm_writer_put_argument_list_setup (HooxArmWriter * self,
    hx_uint n_args, const HooxArgument * args);
static void hoox_arm_writer_put_argument_list_setup_va (HooxArmWriter * self,
    hx_uint n_args, va_list args);
static void hoox_arm_writer_put_argument_list_teardown (HooxArmWriter * self,
    hx_uint n_args);
static void hoox_arm_writer_put_call_address_body (HooxArmWriter * self,
    HooxAddress address);
static hx_boolean hoox_arm_writer_put_vector_push_or_pop_range (
    HooxArmWriter * self, hx_uint32 insn_template, hx_arm_reg first_reg,
    hx_arm_reg last_reg);

static hx_boolean hoox_arm_writer_try_commit_label_refs (HooxArmWriter * self);
static void hoox_arm_writer_maybe_commit_literals (HooxArmWriter * self);
static void hoox_arm_writer_commit_literals (HooxArmWriter * self);

static hx_uint32 hoox_arm_condify (hx_arm_cc cc);
static hx_uint32 hoox_arm_shiftify (hx_arm_shifter shifter);

HooxArmWriter *
hoox_arm_writer_new (hx_pointer code_address)
{
  HooxArmWriter * writer;

  writer = hx_slice_new (HooxArmWriter);

  hoox_arm_writer_init (writer, code_address);

  return writer;
}

HooxArmWriter *
hoox_arm_writer_ref (HooxArmWriter * writer)
{
  hx_atomic_int_inc (&writer->ref_count);

  return writer;
}

void
hoox_arm_writer_unref (HooxArmWriter * writer)
{
  if (hx_atomic_int_dec_and_test (&writer->ref_count))
  {
    hoox_arm_writer_clear (writer);

    hx_slice_free (HooxArmWriter, writer);
  }
}

void
hoox_arm_writer_init (HooxArmWriter * writer,
                     hx_pointer code_address)
{
  writer->ref_count = 1;
  writer->flush_on_destroy = TRUE;

  writer->target_os = hoox_process_get_native_os ();
  writer->cpu_features = HOOX_CPU_THUMB_INTERWORK;

  writer->label_defs = NULL;
  writer->label_refs.data = NULL;
  writer->literal_refs.data = NULL;

  hoox_arm_writer_reset (writer, code_address);
}

static hx_boolean
hoox_arm_writer_has_label_defs (HooxArmWriter * self)
{
  return self->label_defs != NULL;
}

static hx_boolean
hoox_arm_writer_has_label_refs (HooxArmWriter * self)
{
  return self->label_refs.data != NULL;
}

static hx_boolean
hoox_arm_writer_has_literal_refs (HooxArmWriter * self)
{
  return self->literal_refs.data != NULL;
}

void
hoox_arm_writer_clear (HooxArmWriter * writer)
{
  if (writer->flush_on_destroy)
    hoox_arm_writer_flush (writer);

  if (hoox_arm_writer_has_label_defs (writer))
    hoox_metal_hash_table_unref (writer->label_defs);

  if (hoox_arm_writer_has_label_refs (writer))
    hoox_metal_array_free (&writer->label_refs);

  if (hoox_arm_writer_has_literal_refs (writer))
    hoox_metal_array_free (&writer->literal_refs);
}

void
hoox_arm_writer_reset (HooxArmWriter * writer,
                      hx_pointer code_address)
{
  writer->base = code_address;
  writer->code = code_address;
  writer->pc = HOOX_ADDRESS (code_address);

  if (hoox_arm_writer_has_label_defs (writer))
    hoox_metal_hash_table_remove_all (writer->label_defs);

  hoox_arm_writer_reset_refs (writer);
}

static void
hoox_arm_writer_reset_refs (HooxArmWriter * self)
{
  if (hoox_arm_writer_has_label_refs (self))
    hoox_metal_array_remove_all (&self->label_refs);

  if (hoox_arm_writer_has_literal_refs (self))
    hoox_metal_array_remove_all (&self->literal_refs);

  self->earliest_literal_insn = NULL;
}

void
hoox_arm_writer_set_target_os (HooxArmWriter * self,
                              HooxOS os)
{
  self->target_os = os;
}

hx_pointer
hoox_arm_writer_cur (HooxArmWriter * self)
{
  return self->code;
}

hx_uint
hoox_arm_writer_offset (HooxArmWriter * self)
{
  return (hx_uint) (self->code - self->base) * sizeof (hx_uint32);
}

void
hoox_arm_writer_skip (HooxArmWriter * self,
                     hx_uint n_bytes)
{
  self->code = (hx_uint32 *) (((hx_uint8 *) self->code) + n_bytes);
  self->pc += n_bytes;
}

hx_boolean
hoox_arm_writer_flush (HooxArmWriter * self)
{
  if (!hoox_arm_writer_try_commit_label_refs (self))
    goto error;

  hoox_arm_writer_commit_literals (self);

  return TRUE;

error:
  {
    hoox_arm_writer_reset_refs (self);

    return FALSE;
  }
}

hx_boolean
hoox_arm_writer_put_label (HooxArmWriter * self,
                          hx_constpointer id)
{
  if (!hoox_arm_writer_has_label_defs (self))
    self->label_defs = hoox_metal_hash_table_new (NULL, NULL);

  if (hoox_metal_hash_table_lookup (self->label_defs, id) != NULL)
    return FALSE;

  hoox_metal_hash_table_insert (self->label_defs, (hx_pointer) id, self->code);

  return TRUE;
}

static void
hoox_arm_writer_add_label_reference_here (HooxArmWriter * self,
                                         hx_constpointer id)
{
  HooxArmLabelRef * r;

  if (!hoox_arm_writer_has_label_refs (self))
    hoox_metal_array_init (&self->label_refs, sizeof (HooxArmLabelRef));

  r = hoox_metal_array_append (&self->label_refs);
  r->id = id;
  r->insn = self->code;
}

static void
hoox_arm_writer_add_literal_reference_here (HooxArmWriter * self,
                                           hx_uint32 val)
{
  HooxArmLiteralRef * r;

  if (!hoox_arm_writer_has_literal_refs (self))
    hoox_metal_array_init (&self->literal_refs, sizeof (HooxArmLiteralRef));

  r = hoox_metal_array_append (&self->literal_refs);
  r->insn = self->code;
  r->val = val;

  if (self->earliest_literal_insn == NULL)
    self->earliest_literal_insn = r->insn;
}

void
hoox_arm_writer_put_call_address_with_arguments (HooxArmWriter * self,
                                                HooxAddress func,
                                                hx_uint n_args,
                                                ...)
{
  va_list args;

  va_start (args, n_args);
  hoox_arm_writer_put_argument_list_setup_va (self, n_args, args);
  va_end (args);

  hoox_arm_writer_put_call_address_body (self, func);

  hoox_arm_writer_put_argument_list_teardown (self, n_args);
}

void
hoox_arm_writer_put_call_address_with_arguments_array (HooxArmWriter * self,
                                                      HooxAddress func,
                                                      hx_uint n_args,
                                                      const HooxArgument * args)
{
  hoox_arm_writer_put_argument_list_setup (self, n_args, args);

  hoox_arm_writer_put_call_address_body (self, func);

  hoox_arm_writer_put_argument_list_teardown (self, n_args);
}

void
hoox_arm_writer_put_call_reg (HooxArmWriter * self,
                             hx_arm_reg reg)
{
  if ((self->cpu_features & HOOX_CPU_THUMB_INTERWORK) != 0)
    hoox_arm_writer_put_blx_reg (self, reg);
  else
    hoox_arm_writer_put_bl_reg (self, reg);
}

void
hoox_arm_writer_put_call_reg_with_arguments (HooxArmWriter * self,
                                            hx_arm_reg reg,
                                            hx_uint n_args,
                                            ...)
{
  va_list args;

  va_start (args, n_args);
  hoox_arm_writer_put_argument_list_setup_va (self, n_args, args);
  va_end (args);

  hoox_arm_writer_put_call_reg (self, reg);

  hoox_arm_writer_put_argument_list_teardown (self, n_args);
}

void
hoox_arm_writer_put_call_reg_with_arguments_array (HooxArmWriter * self,
                                                  hx_arm_reg reg,
                                                  hx_uint n_args,
                                                  const HooxArgument * args)
{
  hoox_arm_writer_put_argument_list_setup (self, n_args, args);

  hoox_arm_writer_put_call_reg (self, reg);

  hoox_arm_writer_put_argument_list_teardown (self, n_args);
}

static void
hoox_arm_writer_put_argument_list_setup (HooxArmWriter * self,
                                        hx_uint n_args,
                                        const HooxArgument * args)
{
  hx_uint n_stack_args;
  hx_int arg_index;

  n_stack_args = MAX ((hx_int) n_args - 4, 0);
  if (n_stack_args % 2 != 0)
    hoox_arm_writer_put_sub_reg_u16 (self, HX_ARM_REG_SP, 4);

  for (arg_index = (hx_int) n_args - 1; arg_index >= 0; arg_index--)
  {
    const HooxArgument * arg = &args[arg_index];
    const hx_arm_reg dst_reg = HX_ARM_REG_R0 + arg_index;

    if (arg_index < 4)
    {
      if (arg->type == HOOX_ARG_ADDRESS)
      {
        hoox_arm_writer_put_ldr_reg_address (self, dst_reg, arg->value.address);
      }
      else
      {
        hx_arm_reg src_reg = arg->value.reg;
        HooxArmRegInfo rs;

        hoox_arm_reg_describe (src_reg, &rs);

        if (src_reg != dst_reg)
          hoox_arm_writer_put_mov_reg_reg (self, dst_reg, arg->value.reg);
      }
    }
    else
    {
      if (arg->type == HOOX_ARG_ADDRESS)
      {
        hoox_arm_writer_put_ldr_reg_address (self, HX_ARM_REG_R0,
            arg->value.address);
        hoox_arm_writer_put_push_regs (self, 1, HX_ARM_REG_R0);
      }
      else
      {
        hoox_arm_writer_put_push_regs (self, 1, arg->value.reg);
      }
    }
  }
}

static void
hoox_arm_writer_put_argument_list_setup_va (HooxArmWriter * self,
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
      arg->value.reg = va_arg (args, hx_arm_reg);
    else
      hx_assert_not_reached ();
  }

  hoox_arm_writer_put_argument_list_setup (self, n_args, arg_values);
}

static void
hoox_arm_writer_put_argument_list_teardown (HooxArmWriter * self,
                                           hx_uint n_args)
{
  hx_uint n_stack_args, n_stack_slots;

  n_stack_args = MAX ((hx_int) n_args - 4, 0);
  if (n_stack_args == 0)
    return;

  n_stack_slots = n_stack_args;
  if (n_stack_slots % 2 != 0)
    n_stack_slots++;

  hoox_arm_writer_put_add_reg_u16 (self, HX_ARM_REG_SP, n_stack_slots * 4);
}

static void
hoox_arm_writer_put_call_address_body (HooxArmWriter * self,
                                      HooxAddress address)
{
  HooxAddress aligned_address;

  aligned_address = address & ~HOOX_ADDRESS (1);

  if (hoox_arm_writer_can_branch_directly_between (self, self->pc,
      aligned_address))
  {
    if (aligned_address == address)
      hoox_arm_writer_put_bl_imm (self, aligned_address);
    else
      hoox_arm_writer_put_blx_imm (self, aligned_address);
  }
  else
  {
    hoox_arm_writer_put_add_reg_reg_imm (self, HX_ARM_REG_LR, HX_ARM_REG_PC, 3 * 4);
    hoox_arm_writer_put_push_regs (self, 2, HX_ARM_REG_R0, HX_ARM_REG_PC);
    hoox_arm_writer_put_ldr_reg_address (self, HX_ARM_REG_R0, address);
    hoox_arm_writer_put_str_reg_reg_offset (self, HX_ARM_REG_R0, HX_ARM_REG_SP, 4);
    hoox_arm_writer_put_pop_regs (self, 2, HX_ARM_REG_R0, HX_ARM_REG_PC);
  }
}

void
hoox_arm_writer_put_branch_address (HooxArmWriter * self,
                                   HooxAddress address)
{
  if (hoox_arm_writer_can_branch_directly_between (self, self->pc, address))
  {
    hoox_arm_writer_put_b_imm (self, address);
  }
  else
  {
    hoox_arm_writer_put_push_regs (self, 2, HX_ARM_REG_R0, HX_ARM_REG_PC);
    hoox_arm_writer_put_ldr_reg_address (self, HX_ARM_REG_R0, address);
    hoox_arm_writer_put_str_reg_reg_offset (self, HX_ARM_REG_R0, HX_ARM_REG_SP, 4);
    hoox_arm_writer_put_pop_regs (self, 2, HX_ARM_REG_R0, HX_ARM_REG_PC);
  }
}

hx_boolean
hoox_arm_writer_can_branch_directly_between (HooxArmWriter * self,
                                            HooxAddress from,
                                            HooxAddress to)
{
  hx_int64 distance = (hx_int64) to - (hx_int64) from;

  return HOOX_IS_WITHIN_INT26_RANGE (distance);
}

hx_boolean
hoox_arm_writer_put_b_imm (HooxArmWriter * self,
                          HooxAddress target)
{
  return hoox_arm_writer_put_b_cond_imm (self, HX_ARM_CC_AL, target);
}

hx_boolean
hoox_arm_writer_put_b_cond_imm (HooxArmWriter * self,
                               hx_arm_cc cc,
                               HooxAddress target)
{
  hx_int64 distance;

  distance = (hx_int64) target - (hx_int64) (self->pc + 8);
  if (!HOOX_IS_WITHIN_INT26_RANGE (distance))
    return FALSE;

  hoox_arm_writer_put_instruction (self, 0x0a000000 | hoox_arm_condify (cc) |
      ((distance >> 2) & HOOX_INT24_MASK));

  return TRUE;
}

void
hoox_arm_writer_put_b_label (HooxArmWriter * self,
                            hx_constpointer label_id)
{
  hoox_arm_writer_put_b_cond_label (self, HX_ARM_CC_AL, label_id);
}

void
hoox_arm_writer_put_b_cond_label (HooxArmWriter * self,
                                 hx_arm_cc cc,
                                 hx_constpointer label_id)
{
  hoox_arm_writer_add_label_reference_here (self, label_id);
  hoox_arm_writer_put_instruction (self, 0x0a000000 | hoox_arm_condify (cc));
}

hx_boolean
hoox_arm_writer_put_bl_imm (HooxArmWriter * self,
                           HooxAddress target)
{
  hx_int64 distance;

  distance = (hx_int64) target - (hx_int64) (self->pc + 8);
  if (!HOOX_IS_WITHIN_INT26_RANGE (distance))
    return FALSE;

  hoox_arm_writer_put_instruction (self, 0xeb000000 |
      ((distance >> 2) & HOOX_INT24_MASK));

  return TRUE;
}

hx_boolean
hoox_arm_writer_put_blx_imm (HooxArmWriter * self,
                            HooxAddress target)
{
  hx_int64 distance;
  hx_uint32 halfword_bit;

  distance = (hx_int64) target - (hx_int64) (self->pc + 8);
  if (!HOOX_IS_WITHIN_INT26_RANGE (distance))
    return FALSE;

  halfword_bit = (distance >> 1) & 1;

  hoox_arm_writer_put_instruction (self, 0xfa000000 | (halfword_bit << 24) |
      ((distance >> 2) & HOOX_INT24_MASK));

  return TRUE;
}

void
hoox_arm_writer_put_bl_label (HooxArmWriter * self,
                             hx_constpointer label_id)
{
  hoox_arm_writer_add_label_reference_here (self, label_id);
  hoox_arm_writer_put_instruction (self, 0xeb000000);
}

void
hoox_arm_writer_put_bx_reg (HooxArmWriter * self,
                           hx_arm_reg reg)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_arm_writer_put_instruction (self, 0xe12fff10 | ri.index);
}

void
hoox_arm_writer_put_bl_reg (HooxArmWriter * self,
                           hx_arm_reg reg)
{
  hoox_arm_writer_put_mov_reg_reg (self, HX_ARM_REG_LR, HX_ARM_REG_PC);
  hoox_arm_writer_put_mov_reg_reg (self, HX_ARM_REG_PC, reg);
}

void
hoox_arm_writer_put_blx_reg (HooxArmWriter * self,
                            hx_arm_reg reg)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_arm_writer_put_instruction (self, 0xe12fff30 | ri.index);
}

void
hoox_arm_writer_put_ret (HooxArmWriter * self)
{
  hoox_arm_writer_put_instruction (self, 0xe1a0f00e);
}

void
hoox_arm_writer_put_push_regs (HooxArmWriter * self,
                              hx_uint n,
                              ...)
{
  va_list args;
  hx_uint16 mask;
  hx_uint i;

  va_start (args, n);

  mask = 0;
  for (i = 0; i != n; i++)
  {
    hx_arm_reg reg;
    HooxArmRegInfo ri;

    reg = va_arg (args, hx_arm_reg);
    hoox_arm_reg_describe (reg, &ri);

    mask |= 1 << ri.index;
  }

  va_end (args);

  hoox_arm_writer_put_instruction (self, 0xe92d0000 | mask);
}

void
hoox_arm_writer_put_pop_regs (HooxArmWriter * self,
                             hx_uint n,
                             ...)
{
  va_list args;
  hx_uint16 mask;
  hx_uint i;

  va_start (args, n);

  mask = 0;
  for (i = 0; i != n; i++)
  {
    hx_arm_reg reg;
    HooxArmRegInfo ri;

    reg = va_arg (args, hx_arm_reg);
    hoox_arm_reg_describe (reg, &ri);

    mask |= 1 << ri.index;
  }

  va_end (args);

  hoox_arm_writer_put_ldmia_reg_mask_wb (self, HX_ARM_REG_SP, mask);
}

hx_boolean
hoox_arm_writer_put_vpush_range (HooxArmWriter * self,
                                hx_arm_reg first_reg,
                                hx_arm_reg last_reg)
{
  return hoox_arm_writer_put_vector_push_or_pop_range (self, 0x0d2d0a00,
      first_reg, last_reg);
}

hx_boolean
hoox_arm_writer_put_vpop_range (HooxArmWriter * self,
                               hx_arm_reg first_reg,
                               hx_arm_reg last_reg)
{
  return hoox_arm_writer_put_vector_push_or_pop_range (self, 0x0cbd0a00,
      first_reg, last_reg);
}

static hx_boolean
hoox_arm_writer_put_vector_push_or_pop_range (HooxArmWriter * self,
                                             hx_uint32 insn_template,
                                             hx_arm_reg first_reg,
                                             hx_arm_reg last_reg)
{
  HooxArmRegInfo rf, rl;
  hx_uint8 count, imm8;

  hoox_arm_reg_describe (first_reg, &rf);
  hoox_arm_reg_describe (last_reg, &rl);

  if (rl.width != rf.width || rl.index < rf.index)
    return FALSE;

  if (rf.width == 128)
  {
    rf.width = 64;
    rf.index *= 2;
    rf.meta = HOOX_ARM_MREG_D0 + rf.index;

    rl.width = 64;
    rl.index *= 2;
    if (rl.index % 2 == 0)
      rl.index++;
    rl.meta = HOOX_ARM_MREG_D0 + rl.index;
  }

  count = rl.index - rf.index + 1;
  if (rf.width == 64)
  {
    if (count > 16)
      return FALSE;
    imm8 = 2 * count;
  }
  else
  {
    imm8 = count;
  }

  hoox_arm_writer_put_instruction (self, insn_template | (0xe << 28) |
      ((rf.index >> 4) << 22) | ((rf.index & HOOX_INT4_MASK) << 12) |
      ((rf.width == 64) << 8) | imm8);

  return TRUE;
}

hx_boolean
hoox_arm_writer_put_ldr_reg_address (HooxArmWriter * self,
                                    hx_arm_reg reg,
                                    HooxAddress address)
{
  return hoox_arm_writer_put_ldr_reg_u32 (self, reg, (hx_uint32) address);
}

hx_boolean
hoox_arm_writer_put_ldr_reg_u32 (HooxArmWriter * self,
                                hx_arm_reg reg,
                                hx_uint32 val)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_arm_writer_add_literal_reference_here (self, val);
  hoox_arm_writer_put_instruction (self, 0xe51f0000 | (ri.index << 12));

  return TRUE;
}

hx_boolean
hoox_arm_writer_put_ldr_reg_reg (HooxArmWriter * self,
                                hx_arm_reg dst_reg,
                                hx_arm_reg src_reg)
{
  return hoox_arm_writer_put_ldr_reg_reg_offset (self, dst_reg, src_reg, 0);
}

hx_boolean
hoox_arm_writer_put_ldr_reg_reg_offset (HooxArmWriter * self,
                                       hx_arm_reg dst_reg,
                                       hx_arm_reg src_reg,
                                       hx_ssize src_offset)
{
  return hoox_arm_writer_put_ldr_cond_reg_reg_offset (self, HX_ARM_CC_AL, dst_reg,
      src_reg, src_offset);
}

hx_boolean
hoox_arm_writer_put_ldr_cond_reg_reg_offset (HooxArmWriter * self,
                                            hx_arm_cc cc,
                                            hx_arm_reg dst_reg,
                                            hx_arm_reg src_reg,
                                            hx_ssize src_offset)
{
  HooxArmRegInfo rd, rs;
  hx_boolean is_positive;
  hx_size abs_src_offset;

  hoox_arm_reg_describe (dst_reg, &rd);
  hoox_arm_reg_describe (src_reg, &rs);

  is_positive = src_offset >= 0;

  abs_src_offset = ABS (src_offset);
  if (abs_src_offset >= 4096)
    return FALSE;

  hoox_arm_writer_put_instruction (self, 0x05100000 | hoox_arm_condify (cc) |
      (is_positive << 23) | (rd.index << 12) | (rs.index << 16) |
      abs_src_offset);

  return TRUE;
}

void
hoox_arm_writer_put_ldmia_reg_mask (HooxArmWriter * self,
                                   hx_arm_reg reg,
                                   hx_uint16 mask)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_arm_writer_put_instruction (self, 0xe8900000 | (ri.index << 16) | mask);
}

void
hoox_arm_writer_put_ldmia_reg_mask_wb (HooxArmWriter * self,
                                      hx_arm_reg reg,
                                      hx_uint16 mask)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_arm_writer_put_instruction (self, 0xe8b00000 | (ri.index << 16) | mask);
}

hx_boolean
hoox_arm_writer_put_str_reg_reg (HooxArmWriter * self,
                                hx_arm_reg src_reg,
                                hx_arm_reg dst_reg)
{
  return hoox_arm_writer_put_str_reg_reg_offset (self, src_reg, dst_reg, 0);
}

hx_boolean
hoox_arm_writer_put_str_reg_reg_offset (HooxArmWriter * self,
                                       hx_arm_reg src_reg,
                                       hx_arm_reg dst_reg,
                                       hx_ssize dst_offset)
{
  return hoox_arm_writer_put_str_cond_reg_reg_offset (self, HX_ARM_CC_AL, src_reg,
      dst_reg, dst_offset);
}

hx_boolean
hoox_arm_writer_put_str_cond_reg_reg_offset (HooxArmWriter * self,
                                            hx_arm_cc cc,
                                            hx_arm_reg src_reg,
                                            hx_arm_reg dst_reg,
                                            hx_ssize dst_offset)
{
  HooxArmRegInfo rs, rd;
  hx_boolean is_positive;
  hx_size abs_dst_offset;

  hoox_arm_reg_describe (src_reg, &rs);
  hoox_arm_reg_describe (dst_reg, &rd);

  is_positive = dst_offset >= 0;

  abs_dst_offset = ABS (dst_offset);
  if (abs_dst_offset >= 4096)
    return FALSE;

  hoox_arm_writer_put_instruction (self, 0x05000000 | hoox_arm_condify (cc) |
      (is_positive << 23) | (rs.index << 12) | (rd.index << 16) |
      abs_dst_offset);

  return TRUE;
}

void
hoox_arm_writer_put_mov_reg_reg (HooxArmWriter * self,
                                hx_arm_reg dst_reg,
                                hx_arm_reg src_reg)
{
  hoox_arm_writer_put_add_reg_reg_imm (self, dst_reg, src_reg, 0);
}

void
hoox_arm_writer_put_mov_reg_reg_shift (HooxArmWriter * self,
                                      hx_arm_reg dst_reg,
                                      hx_arm_reg src_reg,
                                      hx_arm_shifter shift,
                                      hx_uint16 shift_value)
{
  HooxArmRegInfo rd, rs;
  hx_boolean is_noop;

  hoox_arm_reg_describe (dst_reg, &rd);
  hoox_arm_reg_describe (src_reg, &rs);

  is_noop = dst_reg == src_reg && shift_value == 0;
  if (is_noop)
    return;

  hoox_arm_writer_put_instruction (self, 0xe1a00000 | (rd.index << 12) |
      ((shift_value & 0x1f) << 7) | hoox_arm_shiftify (shift) | rs.index);
}

void
hoox_arm_writer_put_mov_reg_cpsr (HooxArmWriter * self,
                                 hx_arm_reg reg)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_arm_writer_put_instruction (self, 0xe10f0000 | ri.index << 12);
}

void
hoox_arm_writer_put_mov_cpsr_reg (HooxArmWriter * self,
                                 hx_arm_reg reg)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_arm_writer_put_instruction (self, 0xe129f000 | ri.index);
}

void
hoox_arm_writer_put_add_reg_u16 (HooxArmWriter * self,
                                hx_arm_reg dst_reg,
                                hx_uint16 val)
{
  hoox_arm_writer_put_add_reg_reg_imm (self, dst_reg, dst_reg,
      0xc00 | ((val >> 8) & 0xff));
  hoox_arm_writer_put_add_reg_reg_imm (self, dst_reg, dst_reg,
      val & 0xff);
}

void
hoox_arm_writer_put_add_reg_u32 (HooxArmWriter * self,
                                hx_arm_reg dst_reg,
                                hx_uint32 val)
{
  hoox_arm_writer_put_add_reg_reg_imm (self, dst_reg, dst_reg,
      0x400 | ((val >> 24) & 0xff));
  hoox_arm_writer_put_add_reg_reg_imm (self, dst_reg, dst_reg,
      0x800 | ((val >> 16) & 0xff));
  hoox_arm_writer_put_add_reg_reg_imm (self, dst_reg, dst_reg,
      0xc00 | ((val >> 8) & 0xff));
  hoox_arm_writer_put_add_reg_reg_imm (self, dst_reg, dst_reg,
      val & 0xff);
}

void
hoox_arm_writer_put_add_reg_reg_imm (HooxArmWriter * self,
                                    hx_arm_reg dst_reg,
                                    hx_arm_reg src_reg,
                                    hx_uint32 imm_val)
{
  HooxArmRegInfo rd, rs;
  hx_boolean is_noop;

  hoox_arm_reg_describe (dst_reg, &rd);
  hoox_arm_reg_describe (src_reg, &rs);

  is_noop = dst_reg == src_reg && (imm_val & HOOX_INT8_MASK) == 0;
  if (is_noop)
    return;

  hoox_arm_writer_put_instruction (self, 0xe2800000 | (rd.index << 12) |
      (rs.index << 16) | (imm_val & HOOX_INT12_MASK));
}

void
hoox_arm_writer_put_add_reg_reg_reg (HooxArmWriter * self,
                                    hx_arm_reg dst_reg,
                                    hx_arm_reg src_reg1,
                                    hx_arm_reg src_reg2)
{
  HooxArmRegInfo rd, rs1, rs2;

  hoox_arm_reg_describe (dst_reg, &rd);
  hoox_arm_reg_describe (src_reg1, &rs1);
  hoox_arm_reg_describe (src_reg2, &rs2);

  hoox_arm_writer_put_instruction (self, 0xe0800000 | (rd.index << 12) |
      (rs1.index << 16) | rs2.index);
}

void
hoox_arm_writer_put_add_reg_reg_reg_shift (HooxArmWriter * self,
                                          hx_arm_reg dst_reg,
                                          hx_arm_reg src_reg1,
                                          hx_arm_reg src_reg2,
                                          hx_arm_shifter shift,
                                          hx_uint16 shift_value)
{
  HooxArmRegInfo rd, rs1, rs2;

  hoox_arm_reg_describe (dst_reg, &rd);
  hoox_arm_reg_describe (src_reg1, &rs1);
  hoox_arm_reg_describe (src_reg2, &rs2);

  hoox_arm_writer_put_instruction (self, 0xe0800000 | (rd.index << 12) |
      (rs1.index << 16) | ((shift_value & 0x1f) << 7) |
      hoox_arm_shiftify (shift) | rs2.index);
}

void
hoox_arm_writer_put_sub_reg_u16 (HooxArmWriter * self,
                                hx_arm_reg dst_reg,
                                hx_uint16 val)
{
  hoox_arm_writer_put_sub_reg_reg_imm (self, dst_reg, dst_reg,
      0xc00 | ((val >> 8) & 0xff));
  hoox_arm_writer_put_sub_reg_reg_imm (self, dst_reg, dst_reg,
      val & 0xff);
}

void
hoox_arm_writer_put_sub_reg_u32 (HooxArmWriter * self,
                                hx_arm_reg dst_reg,
                                hx_uint32 val)
{
  hoox_arm_writer_put_sub_reg_reg_imm (self, dst_reg, dst_reg,
      0x400 | ((val >> 24) & 0xff));
  hoox_arm_writer_put_sub_reg_reg_imm (self, dst_reg, dst_reg,
      0x800 | ((val >> 16) & 0xff));
  hoox_arm_writer_put_sub_reg_reg_imm (self, dst_reg, dst_reg,
      0xc00 | ((val >> 8) & 0xff));
  hoox_arm_writer_put_sub_reg_reg_imm (self, dst_reg, dst_reg,
      val & 0xff);
}

void
hoox_arm_writer_put_sub_reg_reg_imm (HooxArmWriter * self,
                                    hx_arm_reg dst_reg,
                                    hx_arm_reg src_reg,
                                    hx_uint32 imm_val)
{
  HooxArmRegInfo rd, rs;
  hx_boolean is_noop;

  hoox_arm_reg_describe (dst_reg, &rd);
  hoox_arm_reg_describe (src_reg, &rs);

  is_noop = dst_reg == src_reg && (imm_val & HOOX_INT8_MASK) == 0;
  if (is_noop)
    return;

  hoox_arm_writer_put_instruction (self, 0xe2400000 | (rd.index << 12) |
      (rs.index << 16) | (imm_val & HOOX_INT12_MASK));
}

void
hoox_arm_writer_put_sub_reg_reg_reg (HooxArmWriter * self,
                                    hx_arm_reg dst_reg,
                                    hx_arm_reg src_reg1,
                                    hx_arm_reg src_reg2)
{
  HooxArmRegInfo rd, rs1, rs2;

  hoox_arm_reg_describe (dst_reg, &rd);
  hoox_arm_reg_describe (src_reg1, &rs1);
  hoox_arm_reg_describe (src_reg2, &rs2);

  hoox_arm_writer_put_instruction (self, 0xe0400000 | (rd.index << 12) |
      (rs1.index << 16) | rs2.index);
}

void
hoox_arm_writer_put_rsb_reg_reg_imm (HooxArmWriter * self,
                                    hx_arm_reg dst_reg,
                                    hx_arm_reg src_reg,
                                    hx_uint32 imm_val)
{
  HooxArmRegInfo rd, rs;

  hoox_arm_reg_describe (dst_reg, &rd);
  hoox_arm_reg_describe (src_reg, &rs);

  hoox_arm_writer_put_instruction (self, 0xe2600000 | (rd.index << 12) |
      (rs.index << 16) | (imm_val & HOOX_INT12_MASK));
}

void
hoox_arm_writer_put_ands_reg_reg_imm (HooxArmWriter * self,
                                     hx_arm_reg dst_reg,
                                     hx_arm_reg src_reg,
                                     hx_uint32 imm_val)
{
  HooxArmRegInfo rd, rs;

  hoox_arm_reg_describe (dst_reg, &rd);
  hoox_arm_reg_describe (src_reg, &rs);

  hoox_arm_writer_put_instruction (self, 0xe2100000 | (rd.index << 12) |
      (rs.index << 16) | (imm_val & HOOX_INT8_MASK));
}

void
hoox_arm_writer_put_cmp_reg_imm (HooxArmWriter * self,
                                hx_arm_reg dst_reg,
                                hx_uint32 imm_val)
{
  HooxArmRegInfo rd;

  hoox_arm_reg_describe (dst_reg, &rd);

  hoox_arm_writer_put_instruction (self, 0xe3500000 | (rd.index << 16) |
      imm_val);
}

void
hoox_arm_writer_put_nop (HooxArmWriter * self)
{
  hoox_arm_writer_put_instruction (self, 0xe1a00000);
}

void
hoox_arm_writer_put_breakpoint (HooxArmWriter * self)
{
  switch (self->target_os)
  {
    case HOOX_OS_LINUX:
    case HOOX_OS_ANDROID:
    default: /* TODO: handle other OSes */
      hoox_arm_writer_put_brk_imm (self, 0x10);
      break;
  }
}

void
hoox_arm_writer_put_brk_imm (HooxArmWriter * self,
                            hx_uint16 imm)
{
  hoox_arm_writer_put_instruction (self, 0xe7f000f0 |
      ((imm >> 4) << 8) | (imm & 0xf));
}

void
hoox_arm_writer_put_instruction (HooxArmWriter * self,
                                hx_uint32 insn)
{
  *self->code++ = HX_UINT32_TO_LE (insn);
  self->pc += 4;

  hoox_arm_writer_maybe_commit_literals (self);
}

hx_boolean
hoox_arm_writer_put_bytes (HooxArmWriter * self,
                          const hx_uint8 * data,
                          hx_uint n)
{
  if (n % 4 != 0)
    return FALSE;

  memcpy (self->code, data, n);
  self->code += n / sizeof (hx_uint32);
  self->pc += n;

  hoox_arm_writer_maybe_commit_literals (self);

  return TRUE;
}

static hx_boolean
hoox_arm_writer_try_commit_label_refs (HooxArmWriter * self)
{
  hx_uint num_refs, ref_index;

  if (!hoox_arm_writer_has_label_refs (self))
    return TRUE;

  if (!hoox_arm_writer_has_label_defs (self))
    return FALSE;

  num_refs = self->label_refs.length;

  for (ref_index = 0; ref_index != num_refs; ref_index++)
  {
    HooxArmLabelRef * r;
    const hx_uint32 * target_insn;
    hx_ssize distance;
    hx_uint32 insn;

    r = hoox_metal_array_element_at (&self->label_refs, ref_index);

    target_insn = hoox_metal_hash_table_lookup (self->label_defs, r->id);
    if (target_insn == NULL)
      return FALSE;

    distance = target_insn - (r->insn + 2);
    if (!HOOX_IS_WITHIN_INT24_RANGE (distance))
      return FALSE;

    insn = HX_UINT32_FROM_LE (*r->insn);
    insn |= distance & HOOX_INT24_MASK;
    *r->insn = HX_UINT32_TO_LE (insn);
  }

  hoox_metal_array_remove_all (&self->label_refs);

  return TRUE;
}

static void
hoox_arm_writer_maybe_commit_literals (HooxArmWriter * self)
{
  hx_size space_used;
  hx_constpointer after_literals = self->code;

  if (self->earliest_literal_insn == NULL)
    return;

  space_used = (self->code - self->earliest_literal_insn) * sizeof (hx_uint32);
  space_used += self->literal_refs.length * sizeof (hx_uint32);
  if (space_used <= 4096)
    return;

  self->earliest_literal_insn = NULL;

  hoox_arm_writer_put_b_label (self, after_literals);
  hoox_arm_writer_commit_literals (self);
  hoox_arm_writer_put_label (self, after_literals);
}

static void
hoox_arm_writer_commit_literals (HooxArmWriter * self)
{
  hx_uint num_refs, ref_index;
  hx_uint32 * first_slot, * last_slot;

  if (!hoox_arm_writer_has_literal_refs (self))
    return;

  num_refs = self->literal_refs.length;
  if (num_refs == 0)
    return;

  first_slot = self->code;
  last_slot = first_slot;

  for (ref_index = 0; ref_index != num_refs; ref_index++)
  {
    HooxArmLiteralRef * r;
    hx_uint32 * cur_slot;
    hx_int64 distance_in_words;
    hx_uint32 insn;

    r = hoox_metal_array_element_at (&self->literal_refs, ref_index);

    for (cur_slot = first_slot; cur_slot != last_slot; cur_slot++)
    {
      if (*cur_slot == r->val)
        break;
    }

    if (cur_slot == last_slot)
    {
      *cur_slot = r->val;
      last_slot++;
    }

    distance_in_words = cur_slot - (r->insn + 2);

    insn = HX_UINT32_FROM_LE (*r->insn);
    insn |= ABS (distance_in_words) * 4;
    if (distance_in_words >= 0)
      insn |= 1 << 23;
    *r->insn = HX_UINT32_TO_LE (insn);
  }

  self->code = last_slot;
  self->pc += (hx_uint8 *) last_slot - (hx_uint8 *) first_slot;

  hoox_metal_array_remove_all (&self->literal_refs);
}

static hx_uint32
hoox_arm_condify (hx_arm_cc cc)
{
  return (cc - 1) << 28;
}

static hx_uint32
hoox_arm_shiftify (hx_arm_shifter shifter)
{
  hx_uint32 code = 0;

  switch (shifter)
  {
    case HX_ARM_SFT_INVALID:
    case HX_ARM_SFT_LSL:
      code = 0;
      break;
    case HX_ARM_SFT_LSR:
      code = 1;
      break;
    case HX_ARM_SFT_ASR:
      code = 2;
      break;
    case HX_ARM_SFT_ROR:
      code = 3;
      break;
    default:
      hx_assert_not_reached ();
      break;
  }

  return code << 5;
}
