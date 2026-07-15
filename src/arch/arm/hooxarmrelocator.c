/*
 * Copyright (C) 2010-2024 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxarmrelocator.h"

#include "hooxmemory.h"

#define HOOX_MAX_INPUT_INSN_COUNT (100)

typedef struct _HooxCodeGenCtx HooxCodeGenCtx;

struct _HooxCodeGenCtx
{
  const hx_insn * insn;
  hx_arm * detail;
  HooxAddress pc;

  HooxArmWriter * output;
};

static hx_boolean hoox_arm_branch_is_unconditional (const hx_insn * insn);
static hx_boolean hoox_arm_relocator_insn_is_supported (const hx_insn * insn);
static hx_boolean hoox_reg_dest_is_pc (const hx_insn * insn);
static hx_boolean hoox_reg_list_contains_pc (const hx_insn * insn,
    hx_uint8 start_index);

static hx_boolean hoox_arm_relocator_rewrite_ldr (HooxArmRelocator * self,
    HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm_relocator_rewrite_mov (HooxArmRelocator * self,
    HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm_relocator_rewrite_add (HooxArmRelocator * self,
    HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm_relocator_rewrite_sub (HooxArmRelocator * self,
    HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm_relocator_rewrite_b (HooxArmRelocator * self,
    hx_mode target_mode, HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm_relocator_rewrite_b_cond (HooxArmRelocator * self,
    HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm_relocator_rewrite_bl (HooxArmRelocator * self,
    hx_mode target_mode, HooxCodeGenCtx * ctx);

HooxArmRelocator *
hoox_arm_relocator_new (hx_constpointer input_code,
                       HooxArmWriter * output)
{
  HooxArmRelocator * relocator;

  relocator = hx_slice_new (HooxArmRelocator);

  hoox_arm_relocator_init (relocator, input_code, output);

  return relocator;
}

HooxArmRelocator *
hoox_arm_relocator_ref (HooxArmRelocator * relocator)
{
  hx_atomic_int_inc (&relocator->ref_count);

  return relocator;
}

void
hoox_arm_relocator_unref (HooxArmRelocator * relocator)
{
  if (hx_atomic_int_dec_and_test (&relocator->ref_count))
  {
    hoox_arm_relocator_clear (relocator);

    hx_slice_free (HooxArmRelocator, relocator);
  }
}

void
hoox_arm_relocator_init (HooxArmRelocator * relocator,
                        hx_constpointer input_code,
                        HooxArmWriter * output)
{
  relocator->ref_count = 1;

  hx_arch_register_arm ();
  hx_open (HX_ARCH_ARM, HX_MODE_ARM | HX_MODE_V8, &relocator->capstone);
  hx_option (relocator->capstone, HX_OPT_DETAIL, HX_OPT_ON);
  relocator->input_insns = hx_new0 (hx_insn *, HOOX_MAX_INPUT_INSN_COUNT);

  relocator->output = NULL;

  hoox_arm_relocator_reset (relocator, input_code, output);
}

void
hoox_arm_relocator_clear (HooxArmRelocator * relocator)
{
  hx_uint i;

  hoox_arm_relocator_reset (relocator, NULL, NULL);

  for (i = 0; i != HOOX_MAX_INPUT_INSN_COUNT; i++)
  {
    hx_insn * insn = relocator->input_insns[i];
    if (insn != NULL)
    {
      hx_insn_free (insn, 1);
      relocator->input_insns[i] = NULL;
    }
  }
  hx_free (relocator->input_insns);

  hx_close (&relocator->capstone);
}

void
hoox_arm_relocator_reset (HooxArmRelocator * relocator,
                         hx_constpointer input_code,
                         HooxArmWriter * output)
{
  relocator->input_start = input_code;
  relocator->input_cur = input_code;
  relocator->input_pc = HOOX_ADDRESS (input_code);

  if (output != NULL)
    hoox_arm_writer_ref (output);
  if (relocator->output != NULL)
    hoox_arm_writer_unref (relocator->output);
  relocator->output = output;

  relocator->inpos = 0;
  relocator->outpos = 0;

  relocator->eob = FALSE;
  relocator->eoi = FALSE;
}

static hx_uint
hoox_arm_relocator_inpos (HooxArmRelocator * self)
{
  return self->inpos % HOOX_MAX_INPUT_INSN_COUNT;
}

static hx_uint
hoox_arm_relocator_outpos (HooxArmRelocator * self)
{
  return self->outpos % HOOX_MAX_INPUT_INSN_COUNT;
}

static void
hoox_arm_relocator_increment_inpos (HooxArmRelocator * self)
{
  self->inpos++;
  hx_assert (self->inpos > self->outpos);
}

static void
hoox_arm_relocator_increment_outpos (HooxArmRelocator * self)
{
  self->outpos++;
  hx_assert (self->outpos <= self->inpos);
}

hx_uint
hoox_arm_relocator_read_one (HooxArmRelocator * self,
                            const hx_insn ** instruction)
{
  hx_insn ** insn_ptr, * insn;
  const uint8_t * code;
  size_t size;
  uint64_t address;

  if (self->eoi)
    return 0;

  insn_ptr = &self->input_insns[hoox_arm_relocator_inpos (self)];

  if (*insn_ptr == NULL)
    *insn_ptr = hx_insn_alloc (self->capstone);

  code = self->input_cur;
  size = 4;
  address = self->input_pc;
  insn = *insn_ptr;

  if (!hx_disasm_iter (self->capstone, &code, &size, &address, insn))
    return 0;

  switch (insn->id)
  {
    case HX_ARM_INS_B:
    case HX_ARM_INS_BX:
      self->eob = TRUE;
      self->eoi = hoox_arm_branch_is_unconditional (insn);
      break;
    case HX_ARM_INS_BL:
    case HX_ARM_INS_BLX:
      self->eob = TRUE;
      self->eoi = FALSE;
      break;
    case HX_ARM_INS_LDR:
    case HX_ARM_INS_MOV:
    case HX_ARM_INS_ADD:
    case HX_ARM_INS_SUB:
      self->eob = self->eoi = hoox_reg_dest_is_pc (insn);
      break;
    case HX_ARM_INS_POP:
      self->eob = self->eoi = hoox_reg_list_contains_pc (insn, 0);
      break;
    case HX_ARM_INS_LDM:
      self->eob = self->eoi = hoox_reg_list_contains_pc (insn, 1);
      break;
    default:
      self->eob = FALSE;
      break;
  }

  hoox_arm_relocator_increment_inpos (self);

  if (instruction != NULL)
    *instruction = insn;

  self->input_cur = code;
  self->input_pc = address;

  return self->input_cur - self->input_start;
}

hx_insn *
hoox_arm_relocator_peek_next_write_insn (HooxArmRelocator * self)
{
  if (self->outpos == self->inpos)
    return NULL;

  return self->input_insns[hoox_arm_relocator_outpos (self)];
}

hx_pointer
hoox_arm_relocator_peek_next_write_source (HooxArmRelocator * self)
{
  hx_insn * next;

  next = hoox_arm_relocator_peek_next_write_insn (self);
  if (next == NULL)
    return NULL;

  return HX_SIZE_TO_POINTER (next->address);
}

void
hoox_arm_relocator_skip_one (HooxArmRelocator * self)
{
  hoox_arm_relocator_increment_outpos (self);
}

hx_boolean
hoox_arm_relocator_write_one (HooxArmRelocator * self)
{
  const hx_insn * insn;
  HooxCodeGenCtx ctx;
  hx_boolean rewritten;

  if ((insn = hoox_arm_relocator_peek_next_write_insn (self)) == NULL)
    return FALSE;
  hoox_arm_relocator_increment_outpos (self);
  ctx.insn = insn;
  ctx.detail = &ctx.insn->detail->arm;
  ctx.pc = insn->address + 8;
  ctx.output = self->output;

  switch (insn->id)
  {
    case HX_ARM_INS_LDR:
      rewritten = hoox_arm_relocator_rewrite_ldr (self, &ctx);
      break;
    case HX_ARM_INS_MOV:
      rewritten = hoox_arm_relocator_rewrite_mov (self, &ctx);
      break;
    case HX_ARM_INS_ADD:
      rewritten = hoox_arm_relocator_rewrite_add (self, &ctx);
      break;
    case HX_ARM_INS_SUB:
      rewritten = hoox_arm_relocator_rewrite_sub (self, &ctx);
      break;
    case HX_ARM_INS_B:
      if (hoox_arm_branch_is_unconditional (insn))
        rewritten = hoox_arm_relocator_rewrite_b (self, HX_MODE_ARM, &ctx);
      else
        rewritten = hoox_arm_relocator_rewrite_b_cond (self, &ctx);
      break;
    case HX_ARM_INS_BX:
      rewritten = hoox_arm_relocator_rewrite_b (self, HX_MODE_THUMB, &ctx);
      break;
    case HX_ARM_INS_BL:
      rewritten = hoox_arm_relocator_rewrite_bl (self, HX_MODE_ARM, &ctx);
      break;
    case HX_ARM_INS_BLX:
      rewritten = hoox_arm_relocator_rewrite_bl (self, HX_MODE_THUMB, &ctx);
      break;
    default:
      rewritten = FALSE;
      break;
  }

  if (!rewritten)
    hoox_arm_writer_put_bytes (ctx.output, insn->bytes, insn->size);

  return TRUE;
}

void
hoox_arm_relocator_write_all (HooxArmRelocator * self)
{
  HX_GNUC_UNUSED hx_uint count = 0;

  while (hoox_arm_relocator_write_one (self))
    count++;

  hx_assert (count > 0);
}

hx_boolean
hoox_arm_relocator_eob (HooxArmRelocator * self)
{
  return self->eob;
}

hx_boolean
hoox_arm_relocator_eoi (HooxArmRelocator * self)
{
  return self->eoi;
}

hx_boolean
hoox_arm_relocator_can_relocate (hx_pointer address,
                                hx_uint min_bytes,
                                hx_uint * maximum)
{
  hx_uint n = 0;
  hx_uint8 * buf;
  HooxArmWriter cw;
  HooxArmRelocator rl;
  hx_uint reloc_bytes;

  buf = hx_alloca (3 * min_bytes);
  hoox_arm_writer_init (&cw, buf);
  cw.cpu_features = hoox_query_cpu_features ();

  hoox_arm_relocator_init (&rl, address, &cw);

  do
  {
    const hx_insn * insn;

    reloc_bytes = hoox_arm_relocator_read_one (&rl, &insn);
    if (reloc_bytes == 0)
      break;
    if (!hoox_arm_relocator_insn_is_supported (insn))
      break;

    n = reloc_bytes;
  }
  while (reloc_bytes < min_bytes);

  hoox_arm_relocator_clear (&rl);

  hoox_arm_writer_clear (&cw);

  if (maximum != NULL)
    *maximum = n;

  return n >= min_bytes;
}

hx_uint
hoox_arm_relocator_relocate (hx_pointer from,
                            hx_uint min_bytes,
                            hx_pointer to)
{
  HooxArmWriter cw;
  HooxArmRelocator rl;
  hx_uint reloc_bytes;

  hoox_arm_writer_init (&cw, to);
  cw.cpu_features = hoox_query_cpu_features ();

  hoox_arm_relocator_init (&rl, from, &cw);

  do
  {
    reloc_bytes = hoox_arm_relocator_read_one (&rl, NULL);
    hx_assert (reloc_bytes != 0);
  }
  while (reloc_bytes < min_bytes);

  hoox_arm_relocator_write_all (&rl);

  hoox_arm_relocator_clear (&rl);
  hoox_arm_writer_clear (&cw);

  return reloc_bytes;
}

static hx_boolean
hoox_arm_branch_is_unconditional (const hx_insn * insn)
{
  switch (insn->detail->arm.cc)
  {
    case HX_ARM_CC_INVALID:
    case HX_ARM_CC_AL:
      return TRUE;
    default:
      return FALSE;
  }
}

static hx_boolean
hoox_arm_relocator_insn_is_supported (const hx_insn * insn)
{
  const hx_arm * detail = &insn->detail->arm;

  if (insn->id == HX_ARM_INS_UNSUPPORTED_PC_RELATIVE)
    return FALSE;

  if (insn->id == HX_ARM_INS_LDR && detail->op_count >= 2)
  {
    const hx_arm_op * source = &detail->operands[1];

    if (source->type == HX_ARM_OP_MEM && source->mem.base == HX_ARM_REG_PC &&
        (detail->writeback ||
         (source->mem.index != HX_ARM_REG_INVALID && source->subtracted)))
      return FALSE;
  }

  return TRUE;
}

static hx_boolean
hoox_reg_dest_is_pc (const hx_insn * insn)
{
  return insn->detail->arm.operands[0].reg == HX_ARM_REG_PC;
}

static hx_boolean
hoox_reg_list_contains_pc (const hx_insn * insn,
                          hx_uint8 start_index)
{
  hx_uint8 i;

  for (i = start_index; i < insn->detail->arm.op_count; i++)
  {
    if (insn->detail->arm.operands[i].reg == HX_ARM_REG_PC)
      return TRUE;
  }

  return FALSE;
}

static hx_boolean
hoox_arm_relocator_rewrite_ldr (HooxArmRelocator * self,
                               HooxCodeGenCtx * ctx)
{
  const hx_arm_op * dst = &ctx->detail->operands[0];
  const hx_arm_op * src = &ctx->detail->operands[1];
  hx_arm_reg target;

  if (src->type != HX_ARM_OP_MEM || src->mem.base != HX_ARM_REG_PC)
    return FALSE;

  if (ctx->detail->writeback)
  {
    /* FIXME: LDR with writeback not yet supported. */
    return FALSE;
  }

  if (dst->reg == HX_ARM_REG_PC)
  {
    /*
     * When choosing a scratch register, we favor Rm since it is often this
     * value we wish to use to start our calculation and this avoids a register
     * move.
     *
     * If however Rm is an immediate, we choose an arbitrary register.
     */
    target = (src->mem.index != HX_ARM_REG_INVALID) ? src->mem.index : HX_ARM_REG_R0;

    hoox_arm_writer_put_push_regs (ctx->output, 2, target, HX_ARM_REG_PC);
  }
  else
  {
    target = dst->reg;
  }

  /* Handle 'LDR Rt, [Rn, #x]' or 'LDR Rt, [Rn, #-x]' */
  if (src->mem.index == HX_ARM_REG_INVALID)
  {
    hoox_arm_writer_put_ldr_reg_address (ctx->output, target,
        ctx->pc + src->mem.disp);
  }
  else
  {
    if (src->subtracted)
    {
      /* FIXME: 'LDR Rt, [Rn, -Rm, #x]' not yet supported. */
      return FALSE;
    }

    /* Handle 'LDR Rt, [Rn, Rm, lsl #x]' */
    hoox_arm_writer_put_mov_reg_reg_shift (ctx->output, target, src->mem.index,
        src->shift.type, src->shift.value);

    hoox_arm_writer_put_add_reg_u32 (ctx->output, target, ctx->pc);
  }

  hoox_arm_writer_put_ldr_reg_reg_offset (ctx->output, target, target, 0);

  if (dst->reg == HX_ARM_REG_PC)
  {
    hoox_arm_writer_put_str_reg_reg_offset (ctx->output, target, HX_ARM_REG_SP, 4);
    hoox_arm_writer_put_pop_regs (ctx->output, 2, target, HX_ARM_REG_PC);
  }

  return TRUE;
}

static hx_boolean
hoox_arm_relocator_rewrite_mov (HooxArmRelocator * self,
                               HooxCodeGenCtx * ctx)
{
  const hx_arm_op * dst = &ctx->detail->operands[0];
  const hx_arm_op * src = &ctx->detail->operands[1];

  if (src->type != HX_ARM_OP_REG || src->reg != HX_ARM_REG_PC)
    return FALSE;

  hoox_arm_writer_put_ldr_reg_address (ctx->output, dst->reg, ctx->pc);
  return TRUE;
}

static hx_boolean
hoox_arm_relocator_rewrite_add (HooxArmRelocator * self,
                               HooxCodeGenCtx * ctx)
{
  const hx_arm_op * operands = ctx->detail->operands;
  const hx_arm_op * dst = &operands[0];
  const hx_arm_op * left = &operands[1];
  const hx_arm_op * right = &operands[2];
  hx_arm_reg target;

  if (right->type == HX_ARM_OP_REG && right->reg == HX_ARM_REG_PC)
  {
    const hx_arm_op * l = left;
    left = right;
    right = l;
  }

  if (left->reg != HX_ARM_REG_PC)
    return FALSE;

  if (dst->reg == HX_ARM_REG_PC)
  {
    /*
     * When choosing a scratch register, we favor Rm since it is often this
     * value we wish to use to start our calculation and this avoids a register
     * move.
     *
     * If however Rm is an immediate, we choose an arbitrary register.
     */
    target = (right->type == HX_ARM_OP_REG) ? right->reg : HX_ARM_REG_R0;

    hoox_arm_writer_put_push_regs (ctx->output, 2, target, HX_ARM_REG_PC);
  }
  else
  {
    target = dst->reg;
  }

  if (right->shift.value == 0 && ctx->detail->op_count < 4)
  {
    /*
     * We have no shift to apply, so we start our calculation with the value of
     * PC since we can store this as a literal in the code stream and reduce the
     * number of instructions we need to generate.
     */
    if (right->type == HX_ARM_OP_IMM)
    {
      /* Handle 'ADD Rd, Rn, #x' */
      hoox_arm_writer_put_ldr_reg_address (ctx->output, target, ctx->pc);
      hoox_arm_writer_put_add_reg_u32 (ctx->output, target, right->imm);
    }
    else if (right->reg == dst->reg)
    {
      /*
       * Handle 'ADD Rd, Rn, Rd'. This is a special case since we cannot load PC
       * from a literal into Rd since in doing so, we lose the value of Rm which
       * we want to add on. This calculation can be simplified to just adding
       * the PC to Rd.
       */
      hoox_arm_writer_put_add_reg_u32 (ctx->output, target, ctx->pc);
    }
    else
    {
      /* Handle 'ADD Rd, Rn, Rm' */
      hoox_arm_writer_put_ldr_reg_address (ctx->output, target, ctx->pc);
      hoox_arm_writer_put_add_reg_reg_reg (ctx->output, target, target,
          right->reg);
    }
  }
  else
  {
    /*
     * As we have a shift operation to apply, we must start by calculating this
     * value and adding on PC, as we would otherwise need a second scratch
     * register to calculate this. Note that in this case, we don't have to
     * worry if Rd == Rm since although we may be using Rd to hold the
     * intermediate result, we perform all necessary calculations on Rm before
     * we update Rd.
     */

    if (right->type == HX_ARM_OP_IMM)
    {
      /* Handle 'ADD Rd, Rn, #x, lsl #n' */
      hoox_arm_writer_put_ldr_reg_u32 (ctx->output, target, right->imm);
    }
    else
    {
      /* Handle 'ADD Rd, Rn, Rm, lsl #n' */
      hoox_arm_writer_put_mov_reg_reg (ctx->output, target, right->reg);
    }

    if (ctx->detail->op_count < 4)
    {
      hoox_arm_writer_put_mov_reg_reg_shift (ctx->output, target, target,
          right->shift.type, right->shift.value);
    }
    else
    {
      hoox_arm_writer_put_mov_reg_reg_shift (ctx->output, target, target,
          HX_ARM_SFT_ROR, operands[3].imm);
    }

    /*
     * Now the shifted second operand has been calculated, we can simply add the
     * PC value.
     */
    hoox_arm_writer_put_add_reg_u32 (ctx->output, target, ctx->pc);
  }

  if (dst->reg == HX_ARM_REG_PC)
  {
    hoox_arm_writer_put_str_reg_reg_offset (ctx->output, target, HX_ARM_REG_SP, 4);
    hoox_arm_writer_put_pop_regs (ctx->output, 2, target, HX_ARM_REG_PC);
  }

  return TRUE;
}

static hx_boolean
hoox_arm_relocator_rewrite_sub (HooxArmRelocator * self,
                               HooxCodeGenCtx * ctx)
{
  const hx_arm_op * operands = ctx->detail->operands;
  const hx_arm_op * dst = &operands[0];
  const hx_arm_op * left = &operands[1];
  const hx_arm_op * right = &operands[2];
  hx_boolean pc_is_involved;
  hx_arm_reg target;

  pc_is_involved = (left->type == HX_ARM_OP_REG && left->reg == HX_ARM_REG_PC) ||
      (right->type == HX_ARM_OP_REG && right->reg == HX_ARM_REG_PC);
  if (!pc_is_involved)
    return FALSE;

  if (dst->reg == HX_ARM_REG_PC)
  {
    /*
     * When choosing a scratch register, we favor Rm since it is often this
     * value we wish to use to start our calculation and this avoids a register
     * move.
     *
     * If however Rm is an immediate, we choose an arbitrary register.
     */
    target = (right->type == HX_ARM_OP_REG && right->reg != HX_ARM_REG_PC)
        ? right->reg
        : HX_ARM_REG_R0;

    hoox_arm_writer_put_push_regs (ctx->output, 2, target, HX_ARM_REG_PC);
  }
  else
  {
    target = dst->reg;
  }

  if (right->shift.value == 0)
  {
    /*
     * We have no shift to apply, so we start our calculation with the value of
     * PC since we can store this as a literal in the code stream and reduce the
     * number of instructions we need to generate.
     */
    if (right->type == HX_ARM_OP_IMM)
    {
      /* Handle 'SUB Rd, PC, #x'. */
      hoox_arm_writer_put_ldr_reg_address (ctx->output, target, ctx->pc);
      hoox_arm_writer_put_sub_reg_u32 (ctx->output, target, right->imm);
    }
    else if (dst->reg == left->reg && left->reg == right->reg)
    {
      /* Handle 'SUB, PC, PC, PC'. */
      hoox_arm_writer_put_sub_reg_reg_reg (ctx->output, target, target, target);
    }
    else if (left->reg == dst->reg)
    {
      if (left->reg == HX_ARM_REG_PC)
      {
        /* Handle 'SUB PC, PC, Rm'. */
        hoox_arm_writer_put_rsb_reg_reg_imm (ctx->output, target, target, 0);
        hoox_arm_writer_put_add_reg_u32 (ctx->output, target, ctx->pc);
      }
      else
      {
        /* Handle 'SUB Rd, Rd, PC'. */
        hoox_arm_writer_put_sub_reg_u32 (ctx->output, target, ctx->pc);
      }
    }
    else if (right->reg == dst->reg)
    {
      if (right->reg == HX_ARM_REG_PC)
      {
        /* Handle 'SUB PC, Rn, PC'. */
        hoox_arm_writer_put_mov_reg_reg (ctx->output, target, left->reg);
        hoox_arm_writer_put_sub_reg_u32 (ctx->output, target, ctx->pc);
      }
      else
      {
        /* Handle 'SUB Rd, PC, Rd'. */
        hoox_arm_writer_put_rsb_reg_reg_imm (ctx->output, target, target, 0);
        hoox_arm_writer_put_add_reg_u32 (ctx->output, target, ctx->pc);
      }
    }
    else if (left->reg == HX_ARM_REG_PC)
    {
      /* Handle 'SUB Rd, PC, Rm'. */
      hoox_arm_writer_put_ldr_reg_address (ctx->output, target, ctx->pc);
      hoox_arm_writer_put_sub_reg_reg_imm (ctx->output, target, right->reg, 0);
    }
    else if (right->reg == HX_ARM_REG_PC)
    {
      /* Handle 'SUB Rd, Rn, PC'. */
      hoox_arm_writer_put_ldr_reg_address (ctx->output, target, ctx->pc);
      hoox_arm_writer_put_rsb_reg_reg_imm (ctx->output, target, target, 0);
      hoox_arm_writer_put_add_reg_reg_imm (ctx->output, target, left->reg, 0);
    }
  }
  else
  {
    /*
     * As we have a shift operation to apply, we must start by calculating this
     * value and subtracting from PC, as we would otherwise need a second
     * scratch register to calculate this. Note that in this case, we don't have
     * to worry if Rd == Rm since although we may be using Rd to hold the
     * intermediate result, we perform all necessary calculations on Rm before
     * we update Rd.
     */
    if (right->type == HX_ARM_OP_IMM)
    {
      /* Handle 'SUB Rd, PC, #x, lsl #n'. */
      hoox_arm_writer_put_ldr_reg_u32 (ctx->output, target, right->imm);
    }
    else
    {
      /*
      * Whilst technically possible, it seems quite unlikely that anyone would
      * want to perform any shifting operations on the PC itself.
      */
      hx_assert (right->reg != HX_ARM_REG_PC);

      /* Handle 'SUB Rd, PC, Rm, lsl #n'. */
      hoox_arm_writer_put_mov_reg_reg (ctx->output, target, right->reg);
    }

    hoox_arm_writer_put_mov_reg_reg_shift (ctx->output, target, target,
        right->shift.type, right->shift.value);

    /*
     * Now the shifted second operand has been calculated, we can negate it and
     * add the PC value.
     */
    hoox_arm_writer_put_rsb_reg_reg_imm (ctx->output, target, target, 0);
    hoox_arm_writer_put_add_reg_u32 (ctx->output, target, ctx->pc);
  }

  if (dst->reg == HX_ARM_REG_PC)
  {
    hoox_arm_writer_put_str_reg_reg_offset (ctx->output, target, HX_ARM_REG_SP, 4);
    hoox_arm_writer_put_pop_regs (ctx->output, 2, target, HX_ARM_REG_PC);
  }

  return TRUE;
}

static hx_boolean
hoox_arm_relocator_rewrite_b (HooxArmRelocator * self,
                             hx_mode target_mode,
                             HooxCodeGenCtx * ctx)
{
  const hx_arm_op * target = &ctx->detail->operands[0];

  if (target->type != HX_ARM_OP_IMM)
    return FALSE;

  hoox_arm_writer_put_ldr_reg_address (ctx->output, HX_ARM_REG_PC,
      (target_mode == HX_MODE_THUMB) ? target->imm | 1 : target->imm);
  return TRUE;
}

static hx_boolean
hoox_arm_relocator_rewrite_b_cond (HooxArmRelocator * self,
                                  HooxCodeGenCtx * ctx)
{
  const hx_arm_op * target = &ctx->detail->operands[0];
  hx_size unique_id = HX_POINTER_TO_SIZE (ctx->output->code) << 1;
  hx_constpointer is_true = HX_SIZE_TO_POINTER (unique_id | 1);
  hx_constpointer is_false = HX_SIZE_TO_POINTER (unique_id | 0);

  if (target->type != HX_ARM_OP_IMM)
    return FALSE;

  hoox_arm_writer_put_b_cond_label (ctx->output, ctx->detail->cc, is_true);
  hoox_arm_writer_put_b_label (ctx->output, is_false);

  hoox_arm_writer_put_label (ctx->output, is_true);
  hoox_arm_writer_put_push_regs (ctx->output, 1, HX_ARM_REG_R0);
  hoox_arm_writer_put_push_regs (ctx->output, 1, HX_ARM_REG_R0);
  hoox_arm_writer_put_ldr_reg_address (ctx->output, HX_ARM_REG_R0,
      target->imm);
  hoox_arm_writer_put_str_reg_reg_offset (ctx->output, HX_ARM_REG_R0,
      HX_ARM_REG_SP, 4);
  hoox_arm_writer_put_pop_regs (ctx->output, 2, HX_ARM_REG_R0, HX_ARM_REG_PC);

  hoox_arm_writer_put_label (ctx->output, is_false);

  return TRUE;
}

static hx_boolean
hoox_arm_relocator_rewrite_bl (HooxArmRelocator * self,
                              hx_mode target_mode,
                              HooxCodeGenCtx * ctx)
{
  const hx_arm_op * target = &ctx->detail->operands[0];

  if (target->type != HX_ARM_OP_IMM)
    return FALSE;

  hoox_arm_writer_put_ldr_reg_address (ctx->output, HX_ARM_REG_LR,
      ctx->output->pc + (2 * 4));
  hoox_arm_writer_put_ldr_reg_address (ctx->output, HX_ARM_REG_PC,
      (target_mode == HX_MODE_THUMB) ? target->imm | 1 : target->imm);
  return TRUE;
}
