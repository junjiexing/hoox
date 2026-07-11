/*
 * Copyright (C) 2014-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

/* Useful reference: C4.1 A64 instruction index by encoding */

#include "hooxarm64relocator.h"

#include "hooxmemory.h"

#define HOOX_MAX_INPUT_INSN_COUNT (100)

typedef struct _HooxCodeGenCtx HooxCodeGenCtx;

struct _HooxCodeGenCtx
{
  const hx_insn * insn;
  hx_arm64 * detail;

  HooxArm64Writer * output;
};

static hx_boolean hoox_arm64_relocator_register_is_free (
    const HooxArm64Relocator * self, hx_uint n, hx_arm64_reg reg);

static hx_boolean hoox_arm64_branch_is_unconditional (const hx_insn * insn);

static hx_boolean hoox_arm64_relocator_rewrite_ldr (HooxArm64Relocator * self,
    HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm64_relocator_rewrite_adr (HooxArm64Relocator * self,
    HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm64_relocator_rewrite_b (HooxArm64Relocator * self,
    HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm64_relocator_rewrite_b_cond (HooxArm64Relocator * self,
    HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm64_relocator_rewrite_bl (HooxArm64Relocator * self,
    HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm64_relocator_rewrite_cbz (HooxArm64Relocator * self,
    HooxCodeGenCtx * ctx);
static hx_boolean hoox_arm64_relocator_rewrite_tbz (HooxArm64Relocator * self,
    HooxCodeGenCtx * ctx);

HooxArm64Relocator *
hoox_arm64_relocator_new (hx_constpointer input_code,
                         HooxArm64Writer * output)
{
  HooxArm64Relocator * relocator;

  relocator = hx_slice_new (HooxArm64Relocator);

  hoox_arm64_relocator_init (relocator, input_code, output);

  return relocator;
}

HooxArm64Relocator *
hoox_arm64_relocator_ref (HooxArm64Relocator * relocator)
{
  hx_atomic_int_inc (&relocator->ref_count);

  return relocator;
}

void
hoox_arm64_relocator_unref (HooxArm64Relocator * relocator)
{
  if (hx_atomic_int_dec_and_test (&relocator->ref_count))
  {
    hoox_arm64_relocator_clear (relocator);

    hx_slice_free (HooxArm64Relocator, relocator);
  }
}

void
hoox_arm64_relocator_init (HooxArm64Relocator * relocator,
                          hx_constpointer input_code,
                          HooxArm64Writer * output)
{
  relocator->ref_count = 1;

  hx_arch_register_arm64 ();
  hx_open (HX_ARCH_ARM64, HOOX_DEFAULT_HX_ENDIAN, &relocator->capstone);
  hx_option (relocator->capstone, HX_OPT_DETAIL, HX_OPT_ON);
  relocator->input_insns = hx_new0 (hx_insn *, HOOX_MAX_INPUT_INSN_COUNT);

  relocator->output = NULL;

  hoox_arm64_relocator_reset (relocator, input_code, output);
}

void
hoox_arm64_relocator_clear (HooxArm64Relocator * relocator)
{
  hx_uint i;

  hoox_arm64_relocator_reset (relocator, NULL, NULL);

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
hoox_arm64_relocator_reset (HooxArm64Relocator * relocator,
                           hx_constpointer input_code,
                           HooxArm64Writer * output)
{
  relocator->input_start = input_code;
  relocator->input_cur = input_code;
  relocator->input_pc = HOOX_ADDRESS (input_code);

  if (output != NULL)
    hoox_arm64_writer_ref (output);
  if (relocator->output != NULL)
    hoox_arm64_writer_unref (relocator->output);
  relocator->output = output;

  relocator->inpos = 0;
  relocator->outpos = 0;

  relocator->eob = FALSE;
  relocator->eoi = FALSE;
}

static hx_uint
hoox_arm64_relocator_inpos (HooxArm64Relocator * self)
{
  return self->inpos % HOOX_MAX_INPUT_INSN_COUNT;
}

static hx_uint
hoox_arm64_relocator_outpos (HooxArm64Relocator * self)
{
  return self->outpos % HOOX_MAX_INPUT_INSN_COUNT;
}

static void
hoox_arm64_relocator_increment_inpos (HooxArm64Relocator * self)
{
  self->inpos++;
  hx_assert (self->inpos > self->outpos);
}

static void
hoox_arm64_relocator_increment_outpos (HooxArm64Relocator * self)
{
  self->outpos++;
  hx_assert (self->outpos <= self->inpos);
}

hx_uint
hoox_arm64_relocator_read_one (HooxArm64Relocator * self,
                              const hx_insn ** instruction)
{
  hx_insn ** insn_ptr, * insn;
  const uint8_t * code;
  size_t size;
  uint64_t address;

  if (self->eoi)
    return 0;

  insn_ptr = &self->input_insns[hoox_arm64_relocator_inpos (self)];

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
    case HX_ARM64_INS_B:
      self->eob = TRUE;
      self->eoi = hoox_arm64_branch_is_unconditional (insn);
      break;
    case HX_ARM64_INS_BR:
    case HX_ARM64_INS_BRAA:
    case HX_ARM64_INS_BRAAZ:
    case HX_ARM64_INS_BRAB:
    case HX_ARM64_INS_BRABZ:
    case HX_ARM64_INS_RET:
    case HX_ARM64_INS_RETAA:
    case HX_ARM64_INS_RETAB:
      self->eob = TRUE;
      self->eoi = TRUE;
      break;
    case HX_ARM64_INS_BL:
    case HX_ARM64_INS_BLR:
    case HX_ARM64_INS_BLRAA:
    case HX_ARM64_INS_BLRAAZ:
    case HX_ARM64_INS_BLRAB:
    case HX_ARM64_INS_BLRABZ:
      self->eob = TRUE;
      self->eoi = FALSE;
      break;
    case HX_ARM64_INS_CBZ:
    case HX_ARM64_INS_CBNZ:
    case HX_ARM64_INS_TBZ:
    case HX_ARM64_INS_TBNZ:
      self->eob = TRUE;
      self->eoi = FALSE;
      break;
    default:
      self->eob = FALSE;
      break;
  }

  hoox_arm64_relocator_increment_inpos (self);

  if (instruction != NULL)
    *instruction = insn;

  self->input_cur = code;
  self->input_pc = address;

  return self->input_cur - self->input_start;
}

hx_insn *
hoox_arm64_relocator_peek_next_write_insn (HooxArm64Relocator * self)
{
  if (self->outpos == self->inpos)
    return NULL;

  return self->input_insns[hoox_arm64_relocator_outpos (self)];
}

hx_pointer
hoox_arm64_relocator_peek_next_write_source (HooxArm64Relocator * self)
{
  hx_insn * next;

  next = hoox_arm64_relocator_peek_next_write_insn (self);
  if (next == NULL)
    return NULL;

  return HX_SIZE_TO_POINTER (next->address);
}

void
hoox_arm64_relocator_skip_one (HooxArm64Relocator * self)
{
  hoox_arm64_relocator_increment_outpos (self);
}

hx_boolean
hoox_arm64_relocator_write_one (HooxArm64Relocator * self)
{
  const hx_insn * insn;
  HooxCodeGenCtx ctx;
  hx_boolean rewritten;

  if ((insn = hoox_arm64_relocator_peek_next_write_insn (self)) == NULL)
    return FALSE;
  hoox_arm64_relocator_increment_outpos (self);
  ctx.insn = insn;
  ctx.detail = &ctx.insn->detail->arm64;
  ctx.output = self->output;

  switch (insn->id)
  {
    case HX_ARM64_INS_LDR:
    case HX_ARM64_INS_LDRSW:
      rewritten = hoox_arm64_relocator_rewrite_ldr (self, &ctx);
      break;
    case HX_ARM64_INS_ADR:
    case HX_ARM64_INS_ADRP:
      rewritten = hoox_arm64_relocator_rewrite_adr (self, &ctx);
      break;
    case HX_ARM64_INS_B:
      if (hoox_arm64_branch_is_unconditional (ctx.insn))
        rewritten = hoox_arm64_relocator_rewrite_b (self, &ctx);
      else
        rewritten = hoox_arm64_relocator_rewrite_b_cond (self, &ctx);
      break;
    case HX_ARM64_INS_BL:
      rewritten = hoox_arm64_relocator_rewrite_bl (self, &ctx);
      break;
    case HX_ARM64_INS_CBZ:
    case HX_ARM64_INS_CBNZ:
      rewritten = hoox_arm64_relocator_rewrite_cbz (self, &ctx);
      break;
    case HX_ARM64_INS_TBZ:
    case HX_ARM64_INS_TBNZ:
      rewritten = hoox_arm64_relocator_rewrite_tbz (self, &ctx);
      break;
    default:
      rewritten = FALSE;
      break;
  }

  if (!rewritten)
    hoox_arm64_writer_put_bytes (ctx.output, insn->bytes, insn->size);

  return TRUE;
}

void
hoox_arm64_relocator_write_all (HooxArm64Relocator * self)
{
  HX_GNUC_UNUSED hx_uint count = 0;

  while (hoox_arm64_relocator_write_one (self))
    count++;

  hx_assert (count > 0);
}

hx_boolean
hoox_arm64_relocator_eob (HooxArm64Relocator * self)
{
  return self->eob;
}

hx_boolean
hoox_arm64_relocator_eoi (HooxArm64Relocator * self)
{
  return self->eoi;
}

hx_boolean
hoox_arm64_relocator_can_relocate (hx_pointer address,
                                  hx_uint min_bytes,
                                  HooxRelocationScenario scenario,
                                  HooxRelocationPolicy policy,
                                  hx_uint * maximum,
                                  hx_arm64_reg * available_scratch_reg)
{
  hx_uint n = 0;
  hx_uint8 * buf;
  HooxArm64Writer cw;
  HooxArm64Relocator rl;
  hx_uint reloc_bytes;

  buf = hx_alloca (3 * min_bytes);
  hoox_arm64_writer_init (&cw, buf);

  hoox_arm64_relocator_init (&rl, address, &cw);

  do
  {
    const hx_insn * insn;
    hx_boolean safe_to_relocate_further;

    reloc_bytes = hoox_arm64_relocator_read_one (&rl, &insn);
    if (reloc_bytes == 0)
      break;

    n = reloc_bytes;

    if (scenario == HOOX_SCENARIO_ONLINE)
    {
      switch (insn->id)
      {
        case HX_ARM64_INS_BL:
        case HX_ARM64_INS_BLR:
        case HX_ARM64_INS_SVC:
          safe_to_relocate_further = FALSE;
          break;
        default:
          safe_to_relocate_further = TRUE;
          break;
      }
    }
    else
    {
      safe_to_relocate_further = TRUE;
    }

    if (!safe_to_relocate_further)
      break;
  }
  while (reloc_bytes < min_bytes);

  if (policy == HOOX_RELOCATION_CHECKED && !rl.eoi)
  {
    HxHashTable * checked_targets, * targets_to_check;
    hx_csh capstone;
    hx_insn * insn;
    const hx_uint8 * current_code;
    uint64_t current_address;
    size_t current_code_size;
    hx_pointer target;
    HxHashTableIter iter;

    checked_targets = hx_hash_table_new (NULL, NULL);
    targets_to_check = hx_hash_table_new (NULL, NULL);

    hx_open (HX_ARCH_ARM64, HOOX_DEFAULT_HX_ENDIAN, &capstone);
    hx_option (capstone, HX_OPT_DETAIL, HX_OPT_ON);

    insn = hx_insn_alloc (capstone);
    current_code = rl.input_cur;
    current_address = rl.input_pc;
    current_code_size = 1024;

    do
    {
      hx_boolean carry_on = TRUE;

      hx_hash_table_add (checked_targets, (hx_pointer) current_code);

      hoox_ensure_code_readable (current_code, current_code_size);

      while (carry_on && hx_disasm_iter (capstone, &current_code,
          &current_code_size, &current_address, insn))
      {
        hx_arm64 * d = &insn->detail->arm64;

        switch (insn->id)
        {
          case HX_ARM64_INS_B:
          {
            hx_arm64_op * op = &d->operands[0];

            hx_assert (op->type == HX_ARM64_OP_IMM);
            target = HX_SIZE_TO_POINTER (op->imm);
            if (!hx_hash_table_contains (checked_targets, target))
              hx_hash_table_add (targets_to_check, target);

            carry_on = d->cc != HX_ARM64_CC_INVALID && d->cc != HX_ARM64_CC_AL &&
                d->cc != HX_ARM64_CC_NV;

            break;
          }
          case HX_ARM64_INS_CBZ:
          case HX_ARM64_INS_CBNZ:
          {
            hx_arm64_op * op = &d->operands[1];

            hx_assert (op->type == HX_ARM64_OP_IMM);
            target = HX_SIZE_TO_POINTER (op->imm);
            if (!hx_hash_table_contains (checked_targets, target))
              hx_hash_table_add (targets_to_check, target);

            break;
          }
          case HX_ARM64_INS_TBZ:
          case HX_ARM64_INS_TBNZ:
          {
            hx_arm64_op * op = &d->operands[2];

            hx_assert (op->type == HX_ARM64_OP_IMM);
            target = HX_SIZE_TO_POINTER (op->imm);
            if (!hx_hash_table_contains (checked_targets, target))
              hx_hash_table_add (targets_to_check, target);

            break;
          }
          case HX_ARM64_INS_RET:
          case HX_ARM64_INS_RETAA:
          case HX_ARM64_INS_RETAB:
          {
            carry_on = FALSE;
            break;
          }
          case HX_ARM64_INS_BR:
          case HX_ARM64_INS_BRAA:
          case HX_ARM64_INS_BRAAZ:
          case HX_ARM64_INS_BRAB:
          case HX_ARM64_INS_BRABZ:
          {
            carry_on = FALSE;
            break;
          }
          default:
            break;
        }
      }

      hx_hash_table_iter_init (&iter, targets_to_check);
      if (hx_hash_table_iter_next (&iter, &target, NULL))
      {
        current_code = target;
        if (current_code > rl.input_cur)
          current_address = (current_code - rl.input_cur) + rl.input_pc;
        else
          current_address = rl.input_pc - (rl.input_cur - current_code);
        hx_hash_table_iter_remove (&iter);
      }
      else
      {
        current_code = NULL;
      }
    }
    while (current_code != NULL);

    hx_hash_table_iter_init (&iter, checked_targets);
    while (hx_hash_table_iter_next (&iter, &target, NULL))
    {
      hx_ssize offset = (hx_ssize) target - (hx_ssize) address;
      if (offset > 0 && offset < (hx_ssize) n)
      {
        n = offset;
        if (n == 4)
          break;
      }
    }

    hx_insn_free (insn, 1);

    hx_close (&capstone);

    hx_hash_table_unref (targets_to_check);
    hx_hash_table_unref (checked_targets);
  }

  if (available_scratch_reg != NULL)
  {
    hx_arm64_reg requested_scratch_reg = *available_scratch_reg;

    if (requested_scratch_reg != HX_ARM64_REG_INVALID)
    {
      if (!hoox_arm64_relocator_register_is_free (&rl, n, requested_scratch_reg))
        *available_scratch_reg = HX_ARM64_REG_INVALID;
    }
    else if (hoox_arm64_relocator_register_is_free (&rl, n, HX_ARM64_REG_X16))
    {
      *available_scratch_reg = HX_ARM64_REG_X16;
    }
    else if (hoox_arm64_relocator_register_is_free (&rl, n, HX_ARM64_REG_X17))
    {
      *available_scratch_reg = HX_ARM64_REG_X17;
    }
    else
    {
      *available_scratch_reg = HX_ARM64_REG_INVALID;
    }
  }

  hoox_arm64_relocator_clear (&rl);

  hoox_arm64_writer_clear (&cw);

  if (maximum != NULL)
    *maximum = n;

  return n >= min_bytes;
}

static hx_boolean
hoox_arm64_relocator_register_is_free (const HooxArm64Relocator * self,
                                      hx_uint n,
                                      hx_arm64_reg reg)
{
  hx_uint insn_index;

  for (insn_index = 0; insn_index != n / 4; insn_index++)
  {
    const hx_insn * insn = self->input_insns[insn_index];
    const hx_arm64 * info = &insn->detail->arm64;
    uint8_t op_index;

    for (op_index = 0; op_index != info->op_count; op_index++)
    {
      const hx_arm64_op * op = &info->operands[op_index];

      if (op->type == HX_ARM64_OP_REG && op->reg == reg)
        return FALSE;
    }
  }

  return TRUE;
}

hx_uint
hoox_arm64_relocator_relocate (hx_pointer from,
                              hx_uint min_bytes,
                              hx_pointer to)
{
  HooxArm64Writer cw;
  HooxArm64Relocator rl;
  hx_uint reloc_bytes;

  hoox_arm64_writer_init (&cw, to);

  hoox_arm64_relocator_init (&rl, from, &cw);

  do
  {
    reloc_bytes = hoox_arm64_relocator_read_one (&rl, NULL);
    hx_assert (reloc_bytes != 0);
  }
  while (reloc_bytes < min_bytes);

  hoox_arm64_relocator_write_all (&rl);

  hoox_arm64_relocator_clear (&rl);
  hoox_arm64_writer_clear (&cw);

  return reloc_bytes;
}

static hx_boolean
hoox_arm64_branch_is_unconditional (const hx_insn * insn)
{
  switch (insn->detail->arm64.cc)
  {
    case HX_ARM64_CC_INVALID:
    case HX_ARM64_CC_AL:
    case HX_ARM64_CC_NV:
      return TRUE;
    default:
      return FALSE;
  }
}

static hx_boolean
hoox_arm64_relocator_rewrite_ldr (HooxArm64Relocator * self,
                                 HooxCodeGenCtx * ctx)
{
  hx_arm64_insn insn_id = ctx->insn->id;
  const hx_arm64_op * dst = &ctx->detail->operands[0];
  const hx_arm64_op * src = &ctx->detail->operands[1];
  hx_boolean dst_reg_is_fp_or_simd;
  hx_arm64_reg tmp_reg;

  if (src->type != HX_ARM64_OP_IMM)
    return FALSE;

  dst_reg_is_fp_or_simd =
      (dst->reg >= HX_ARM64_REG_S0 && dst->reg <= HX_ARM64_REG_S31) ||
      (dst->reg >= HX_ARM64_REG_D0 && dst->reg <= HX_ARM64_REG_D31) ||
      (dst->reg >= HX_ARM64_REG_Q0 && dst->reg <= HX_ARM64_REG_Q31);
  if (dst_reg_is_fp_or_simd)
  {
    tmp_reg = HX_ARM64_REG_X0;

    hoox_arm64_writer_put_push_reg_reg (ctx->output, tmp_reg, HX_ARM64_REG_X1);

    hoox_arm64_writer_put_ldr_reg_address (ctx->output, tmp_reg, src->imm);
    hx_assert (insn_id == HX_ARM64_INS_LDR);
    hoox_arm64_writer_put_ldr_reg_reg_offset (ctx->output, dst->reg, tmp_reg, 0);

    hoox_arm64_writer_put_pop_reg_reg (ctx->output, tmp_reg, HX_ARM64_REG_X1);
  }
  else
  {
    if (dst->reg >= HX_ARM64_REG_W0 && dst->reg <= HX_ARM64_REG_W28)
      tmp_reg = HX_ARM64_REG_X0 + (dst->reg - HX_ARM64_REG_W0);
    else if (dst->reg >= HX_ARM64_REG_W29 && dst->reg <= HX_ARM64_REG_W30)
      tmp_reg = HX_ARM64_REG_X29 + (dst->reg - HX_ARM64_REG_W29);
    else
      tmp_reg = dst->reg;

    hoox_arm64_writer_put_ldr_reg_address (ctx->output, tmp_reg, src->imm);
    if (insn_id == HX_ARM64_INS_LDR)
    {
      hoox_arm64_writer_put_ldr_reg_reg_offset (ctx->output, dst->reg, tmp_reg,
          0);
    }
    else
    {
      hoox_arm64_writer_put_ldrsw_reg_reg_offset (ctx->output, dst->reg, tmp_reg,
          0);
    }
  }

  return TRUE;
}

static hx_boolean
hoox_arm64_relocator_rewrite_adr (HooxArm64Relocator * self,
                                 HooxCodeGenCtx * ctx)
{
  const hx_arm64_op * dst = &ctx->detail->operands[0];
  const hx_arm64_op * label = &ctx->detail->operands[1];

  hx_assert (label->type == HX_ARM64_OP_IMM);

  hoox_arm64_writer_put_ldr_reg_address (ctx->output, dst->reg, label->imm);
  return TRUE;
}

static hx_boolean
hoox_arm64_relocator_rewrite_b (HooxArm64Relocator * self,
                               HooxCodeGenCtx * ctx)
{
  const hx_arm64_op * target = &ctx->detail->operands[0];

  hoox_arm64_writer_put_ldr_reg_address (ctx->output, HX_ARM64_REG_X16,
      hoox_arm64_writer_sign (ctx->output, target->imm));
  hoox_arm64_writer_put_br_reg (ctx->output, HX_ARM64_REG_X16);

  return TRUE;
}

static hx_boolean
hoox_arm64_relocator_rewrite_b_cond (HooxArm64Relocator * self,
                                    HooxCodeGenCtx * ctx)
{
  const hx_arm64_op * target = &ctx->detail->operands[0];
  hx_size unique_id = HX_POINTER_TO_SIZE (ctx->output->code) << 1;
  hx_constpointer is_true = HX_SIZE_TO_POINTER (unique_id | 1);
  hx_constpointer is_false = HX_SIZE_TO_POINTER (unique_id | 0);

  hoox_arm64_writer_put_b_cond_label (ctx->output, ctx->detail->cc, is_true);
  hoox_arm64_writer_put_b_label (ctx->output, is_false);

  hoox_arm64_writer_put_label (ctx->output, is_true);
  hoox_arm64_writer_put_ldr_reg_address (ctx->output, HX_ARM64_REG_X16,
      hoox_arm64_writer_sign (ctx->output, target->imm));
  hoox_arm64_writer_put_br_reg (ctx->output, HX_ARM64_REG_X16);

  hoox_arm64_writer_put_label (ctx->output, is_false);

  return TRUE;
}

static hx_boolean
hoox_arm64_relocator_rewrite_bl (HooxArm64Relocator * self,
                                HooxCodeGenCtx * ctx)
{
  const hx_arm64_op * target = &ctx->detail->operands[0];

  hoox_arm64_writer_put_ldr_reg_address (ctx->output, HX_ARM64_REG_LR,
      hoox_arm64_writer_sign (ctx->output, target->imm));
  hoox_arm64_writer_put_blr_reg (ctx->output, HX_ARM64_REG_LR);

  return TRUE;
}

static hx_boolean
hoox_arm64_relocator_rewrite_cbz (HooxArm64Relocator * self,
                                 HooxCodeGenCtx * ctx)
{
  const hx_arm64_op * source = &ctx->detail->operands[0];
  const hx_arm64_op * target = &ctx->detail->operands[1];
  hx_size unique_id = HX_POINTER_TO_SIZE (ctx->output->code) << 1;
  hx_constpointer is_true = HX_SIZE_TO_POINTER (unique_id | 1);
  hx_constpointer is_false = HX_SIZE_TO_POINTER (unique_id | 0);

  if (ctx->insn->id == HX_ARM64_INS_CBZ)
    hoox_arm64_writer_put_cbz_reg_label (ctx->output, source->reg, is_true);
  else
    hoox_arm64_writer_put_cbnz_reg_label (ctx->output, source->reg, is_true);
  hoox_arm64_writer_put_b_label (ctx->output, is_false);

  hoox_arm64_writer_put_label (ctx->output, is_true);
  hoox_arm64_writer_put_ldr_reg_address (ctx->output, HX_ARM64_REG_X16,
      hoox_arm64_writer_sign (ctx->output, target->imm));
  hoox_arm64_writer_put_br_reg (ctx->output, HX_ARM64_REG_X16);

  hoox_arm64_writer_put_label (ctx->output, is_false);

  return TRUE;
}

static hx_boolean
hoox_arm64_relocator_rewrite_tbz (HooxArm64Relocator * self,
                                 HooxCodeGenCtx * ctx)
{
  const hx_arm64_op * source = &ctx->detail->operands[0];
  const hx_arm64_op * bit = &ctx->detail->operands[1];
  const hx_arm64_op * target = &ctx->detail->operands[2];
  hx_size unique_id = HX_POINTER_TO_SIZE (ctx->output->code) << 1;
  hx_constpointer is_true = HX_SIZE_TO_POINTER (unique_id | 1);
  hx_constpointer is_false = HX_SIZE_TO_POINTER (unique_id | 0);

  if (ctx->insn->id == HX_ARM64_INS_TBZ)
  {
    hoox_arm64_writer_put_tbz_reg_imm_label (ctx->output, source->reg, bit->imm,
        is_true);
  }
  else
  {
    hoox_arm64_writer_put_tbnz_reg_imm_label (ctx->output, source->reg, bit->imm,
        is_true);
  }
  hoox_arm64_writer_put_b_label (ctx->output, is_false);

  hoox_arm64_writer_put_label (ctx->output, is_true);
  hoox_arm64_writer_put_ldr_reg_address (ctx->output, HX_ARM64_REG_X16,
      hoox_arm64_writer_sign (ctx->output, target->imm));
  hoox_arm64_writer_put_br_reg (ctx->output, HX_ARM64_REG_X16);

  hoox_arm64_writer_put_label (ctx->output, is_false);

  return TRUE;
}
