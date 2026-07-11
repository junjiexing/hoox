/*
 * Copyright (C) 2010-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C)      2019 Jon Wilson <jonwilson@zepler.net>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxthumbwriter.h"

#include "hooxarmreg.h"
#include "hooxlibc.h"
#include "hooxmemory.h"
#include "hooxprocess.h"

typedef hx_uint HooxThumbLabelRefType;
typedef struct _HooxThumbLabelRef HooxThumbLabelRef;
typedef struct _HooxThumbLiteralRef HooxThumbLiteralRef;
typedef hx_uint HooxThumbMemoryOperation;

enum _HooxThumbLabelRefType
{
  HOOX_THUMB_B_T1,
  HOOX_THUMB_B_T2,
  HOOX_THUMB_B_T3,
  HOOX_THUMB_B_T4,
  HOOX_THUMB_BL_T1,
  HOOX_THUMB_CBZ_T1,
  HOOX_THUMB_CBNZ_T1,
};

struct _HooxThumbLabelRef
{
  hx_constpointer id;
  HooxThumbLabelRefType type;
  hx_uint16 * insn;
};

struct _HooxThumbLiteralRef
{
  hx_uint32 val;
  hx_uint16 * insn;
  HooxAddress pc;
};

enum _HooxThumbMemoryOperation
{
  HOOX_THUMB_MEMORY_LOAD,
  HOOX_THUMB_MEMORY_STORE
};

static void hoox_thumb_writer_reset_refs (HooxThumbWriter * self);

static void hoox_thumb_writer_put_argument_list_setup (HooxThumbWriter * self,
    hx_uint n_args, const HooxArgument * args);
static void hoox_thumb_writer_put_argument_list_setup_va (HooxThumbWriter * self,
    hx_uint n_args, va_list args);
static void hoox_thumb_writer_put_argument_list_teardown (HooxThumbWriter * self,
    hx_uint n_args);
static void hoox_thumb_writer_put_branch_imm (HooxThumbWriter * self,
    HooxAddress target, hx_boolean link, hx_boolean thumb);
static hx_boolean hoox_thumb_writer_put_push_or_pop_regs (HooxThumbWriter * self,
    hx_uint16 narrow_template, hx_uint16 wide_template, HooxArmMetaReg special_reg,
    hx_uint n_regs, const hx_arm_reg * regs);
static hx_boolean hoox_thumb_writer_put_push_or_pop_regs_va (HooxThumbWriter * self,
    hx_uint16 narrow_template, hx_uint16 wide_template, HooxArmMetaReg special_reg,
    hx_uint n_regs, hx_arm_reg first_reg, va_list args);
static hx_boolean hoox_thumb_writer_put_vector_push_or_pop_range (
    HooxThumbWriter * self, hx_uint16 upper_template, hx_arm_reg first_reg,
    hx_arm_reg last_reg);
static hx_boolean hoox_thumb_writer_put_transfer_reg_reg_offset (
    HooxThumbWriter * self, HooxThumbMemoryOperation operation, hx_arm_reg left_reg,
    hx_arm_reg right_reg, hx_size right_offset);
static void hoox_thumb_writer_put_it_al (HooxThumbWriter * self);

static hx_boolean hoox_thumb_writer_try_commit_label_refs (HooxThumbWriter * self);
static hx_boolean hoox_thumb_writer_do_commit_label (HooxThumbLabelRef * r,
    const hx_uint16 * target_insn);
static void hoox_thumb_writer_maybe_commit_literals (HooxThumbWriter * self);
static void hoox_thumb_writer_commit_literals (HooxThumbWriter * self);

static hx_boolean hoox_instruction_is_t1_load (hx_uint16 instruction);

HooxThumbWriter *
hoox_thumb_writer_new (hx_pointer code_address)
{
  HooxThumbWriter * writer;

  writer = hx_slice_new (HooxThumbWriter);

  hoox_thumb_writer_init (writer, code_address);

  return writer;
}

HooxThumbWriter *
hoox_thumb_writer_ref (HooxThumbWriter * writer)
{
  hx_atomic_int_inc (&writer->ref_count);

  return writer;
}

void
hoox_thumb_writer_unref (HooxThumbWriter * writer)
{
  if (hx_atomic_int_dec_and_test (&writer->ref_count))
  {
    hoox_thumb_writer_clear (writer);

    hx_slice_free (HooxThumbWriter, writer);
  }
}

void
hoox_thumb_writer_init (HooxThumbWriter * writer,
                       hx_pointer code_address)
{
  writer->ref_count = 1;
  writer->flush_on_destroy = TRUE;

  writer->target_os = hoox_process_get_native_os ();

  writer->label_defs = NULL;
  writer->label_refs.data = NULL;
  writer->literal_refs.data = NULL;

  hoox_thumb_writer_reset (writer, code_address);
}

static hx_boolean
hoox_thumb_writer_has_label_defs (HooxThumbWriter * self)
{
  return self->label_defs != NULL;
}

static hx_boolean
hoox_thumb_writer_has_label_refs (HooxThumbWriter * self)
{
  return self->label_refs.data != NULL;
}

static hx_boolean
hoox_thumb_writer_has_literal_refs (HooxThumbWriter * self)
{
  return self->literal_refs.data != NULL;
}

void
hoox_thumb_writer_clear (HooxThumbWriter * writer)
{
  if (writer->flush_on_destroy)
    hoox_thumb_writer_flush (writer);

  if (hoox_thumb_writer_has_label_defs (writer))
    hoox_metal_hash_table_unref (writer->label_defs);

  if (hoox_thumb_writer_has_label_refs (writer))
    hoox_metal_array_free (&writer->label_refs);

  if (hoox_thumb_writer_has_literal_refs (writer))
    hoox_metal_array_free (&writer->literal_refs);
}

void
hoox_thumb_writer_reset (HooxThumbWriter * writer,
                        hx_pointer code_address)
{
  writer->base = code_address;
  writer->code = code_address;
  writer->pc = HOOX_ADDRESS (code_address);

  if (hoox_thumb_writer_has_label_defs (writer))
    hoox_metal_hash_table_remove_all (writer->label_defs);

  hoox_thumb_writer_reset_refs (writer);
}

static void
hoox_thumb_writer_reset_refs (HooxThumbWriter * self)
{
  if (hoox_thumb_writer_has_label_refs (self))
    hoox_metal_array_remove_all (&self->label_refs);

  if (hoox_thumb_writer_has_literal_refs (self))
    hoox_metal_array_remove_all (&self->literal_refs);

  self->earliest_literal_insn = NULL;
}

void
hoox_thumb_writer_set_target_os (HooxThumbWriter * self,
                                HooxOS os)
{
  self->target_os = os;
}

hx_pointer
hoox_thumb_writer_cur (HooxThumbWriter * self)
{
  return self->code;
}

hx_uint
hoox_thumb_writer_offset (HooxThumbWriter * self)
{
  return (hx_uint) (self->code - self->base) * sizeof (hx_uint16);
}

void
hoox_thumb_writer_skip (HooxThumbWriter * self,
                       hx_uint n_bytes)
{
  self->code = (hx_uint16 *) (((hx_uint8 *) self->code) + n_bytes);
  self->pc += n_bytes;
}

hx_boolean
hoox_thumb_writer_flush (HooxThumbWriter * self)
{
  if (!hoox_thumb_writer_try_commit_label_refs (self))
    goto error;

  hoox_thumb_writer_commit_literals (self);

  return TRUE;

error:
  {
    hoox_thumb_writer_reset_refs (self);

    return FALSE;
  }
}

hx_boolean
hoox_thumb_writer_put_label (HooxThumbWriter * self,
                            hx_constpointer id)
{
  if (!hoox_thumb_writer_has_label_defs (self))
    self->label_defs = hoox_metal_hash_table_new (NULL, NULL);

  if (hoox_metal_hash_table_lookup (self->label_defs, id) != NULL)
    return FALSE;

  hoox_metal_hash_table_insert (self->label_defs, (hx_pointer) id, self->code);

  return TRUE;
}

hx_boolean
hoox_thumb_writer_commit_label (HooxThumbWriter * self,
                               hx_constpointer id)
{
  hx_uint num_refs, ref_index;

  if (!hoox_thumb_writer_has_label_refs (self))
    return FALSE;

  if (!hoox_thumb_writer_has_label_defs (self))
    return FALSE;

  num_refs = self->label_refs.length;

  for (ref_index = 0; ref_index != num_refs; ref_index++)
  {
    HooxThumbLabelRef * r;
    const hx_uint16 * target_insn;

    r = hoox_metal_array_element_at (&self->label_refs, ref_index);
    if (r->id != id)
      continue;

    target_insn = hoox_metal_hash_table_lookup (self->label_defs, r->id);
    if (target_insn == NULL)
      return FALSE;

    if (!hoox_thumb_writer_do_commit_label (r, target_insn))
      return FALSE;

    hoox_metal_array_remove_at (&self->label_refs, ref_index);

    return TRUE;
  }

  return FALSE;
}

static void
hoox_thumb_writer_add_label_reference_here (HooxThumbWriter * self,
                                           hx_constpointer id,
                                           HooxThumbLabelRefType type)
{
  HooxThumbLabelRef * r;

  if (!hoox_thumb_writer_has_label_refs (self))
    hoox_metal_array_init (&self->label_refs, sizeof (HooxThumbLabelRef));

  r = hoox_metal_array_append (&self->label_refs);
  r->id = id;
  r->type = type;
  r->insn = self->code;
}

static void
hoox_thumb_writer_add_literal_reference_here (HooxThumbWriter * self,
                                             hx_uint32 val)
{
  HooxThumbLiteralRef * r;

  if (!hoox_thumb_writer_has_literal_refs (self))
    hoox_metal_array_init (&self->literal_refs, sizeof (HooxThumbLiteralRef));

  r = hoox_metal_array_append (&self->literal_refs);
  r->val = val;
  r->insn = self->code;
  r->pc = self->pc + 4;

  if (self->earliest_literal_insn == NULL)
    self->earliest_literal_insn = r->insn;
}

void
hoox_thumb_writer_put_call_address_with_arguments (HooxThumbWriter * self,
                                                  HooxAddress func,
                                                  hx_uint n_args,
                                                  ...)
{
  va_list args;

  va_start (args, n_args);
  hoox_thumb_writer_put_argument_list_setup_va (self, n_args, args);
  va_end (args);

  hoox_thumb_writer_put_ldr_reg_address (self, HX_ARM_REG_LR, func);
  hoox_thumb_writer_put_blx_reg (self, HX_ARM_REG_LR);

  hoox_thumb_writer_put_argument_list_teardown (self, n_args);
}

void
hoox_thumb_writer_put_call_address_with_arguments_array (
    HooxThumbWriter * self,
    HooxAddress func,
    hx_uint n_args,
    const HooxArgument * args)
{
  hoox_thumb_writer_put_argument_list_setup (self, n_args, args);

  hoox_thumb_writer_put_ldr_reg_address (self, HX_ARM_REG_LR, func);
  hoox_thumb_writer_put_blx_reg (self, HX_ARM_REG_LR);

  hoox_thumb_writer_put_argument_list_teardown (self, n_args);
}

void
hoox_thumb_writer_put_call_reg_with_arguments (HooxThumbWriter * self,
                                              hx_arm_reg reg,
                                              hx_uint n_args,
                                              ...)
{
  va_list args;

  va_start (args, n_args);
  hoox_thumb_writer_put_argument_list_setup_va (self, n_args, args);
  va_end (args);

  hoox_thumb_writer_put_blx_reg (self, reg);

  hoox_thumb_writer_put_argument_list_teardown (self, n_args);
}

void
hoox_thumb_writer_put_call_reg_with_arguments_array (HooxThumbWriter * self,
                                                    hx_arm_reg reg,
                                                    hx_uint n_args,
                                                    const HooxArgument * args)
{
  hoox_thumb_writer_put_argument_list_setup (self, n_args, args);

  hoox_thumb_writer_put_blx_reg (self, reg);

  hoox_thumb_writer_put_argument_list_teardown (self, n_args);
}

static void
hoox_thumb_writer_put_argument_list_setup (HooxThumbWriter * self,
                                          hx_uint n_args,
                                          const HooxArgument * args)
{
  hx_uint n_stack_args;
  hx_int arg_index;

  n_stack_args = MAX ((hx_int) n_args - 4, 0);
  if (n_stack_args % 2 != 0)
    hoox_thumb_writer_put_sub_reg_imm (self, HX_ARM_REG_SP, 4);

  for (arg_index = (hx_int) n_args - 1; arg_index >= 0; arg_index--)
  {
    const HooxArgument * arg = &args[arg_index];
    hx_arm_reg r = HX_ARM_REG_R0 + arg_index;

    if (arg_index < 4)
    {
      if (arg->type == HOOX_ARG_ADDRESS)
      {
        hoox_thumb_writer_put_ldr_reg_address (self, r, arg->value.address);
      }
      else
      {
        if (arg->value.reg != r)
          hoox_thumb_writer_put_mov_reg_reg (self, r, arg->value.reg);
      }
    }
    else
    {
      if (arg->type == HOOX_ARG_ADDRESS)
      {
        hoox_thumb_writer_put_ldr_reg_address (self, HX_ARM_REG_R0,
            arg->value.address);
        hoox_thumb_writer_put_push_regs (self, 1, HX_ARM_REG_R0);
      }
      else
      {
        hoox_thumb_writer_put_push_regs (self, 1, arg->value.reg);
      }
    }
  }
}

static void
hoox_thumb_writer_put_argument_list_setup_va (HooxThumbWriter * self,
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

  hoox_thumb_writer_put_argument_list_setup (self, n_args, arg_values);
}

static void
hoox_thumb_writer_put_argument_list_teardown (HooxThumbWriter * self,
                                             hx_uint n_args)
{
  hx_uint n_stack_args, n_stack_slots;

  n_stack_args = MAX ((hx_int) n_args - 4, 0);
  if (n_stack_args == 0)
    return;

  n_stack_slots = n_stack_args;
  if (n_stack_slots % 2 != 0)
    n_stack_slots++;

  hoox_thumb_writer_put_add_reg_imm (self, HX_ARM_REG_SP, n_stack_slots * 4);
}

void
hoox_thumb_writer_put_branch_address (HooxThumbWriter * self,
                                     HooxAddress address)
{
  if (hoox_thumb_writer_can_branch_directly_between (self, self->pc, address))
  {
    hoox_thumb_writer_put_b_imm (self, address);
  }
  else
  {
    hoox_thumb_writer_put_push_regs (self, 2, HX_ARM_REG_R0, HX_ARM_REG_R1);
    hoox_thumb_writer_put_ldr_reg_address (self, HX_ARM_REG_R0, address | 1);
    hoox_thumb_writer_put_str_reg_reg_offset (self, HX_ARM_REG_R0, HX_ARM_REG_SP, 4);
    hoox_thumb_writer_put_pop_regs (self, 2, HX_ARM_REG_R0, HX_ARM_REG_PC);
  }
}

hx_boolean
hoox_thumb_writer_can_branch_directly_between (HooxThumbWriter * self,
                                              HooxAddress from,
                                              HooxAddress to)
{
  hx_int64 distance = (hx_int64) to - (hx_int64) from;

  return HOOX_IS_WITHIN_INT24_RANGE (distance);
}

void
hoox_thumb_writer_put_b_imm (HooxThumbWriter * self,
                            HooxAddress target)
{
  hoox_thumb_writer_put_branch_imm (self, target, FALSE, TRUE);
}

void
hoox_thumb_writer_put_b_label (HooxThumbWriter * self,
                              hx_constpointer label_id)
{
  hoox_thumb_writer_add_label_reference_here (self, label_id, HOOX_THUMB_B_T2);
  hoox_thumb_writer_put_instruction (self, 0xe000);
}

void
hoox_thumb_writer_put_b_label_wide (HooxThumbWriter * self,
                                   hx_constpointer label_id)
{
  hoox_thumb_writer_add_label_reference_here (self, label_id, HOOX_THUMB_B_T4);
  hoox_thumb_writer_put_branch_imm (self, 0, FALSE, TRUE);
}

void
hoox_thumb_writer_put_bx_reg (HooxThumbWriter * self,
                             hx_arm_reg reg)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_thumb_writer_put_instruction (self, 0x4700 | (ri.index << 3));
}

void
hoox_thumb_writer_put_bl_imm (HooxThumbWriter * self,
                             HooxAddress target)
{
  hoox_thumb_writer_put_branch_imm (self, target, TRUE, TRUE);
}

void
hoox_thumb_writer_put_bl_label (HooxThumbWriter * self,
                               hx_constpointer label_id)
{
  hoox_thumb_writer_add_label_reference_here (self, label_id, HOOX_THUMB_BL_T1);
  hoox_thumb_writer_put_branch_imm (self, 0, TRUE, TRUE);
}

void
hoox_thumb_writer_put_blx_imm (HooxThumbWriter * self,
                              HooxAddress target)
{
  hoox_thumb_writer_put_branch_imm (self, target, TRUE, FALSE);
}

void
hoox_thumb_writer_put_blx_reg (HooxThumbWriter * self,
                              hx_arm_reg reg)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_thumb_writer_put_instruction (self, 0x4780 | (ri.index << 3));
}

static void
hoox_thumb_writer_put_branch_imm (HooxThumbWriter * self,
                                 HooxAddress target,
                                 hx_boolean link,
                                 hx_boolean thumb)
{
  hx_uint16 s, j1, j2, imm10, imm11;

  if (target != 0)
  {
    union
    {
      hx_int32 i;
      hx_uint32 u;
    } distance;
    hx_uint16 i1, i2;

    distance.i = ((hx_int32) (target & ~((HooxAddress) 1)) -
        (hx_int32) (self->pc + 4)) / 2;

    s =  (distance.u >> 23) & 1;
    i1 = (distance.u >> 22) & 1;
    i2 = (distance.u >> 21) & 1;
    j1 = (i1 ^ 1) ^ s;
    j2 = (i2 ^ 1) ^ s;

    imm10 = (distance.u >> 11) & HOOX_INT10_MASK;
    imm11 =  distance.u        & HOOX_INT11_MASK;
  }
  else
  {
    s = 0;
    j1 = 0;
    j2 = 0;
    imm10 = 0;
    imm11 = 0;
  }

  hoox_thumb_writer_put_instruction_wide (self,
      0xf000 | (s << 10) | imm10,
      0x8000 | (link << 14) | (j1 << 13) | (thumb << 12) | (j2 << 11) | imm11);
}

void
hoox_thumb_writer_put_cmp_reg_imm (HooxThumbWriter * self,
                                  hx_arm_reg reg,
                                  hx_uint8 imm_value)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_thumb_writer_put_instruction (self, 0x2800 | (ri.index << 8) | imm_value);
}

void
hoox_thumb_writer_put_beq_label (HooxThumbWriter * self,
                                hx_constpointer label_id)
{
  hoox_thumb_writer_put_b_cond_label (self, HX_ARM_CC_EQ, label_id);
}

void
hoox_thumb_writer_put_bne_label (HooxThumbWriter * self,
                                hx_constpointer label_id)
{
  hoox_thumb_writer_put_b_cond_label (self, HX_ARM_CC_NE, label_id);
}

void
hoox_thumb_writer_put_b_cond_label (HooxThumbWriter * self,
                                   hx_arm_cc cc,
                                   hx_constpointer label_id)
{
  hoox_thumb_writer_add_label_reference_here (self, label_id, HOOX_THUMB_B_T1);
  hoox_thumb_writer_put_instruction (self, 0xd000 | ((cc - 1) << 8));
}

void
hoox_thumb_writer_put_b_cond_label_wide (HooxThumbWriter * self,
                                        hx_arm_cc cc,
                                        hx_constpointer label_id)
{
  hoox_thumb_writer_add_label_reference_here (self, label_id, HOOX_THUMB_B_T3);
  hoox_thumb_writer_put_instruction_wide (self,
      0xf000 | ((cc - 1) << 6),
      0x8000);
}

void
hoox_thumb_writer_put_cbz_reg_label (HooxThumbWriter * self,
                                    hx_arm_reg reg,
                                    hx_constpointer label_id)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_thumb_writer_add_label_reference_here (self, label_id, HOOX_THUMB_CBZ_T1);
  hoox_thumb_writer_put_instruction (self, 0xb100 | ri.index);
}

void
hoox_thumb_writer_put_cbnz_reg_label (HooxThumbWriter * self,
                                     hx_arm_reg reg,
                                     hx_constpointer label_id)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_thumb_writer_add_label_reference_here (self, label_id, HOOX_THUMB_CBNZ_T1);
  hoox_thumb_writer_put_instruction (self, 0xb900 | ri.index);
}

hx_boolean
hoox_thumb_writer_put_push_regs (HooxThumbWriter * self,
                                hx_uint n_regs,
                                hx_arm_reg first_reg,
                                ...)
{
  hx_boolean success;
  va_list args;

  va_start (args, first_reg);
  success = hoox_thumb_writer_put_push_or_pop_regs_va (self, 0xb400, 0xe92d,
      HOOX_ARM_MREG_LR, n_regs, first_reg, args);
  va_end (args);

  return success;
}

hx_boolean
hoox_thumb_writer_put_push_regs_array (HooxThumbWriter * self,
                                      hx_uint n_regs,
                                      const hx_arm_reg * regs)
{
  return hoox_thumb_writer_put_push_or_pop_regs (self, 0xb400, 0xe92d,
      HOOX_ARM_MREG_LR, n_regs, regs);
}

hx_boolean
hoox_thumb_writer_put_pop_regs (HooxThumbWriter * self,
                               hx_uint n_regs,
                               hx_arm_reg first_reg,
                               ...)
{
  hx_boolean success;
  va_list args;

  va_start (args, first_reg);
  success = hoox_thumb_writer_put_push_or_pop_regs_va (self, 0xbc00, 0xe8bd,
      HOOX_ARM_MREG_PC, n_regs, first_reg, args);
  va_end (args);

  return success;
}

hx_boolean
hoox_thumb_writer_put_pop_regs_array (HooxThumbWriter * self,
                                     hx_uint n_regs,
                                     const hx_arm_reg * regs)
{
  return hoox_thumb_writer_put_push_or_pop_regs (self, 0xbc00, 0xe8bd,
      HOOX_ARM_MREG_PC, n_regs, regs);
}

static hx_boolean
hoox_thumb_writer_put_push_or_pop_regs (HooxThumbWriter * self,
                                       hx_uint16 narrow_template,
                                       hx_uint16 wide_template,
                                       HooxArmMetaReg special_reg,
                                       hx_uint n_regs,
                                       const hx_arm_reg * regs)
{
  HooxArmRegInfo * items;
  hx_boolean need_wide_instruction;
  hx_uint reg_index;

  if (n_regs == 0)
    return FALSE;

  items = hx_newa (HooxArmRegInfo, n_regs);
  need_wide_instruction = FALSE;
  for (reg_index = 0; reg_index != n_regs; reg_index++)
  {
    HooxArmRegInfo * ri = &items[reg_index];
    hx_boolean is_low_reg;

    hoox_arm_reg_describe (regs[reg_index], ri);

    is_low_reg = (ri->meta >= HOOX_ARM_MREG_R0 && ri->meta <= HOOX_ARM_MREG_R7);
    if (!is_low_reg && ri->meta != special_reg)
      need_wide_instruction = TRUE;
  }

  if (need_wide_instruction)
  {
    hx_uint16 mask = 0;

    hoox_thumb_writer_put_instruction (self, wide_template);

    for (reg_index = 0; reg_index != n_regs; reg_index++)
    {
      const HooxArmRegInfo * ri = &items[reg_index];

      mask |= (1 << ri->index);
    }

    hoox_thumb_writer_put_instruction (self, mask);
  }
  else
  {
    hx_uint16 insn = narrow_template;

    for (reg_index = 0; reg_index != n_regs; reg_index++)
    {
      const HooxArmRegInfo * ri = &items[reg_index];

      if (ri->meta == special_reg)
        insn |= 0x0100;
      else
        insn |= (1 << ri->index);
    }

    hoox_thumb_writer_put_instruction (self, insn);
  }

  return TRUE;
}

static hx_boolean
hoox_thumb_writer_put_push_or_pop_regs_va (HooxThumbWriter * self,
                                          hx_uint16 narrow_template,
                                          hx_uint16 wide_template,
                                          HooxArmMetaReg special_reg,
                                          hx_uint n_regs,
                                          hx_arm_reg first_reg,
                                          va_list args)
{
  hx_arm_reg * regs;
  hx_uint reg_index;

  hx_assert (n_regs != 0);

  regs = hx_newa (hx_arm_reg, n_regs);

  for (reg_index = 0; reg_index != n_regs; reg_index++)
  {
    regs[reg_index] = (reg_index == 0) ? first_reg : va_arg (args, hx_arm_reg);
  }

  return hoox_thumb_writer_put_push_or_pop_regs (self, narrow_template,
      wide_template, special_reg, n_regs, regs);
}

hx_boolean
hoox_thumb_writer_put_vpush_range (HooxThumbWriter * self,
                                  hx_arm_reg first_reg,
                                  hx_arm_reg last_reg)
{
  return hoox_thumb_writer_put_vector_push_or_pop_range (self, 0xed2d, first_reg,
      last_reg);
}

hx_boolean
hoox_thumb_writer_put_vpop_range (HooxThumbWriter * self,
                                 hx_arm_reg first_reg,
                                 hx_arm_reg last_reg)
{
  return hoox_thumb_writer_put_vector_push_or_pop_range (self, 0xecbd, first_reg,
      last_reg);
}

static hx_boolean
hoox_thumb_writer_put_vector_push_or_pop_range (HooxThumbWriter * self,
                                               hx_uint16 upper_template,
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

  hoox_thumb_writer_put_instruction_wide (self,
      upper_template | ((rf.index >> 4) << 6) |
      ((rf.index & HOOX_INT4_MASK) << 12),
      0x0a00 | ((rf.width == 64) << 8) | imm8);

  return TRUE;
}

hx_boolean
hoox_thumb_writer_put_ldr_reg_address (HooxThumbWriter * self,
                                      hx_arm_reg reg,
                                      HooxAddress address)
{
  return hoox_thumb_writer_put_ldr_reg_u32 (self, reg, (hx_uint32) address);
}

hx_boolean
hoox_thumb_writer_put_ldr_reg_u32 (HooxThumbWriter * self,
                                  hx_arm_reg reg,
                                  hx_uint32 val)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_thumb_writer_add_literal_reference_here (self, val);

  if (ri.meta <= HOOX_ARM_MREG_R7)
  {
    hoox_thumb_writer_put_instruction (self, 0x4800 | (ri.index << 8));
  }
  else
  {
    hx_boolean add = TRUE;

    hoox_thumb_writer_put_instruction_wide (self,
        0xf85f | (add << 7),
        (ri.index << 12));
  }

  return TRUE;
}

void
hoox_thumb_writer_put_ldr_reg_reg (HooxThumbWriter * self,
                                  hx_arm_reg dst_reg,
                                  hx_arm_reg src_reg)
{
  hoox_thumb_writer_put_ldr_reg_reg_offset (self, dst_reg, src_reg, 0);
}

hx_boolean
hoox_thumb_writer_put_ldr_reg_reg_offset (HooxThumbWriter * self,
                                         hx_arm_reg dst_reg,
                                         hx_arm_reg src_reg,
                                         hx_size src_offset)
{
  return hoox_thumb_writer_put_transfer_reg_reg_offset (self,
      HOOX_THUMB_MEMORY_LOAD, dst_reg, src_reg, src_offset);
}

void
hoox_thumb_writer_put_ldrb_reg_reg (HooxThumbWriter * self,
                                   hx_arm_reg dst_reg,
                                   hx_arm_reg src_reg)
{
  HooxArmRegInfo dst, src;

  hoox_arm_reg_describe (dst_reg, &dst);
  hoox_arm_reg_describe (src_reg, &src);

  hoox_thumb_writer_put_instruction (self, 0x7800 | (src.index << 3) |
      dst.index);
}

void
hoox_thumb_writer_put_ldrh_reg_reg (HooxThumbWriter * self,
                                   hx_arm_reg dst_reg,
                                   hx_arm_reg src_reg)
{
  HooxArmRegInfo dst, src;

  hoox_arm_reg_describe (dst_reg, &dst);
  hoox_arm_reg_describe (src_reg, &src);

  hoox_thumb_writer_put_instruction (self, 0x8800 | (src.index << 3) |
      dst.index);
}

hx_boolean
hoox_thumb_writer_put_vldr_reg_reg_offset (HooxThumbWriter * self,
                                          hx_arm_reg dst_reg,
                                          hx_arm_reg src_reg,
                                          hx_ssize src_offset)
{
  HooxArmRegInfo dst, src;
  hx_uint16 u, d, vd, size;
  hx_size abs_src_offset;

  hoox_arm_reg_describe (dst_reg, &dst);
  hoox_arm_reg_describe (src_reg, &src);

  u = src_offset >= 0;

  abs_src_offset = ABS (src_offset) / 4;
  if (abs_src_offset > HX_MAXUINT8)
    return FALSE;

  if (dst.meta >= HOOX_ARM_MREG_S0 && dst.meta <= HOOX_ARM_MREG_S31)
  {
    vd = (dst.index >> 1) & HOOX_INT4_MASK;
    d = dst.index & 1;

    size = 0x2;
  }
  else
  {
    d = (dst.index >> 4) & 1;
    vd = dst.index & HOOX_INT4_MASK;

    size = 0x3;
  }

  hoox_thumb_writer_put_instruction_wide (self,
      0xed10 | (u << 7) | (d << 6) | src.index,
      0x0800 | (vd << 12) | (size << 8) | abs_src_offset);

  return TRUE;
}

void
hoox_thumb_writer_put_ldmia_reg_mask (HooxThumbWriter * self,
                                     hx_arm_reg reg,
                                     hx_uint16 mask)
{
  HooxArmRegInfo ri;
  const hx_uint16 valid_short_reg_mask = 0x80ff;

  hoox_arm_reg_describe (reg, &ri);

  if (reg == HX_ARM_REG_SP && (mask & ~valid_short_reg_mask) == 0)
  {
    const hx_boolean includes_pc = (mask & 0x8000) != 0;

    hoox_thumb_writer_put_instruction (self, 0xbc00 | (includes_pc << 8) |
        (mask & HOOX_INT8_MASK));
  }
  else
  {
    hoox_thumb_writer_put_instruction_wide (self, 0xe8b0 | ri.index, mask);
  }
}

void
hoox_thumb_writer_put_str_reg_reg (HooxThumbWriter * self,
                                  hx_arm_reg src_reg,
                                  hx_arm_reg dst_reg)
{
  hoox_thumb_writer_put_str_reg_reg_offset (self, src_reg, dst_reg, 0);
}

hx_boolean
hoox_thumb_writer_put_str_reg_reg_offset (HooxThumbWriter * self,
                                         hx_arm_reg src_reg,
                                         hx_arm_reg dst_reg,
                                         hx_size dst_offset)
{
  return hoox_thumb_writer_put_transfer_reg_reg_offset (self,
      HOOX_THUMB_MEMORY_STORE, src_reg, dst_reg, dst_offset);
}

static hx_boolean
hoox_thumb_writer_put_transfer_reg_reg_offset (HooxThumbWriter * self,
                                              HooxThumbMemoryOperation operation,
                                              hx_arm_reg left_reg,
                                              hx_arm_reg right_reg,
                                              hx_size right_offset)
{
  HooxArmRegInfo lr, rr;

  hoox_arm_reg_describe (left_reg, &lr);
  hoox_arm_reg_describe (right_reg, &rr);

  if (lr.meta <= HOOX_ARM_MREG_R7 &&
      (rr.meta <= HOOX_ARM_MREG_R7 || rr.meta == HOOX_ARM_MREG_SP) &&
      ((rr.meta == HOOX_ARM_MREG_SP && right_offset <= 1020) ||
       (rr.meta != HOOX_ARM_MREG_SP && right_offset <= 124)) &&
      (right_offset % 4) == 0)
  {
    hx_uint16 insn;

    if (rr.meta == HOOX_ARM_MREG_SP)
      insn = 0x9000 | (lr.index << 8) | (right_offset / 4);
    else
      insn = 0x6000 | (right_offset / 4) << 6 | (rr.index << 3) | lr.index;

    if (operation == HOOX_THUMB_MEMORY_LOAD)
      insn |= 0x0800;

    hoox_thumb_writer_put_instruction (self, insn);
  }
  else
  {
    if (right_offset > 4095)
      return FALSE;

    hoox_thumb_writer_put_instruction_wide (self,
        0xf8c0 | ((operation == HOOX_THUMB_MEMORY_LOAD) ? 0x0010 : 0x0000) |
            rr.index,
        (lr.index << 12) | right_offset);
  }

  return TRUE;
}

void
hoox_thumb_writer_put_mov_reg_reg (HooxThumbWriter * self,
                                  hx_arm_reg dst_reg,
                                  hx_arm_reg src_reg)
{
  HooxArmRegInfo dst, src;
  hx_uint16 insn;

  hoox_arm_reg_describe (dst_reg, &dst);
  hoox_arm_reg_describe (src_reg, &src);

  if (dst.meta <= HOOX_ARM_MREG_R7 && src.meta <= HOOX_ARM_MREG_R7)
  {
    insn = 0x1c00 | (src.index << 3) | dst.index;

    /* Here we emit “ADDS Rd, Rm, #0” so need to suppress flags */
    hoox_thumb_writer_put_it_al (self);
  }
  else
  {
    hx_uint16 dst_is_high;
    hx_uint dst_index;

    if (dst.meta > HOOX_ARM_MREG_R7)
    {
      dst_is_high = 1;
      dst_index = dst.index - HOOX_ARM_MREG_R8;
    }
    else
    {
      dst_is_high = 0;
      dst_index = dst.index;
    }

    insn = 0x4600 | (dst_is_high << 7) | (src.index << 3) | dst_index;
  }

  hoox_thumb_writer_put_instruction (self, insn);
}

void
hoox_thumb_writer_put_mov_reg_u8 (HooxThumbWriter * self,
                                 hx_arm_reg dst_reg,
                                 hx_uint8 imm_value)
{
  HooxArmRegInfo dst;

  hoox_arm_reg_describe (dst_reg, &dst);

  hoox_thumb_writer_put_it_al (self);
  hoox_thumb_writer_put_instruction (self, 0x2000 | (dst.index << 8) |
      imm_value);
}

void
hoox_thumb_writer_put_mov_reg_cpsr (HooxThumbWriter * self,
                                   hx_arm_reg reg)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_thumb_writer_put_instruction (self, 0xf3ef);
  hoox_thumb_writer_put_instruction (self, 0x8000 | ri.index << 8);
}

void
hoox_thumb_writer_put_mov_cpsr_reg (HooxThumbWriter * self,
                                   hx_arm_reg reg)
{
  HooxArmRegInfo ri;

  hoox_arm_reg_describe (reg, &ri);

  hoox_thumb_writer_put_instruction (self, 0xf380 | ri.index);
  hoox_thumb_writer_put_instruction (self, 0x8900);
}

hx_boolean
hoox_thumb_writer_put_add_reg_imm (HooxThumbWriter * self,
                                  hx_arm_reg dst_reg,
                                  hx_ssize imm_value)
{
  HooxArmRegInfo dst;
  hx_uint16 sign_mask, insn;

  hoox_arm_reg_describe (dst_reg, &dst);

  if (dst_reg != HX_ARM_REG_SP && (dst_reg < HX_ARM_REG_R0 || dst_reg > HX_ARM_REG_R7))
    return FALSE;

  sign_mask = 0x0000;
  if (dst.meta == HOOX_ARM_MREG_SP)
  {
    if (imm_value % 4 != 0)
      return FALSE;

    if (imm_value < 0)
      sign_mask = 0x0080;

    insn = 0xb000 | sign_mask | ABS (imm_value / 4);
  }
  else
  {
    if (imm_value < 0)
      sign_mask = 0x0800;

    insn = 0x3000 | sign_mask | (dst.index << 8) | ABS (imm_value);
    hoox_thumb_writer_put_it_al (self);
  }

  hoox_thumb_writer_put_instruction (self, insn);

  return TRUE;
}

void
hoox_thumb_writer_put_add_reg_reg (HooxThumbWriter * self,
                                  hx_arm_reg dst_reg,
                                  hx_arm_reg src_reg)
{
  hoox_thumb_writer_put_add_reg_reg_reg (self, dst_reg, dst_reg, src_reg);
}

void
hoox_thumb_writer_put_add_reg_reg_reg (HooxThumbWriter * self,
                                      hx_arm_reg dst_reg,
                                      hx_arm_reg left_reg,
                                      hx_arm_reg right_reg)
{
  HooxArmRegInfo dst, left, right;
  hx_uint16 insn;

  hoox_arm_reg_describe (dst_reg, &dst);
  hoox_arm_reg_describe (left_reg, &left);
  hoox_arm_reg_describe (right_reg, &right);

  if (left.meta == dst.meta)
  {
    insn = 0x4400;

    if (dst.meta <= HOOX_ARM_MREG_R7)
      insn |= dst.index;
    else
      insn |= 0x0080 | (dst.index - HOOX_ARM_MREG_R8);
    insn |= (right.index << 3);
  }
  else
  {
    insn = 0x1800 | (right.index << 6) | (left.index << 3) | dst.index;
    hoox_thumb_writer_put_it_al (self);
  }

  hoox_thumb_writer_put_instruction (self, insn);
}

hx_boolean
hoox_thumb_writer_put_add_reg_reg_imm (HooxThumbWriter * self,
                                      hx_arm_reg dst_reg,
                                      hx_arm_reg left_reg,
                                      hx_ssize right_value)
{
  HooxArmRegInfo dst, left;
  hx_uint16 insn;

  hoox_arm_reg_describe (dst_reg, &dst);
  hoox_arm_reg_describe (left_reg, &left);

  if (left.meta == dst.meta)
  {
    return hoox_thumb_writer_put_add_reg_imm (self, dst_reg, right_value);
  }

  if (dst_reg < HX_ARM_REG_R0 || dst_reg > HX_ARM_REG_R7)
    return FALSE;

  if (left_reg != HX_ARM_REG_SP && left_reg != HX_ARM_REG_PC &&
      (left_reg < HX_ARM_REG_R0 || left_reg > HX_ARM_REG_R7))
  {
    return FALSE;
  }

  if (left.meta == HOOX_ARM_MREG_SP || left.meta == HOOX_ARM_MREG_PC)
  {
    hx_uint16 base_mask;

    if (right_value < 0 || right_value % 4 != 0)
      return FALSE;

    if (left.meta == HOOX_ARM_MREG_SP)
      base_mask = 0x0800;
    else
      base_mask = 0x0000;

    /* ADR instruction doesn't modify flags */
    insn = 0xa000 | base_mask | (dst.index << 8) | (right_value / 4);
  }
  else
  {
    hx_uint16 sign_mask = 0x0000;

    if (ABS (right_value) > 7)
      return FALSE;

    if (right_value < 0)
      sign_mask = 0x0200;

    insn = 0x1c00 | sign_mask | (ABS (right_value) << 6) | (left.index << 3) |
        dst.index;
    hoox_thumb_writer_put_it_al (self);
  }

  hoox_thumb_writer_put_instruction (self, insn);

  return TRUE;
}

static void
hoox_thumb_writer_put_it_al (HooxThumbWriter * self)
{
  hoox_thumb_writer_put_instruction (self, 0xbfe8);
}

hx_boolean
hoox_thumb_writer_put_sub_reg_imm (HooxThumbWriter * self,
                                  hx_arm_reg dst_reg,
                                  hx_ssize imm_value)
{
  return hoox_thumb_writer_put_add_reg_imm (self, dst_reg, -imm_value);
}

void
hoox_thumb_writer_put_sub_reg_reg (HooxThumbWriter * self,
                                  hx_arm_reg dst_reg,
                                  hx_arm_reg src_reg)
{
  hoox_thumb_writer_put_sub_reg_reg_reg (self, dst_reg, dst_reg, src_reg);
}

void
hoox_thumb_writer_put_sub_reg_reg_reg (HooxThumbWriter * self,
                                      hx_arm_reg dst_reg,
                                      hx_arm_reg left_reg,
                                      hx_arm_reg right_reg)
{
  HooxArmRegInfo dst, left, right;
  hx_uint16 insn;

  hoox_arm_reg_describe (dst_reg, &dst);
  hoox_arm_reg_describe (left_reg, &left);
  hoox_arm_reg_describe (right_reg, &right);

  insn = 0x1a00 | (right.index << 6) | (left.index << 3) | dst.index;

  hoox_thumb_writer_put_it_al (self);
  hoox_thumb_writer_put_instruction (self, insn);
}

hx_boolean
hoox_thumb_writer_put_sub_reg_reg_imm (HooxThumbWriter * self,
                                      hx_arm_reg dst_reg,
                                      hx_arm_reg left_reg,
                                      hx_ssize right_value)
{
  return hoox_thumb_writer_put_add_reg_reg_imm (self, dst_reg, left_reg,
      -right_value);
}

hx_boolean
hoox_thumb_writer_put_and_reg_reg_imm (HooxThumbWriter * self,
                                      hx_arm_reg dst_reg,
                                      hx_arm_reg left_reg,
                                      hx_ssize right_value)
{
  HooxArmRegInfo dst, left;
  hx_uint16 imm8, insn_high, insn_low;

  hoox_arm_reg_describe (dst_reg, &dst);
  hoox_arm_reg_describe (left_reg, &left);

  /*
   * Thumb does allow up to a 12bit immediate, but the encoded form for this is
   * complex and we don't yet need it for our use-cases.
   */
  if (!HOOX_IS_WITHIN_UINT8_RANGE (right_value))
    return FALSE;

  imm8 = right_value & 0xff;
  insn_high = 0xf000 | left.index;
  insn_low = (dst.index << 8) | imm8;

  hoox_thumb_writer_put_instruction_wide (self, insn_high, insn_low);

  return TRUE;
}

hx_boolean
hoox_thumb_writer_put_or_reg_reg_imm (HooxThumbWriter * self,
                                     hx_arm_reg dst_reg,
                                     hx_arm_reg left_reg,
                                     hx_ssize right_value)
{
  HooxArmRegInfo dst, left;
  hx_uint16 imm8, insn_high, insn_low;

  hoox_arm_reg_describe (dst_reg, &dst);
  hoox_arm_reg_describe (left_reg, &left);

  /*
   * Thumb does allow up to a 12bit immediate, but the encoded form for this is
   * complex and we don't yet need it for our use-cases.
   */
  if (!HOOX_IS_WITHIN_UINT8_RANGE (right_value))
    return FALSE;

  imm8 = right_value & 0xff;
  insn_high = 0xf040 | left.index;
  insn_low = (dst.index << 8) | imm8;

  hoox_thumb_writer_put_instruction_wide (self, insn_high, insn_low);

  return TRUE;
}

hx_boolean
hoox_thumb_writer_put_lsl_reg_reg_imm (HooxThumbWriter * self,
                                      hx_arm_reg dst_reg,
                                      hx_arm_reg left_reg,
                                      hx_uint8 right_value)
{
  hoox_thumb_writer_put_it_al (self);

  return hoox_thumb_writer_put_lsls_reg_reg_imm (self, dst_reg, left_reg,
      right_value);
}

hx_boolean
hoox_thumb_writer_put_lsls_reg_reg_imm (HooxThumbWriter * self,
                                       hx_arm_reg dst_reg,
                                       hx_arm_reg left_reg,
                                       hx_uint8 right_value)
{
  HooxArmRegInfo dst, left;

  if (right_value == 0 || right_value > 31)
    return FALSE;

  hoox_arm_reg_describe (dst_reg, &dst);
  hoox_arm_reg_describe (left_reg, &left);

  hoox_thumb_writer_put_instruction (self, 0x0000 | (right_value << 6) |
      (left.index << 3) | dst.index);

  return TRUE;
}

hx_boolean
hoox_thumb_writer_put_lsrs_reg_reg_imm (HooxThumbWriter * self,
                                       hx_arm_reg dst_reg,
                                       hx_arm_reg left_reg,
                                       hx_uint8 right_value)
{
  HooxArmRegInfo dst, left;

  if (right_value == 0 || right_value > 31)
    return FALSE;

  hoox_arm_reg_describe (dst_reg, &dst);
  hoox_arm_reg_describe (left_reg, &left);

  hoox_thumb_writer_put_instruction (self, 0x0800 | (right_value << 6) |
      (left.index << 3) | dst.index);

  return TRUE;
}

hx_boolean
hoox_thumb_writer_put_mrs_reg_reg (HooxThumbWriter * self,
                                  hx_arm_reg dst_reg,
                                  hx_arm_sysreg src_reg)
{
  HooxArmRegInfo dst;

  hoox_arm_reg_describe (dst_reg, &dst);

  if (dst.meta > HOOX_ARM_MREG_R12)
    return FALSE;
  if (src_reg != HX_ARM_SYSREG_APSR_NZCVQ)
    return FALSE;

  hoox_thumb_writer_put_instruction_wide (self,
      0xf3ef,
      0x8000 | (dst.index << 8));

  return TRUE;
}

hx_boolean
hoox_thumb_writer_put_msr_reg_reg (HooxThumbWriter * self,
                                  hx_arm_sysreg dst_reg,
                                  hx_arm_reg src_reg)
{
  HooxArmRegInfo src;

  hoox_arm_reg_describe (src_reg, &src);

  if (dst_reg != HX_ARM_SYSREG_APSR_NZCVQ)
    return FALSE;
  if (src.meta > HOOX_ARM_MREG_R12)
    return FALSE;

  hoox_thumb_writer_put_instruction_wide (self,
      0xf380 | src.index,
      0x8800);

  return TRUE;
}

void
hoox_thumb_writer_put_nop (HooxThumbWriter * self)
{
  hoox_thumb_writer_put_instruction (self, 0xbf00);
}

void
hoox_thumb_writer_put_bkpt_imm (HooxThumbWriter * self,
                               hx_uint8 imm)
{
  hoox_thumb_writer_put_instruction (self, 0xbe00 | imm);
}

void
hoox_thumb_writer_put_breakpoint (HooxThumbWriter * self)
{
  switch (self->target_os)
  {
    case HOOX_OS_LINUX:
    case HOOX_OS_ANDROID:
      hoox_thumb_writer_put_instruction (self, 0xde01);
      break;
    default:
      hoox_thumb_writer_put_bkpt_imm (self, 0);
      hoox_thumb_writer_put_bx_reg (self, HX_ARM_REG_LR);
      break;
  }
}

void
hoox_thumb_writer_put_instruction (HooxThumbWriter * self,
                                  hx_uint16 insn)
{
  *self->code++ = HX_UINT16_TO_LE (insn);
  self->pc += 2;

  hoox_thumb_writer_maybe_commit_literals (self);
}

void
hoox_thumb_writer_put_instruction_wide (HooxThumbWriter * self,
                                       hx_uint16 upper,
                                       hx_uint16 lower)
{
  *self->code++ = HX_UINT16_TO_LE (upper);
  *self->code++ = HX_UINT16_TO_LE (lower);
  self->pc += 4;

  hoox_thumb_writer_maybe_commit_literals (self);
}

hx_boolean
hoox_thumb_writer_put_bytes (HooxThumbWriter * self,
                            const hx_uint8 * data,
                            hx_uint n)
{
  if (n % 2 != 0)
    return FALSE;

  hoox_memcpy (self->code, data, n);
  self->code += n / sizeof (hx_uint16);
  self->pc += n;

  hoox_thumb_writer_maybe_commit_literals (self);

  return TRUE;
}

static hx_boolean
hoox_thumb_writer_try_commit_label_refs (HooxThumbWriter * self)
{
  hx_uint num_refs, ref_index;

  if (!hoox_thumb_writer_has_label_refs (self))
    return TRUE;

  if (!hoox_thumb_writer_has_label_defs (self))
    return FALSE;

  num_refs = self->label_refs.length;

  for (ref_index = 0; ref_index != num_refs; ref_index++)
  {
    HooxThumbLabelRef * r;
    const hx_uint16 * target_insn;

    r = hoox_metal_array_element_at (&self->label_refs, ref_index);

    target_insn = hoox_metal_hash_table_lookup (self->label_defs, r->id);
    if (target_insn == NULL)
      return FALSE;

    if (!hoox_thumb_writer_do_commit_label (r, target_insn))
      return FALSE;
  }

  hoox_metal_array_remove_all (&self->label_refs);

  return TRUE;
}

static hx_boolean
hoox_thumb_writer_do_commit_label (HooxThumbLabelRef * r,
                                  const hx_uint16 * target_insn)
{
  hx_ssize distance;
  hx_uint16 insn;

  distance = target_insn - (r->insn + 2);

  insn = HX_UINT16_FROM_LE (*r->insn);
  switch (r->type)
  {
    case HOOX_THUMB_B_T1:
      if (!HOOX_IS_WITHIN_INT8_RANGE (distance))
        return FALSE;
      insn |= distance & HOOX_INT8_MASK;
      break;
    case HOOX_THUMB_B_T2:
      if (!HOOX_IS_WITHIN_INT11_RANGE (distance))
        return FALSE;
      insn |= distance & HOOX_INT11_MASK;
      break;
    case HOOX_THUMB_B_T3:
    {
      union
      {
        hx_int32 i;
        hx_uint32 u;
      } distance_word;
      hx_uint32 s, j2, j1, imm6, imm11;
      hx_uint16 insn_low;

      if (!HOOX_IS_WITHIN_INT20_RANGE (distance))
        return FALSE;

      insn_low = HX_UINT16_FROM_LE (r->insn[1]);

      distance_word.i = distance;

      s =  (distance_word.u >> 23) & 1;
      j2 = (distance_word.u >> 18) & 1;
      j1 = (distance_word.u >> 17) & 1;
      imm6 = (distance_word.u >> 11) & HOOX_INT6_MASK;
      imm11 = distance_word.u        & HOOX_INT11_MASK;

      insn     |=  (s << 10) | imm6;
      insn_low |= (j1 << 13) | (j2 << 11) | imm11;

      r->insn[1] = HX_UINT16_TO_LE (insn_low);

      break;
    }
    case HOOX_THUMB_B_T4:
    case HOOX_THUMB_BL_T1:
    {
      union
      {
        hx_int32 i;
        hx_uint32 u;
      } distance_word;
      hx_uint16 s, i1, i2, j1, j2, imm10, imm11;
      hx_uint16 insn_low;

      if (!HOOX_IS_WITHIN_INT24_RANGE (distance))
        return FALSE;

      insn_low = HX_UINT16_FROM_LE (r->insn[1]);

      distance_word.i = distance;

      s =  (distance_word.u >> 23) & 1;
      i1 = (distance_word.u >> 22) & 1;
      i2 = (distance_word.u >> 21) & 1;
      j1 = (i1 ^ 1) ^ s;
      j2 = (i2 ^ 1) ^ s;

      imm10 = (distance_word.u >> 11) & HOOX_INT10_MASK;
      imm11 =  distance_word.u        & HOOX_INT11_MASK;

      insn     |=  (s << 10) | imm10;
      insn_low |= (j1 << 13) | (j2 << 11) | imm11;

      r->insn[1] = HX_UINT16_TO_LE (insn_low);

      break;
    }
    case HOOX_THUMB_CBZ_T1:
    case HOOX_THUMB_CBNZ_T1:
    {
      hx_uint16 i, imm5;

      if (!HOOX_IS_WITHIN_UINT7_RANGE (distance * sizeof (hx_uint16)))
        return FALSE;

      i = (distance >> 5) & 1;
      imm5 = distance & HOOX_INT5_MASK;

      insn |= (i << 9) | (imm5 << 3);

      break;
    }
    default:
      hx_assert_not_reached ();
  }

  *r->insn = HX_UINT16_TO_LE (insn);

  return TRUE;
}

static void
hoox_thumb_writer_maybe_commit_literals (HooxThumbWriter * self)
{
  hx_size space_used;
  hx_constpointer after_literals = self->code;

  if (self->earliest_literal_insn == NULL)
    return;

  space_used = (self->code - self->earliest_literal_insn) * sizeof (hx_uint16);
  space_used += self->literal_refs.length * sizeof (hx_uint32);
  if (space_used <= 1024)
    return;

  self->earliest_literal_insn = NULL;

  hoox_thumb_writer_put_b_label (self, after_literals);
  hoox_thumb_writer_commit_literals (self);
  hoox_thumb_writer_put_label (self, after_literals);
}

static void
hoox_thumb_writer_commit_literals (HooxThumbWriter * self)
{
  hx_uint num_refs, ref_index;
  hx_boolean need_alignment_padding;
  hx_uint32 * first_slot, * last_slot;

  if (!hoox_thumb_writer_has_literal_refs (self))
    return;

  num_refs = self->literal_refs.length;
  if (num_refs == 0)
    return;

  need_alignment_padding = (self->pc & 3) != 0;
  if (need_alignment_padding)
  {
    hoox_thumb_writer_put_nop (self);
  }

  first_slot = (hx_uint32 *) self->code;
  last_slot = first_slot;

  for (ref_index = 0; ref_index != num_refs; ref_index++)
  {
    HooxThumbLiteralRef * r;
    hx_uint16 insn;
    hx_uint32 * cur_slot;
    HooxAddress slot_pc;
    hx_size distance_in_bytes;

    r = hoox_metal_array_element_at (&self->literal_refs, ref_index);
    insn = HX_UINT16_FROM_LE (r->insn[0]);

    for (cur_slot = first_slot; cur_slot != last_slot; cur_slot++)
    {
      if (*cur_slot == r->val)
        break;
    }

    if (cur_slot == last_slot)
    {
      *cur_slot = r->val;
      self->code += 2;
      self->pc += 4;
      last_slot++;
    }

    slot_pc = self->pc - ((hx_uint8 *) last_slot - (hx_uint8 *) first_slot) +
        ((hx_uint8 *) cur_slot - (hx_uint8 *) first_slot);

    distance_in_bytes = slot_pc - (r->pc & ~((HooxAddress) 3));

    if (hoox_instruction_is_t1_load (insn))
    {
      r->insn[0] = HX_UINT16_TO_LE (insn | (distance_in_bytes / 4));
    }
    else
    {
      r->insn[1] = HX_UINT16_TO_LE (HX_UINT16_FROM_LE (r->insn[1]) |
          distance_in_bytes);
    }
  }

  hoox_metal_array_remove_all (&self->literal_refs);
}

static hx_boolean
hoox_instruction_is_t1_load (hx_uint16 instruction)
{
  return (instruction & 0xf800) == 0x4800;
}
