/*
 * Copyright (C) 2010-2025 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxthumbrelocator.h"

#include "hooxmemory.h"

#include <string.h>

#define HOOX_MAX_INPUT_INSN_COUNT (100)

typedef struct _HooxThumbCodeGenCtx HooxThumbCodeGenCtx;

struct _HooxThumbCodeGenCtx
{
  const hx_insn * insn;
  hx_arm * detail;
  HooxAddress pc;

  HooxThumbWriter * output;
};

static void hoox_stalker_relocator_advance (HooxThumbRelocator * self);
static void hoox_thumb_relocator_write_instruction (HooxThumbRelocator * self,
    const hx_insn * insn);
static void hoox_stalker_relocator_write_it_branches (HooxThumbRelocator * self);

static hx_boolean hoox_thumb_branch_is_unconditional (const hx_insn * insn);
static hx_boolean hoox_thumb_reg_dest_is_pc (const hx_insn * insn);
static hx_boolean hoox_thumb_reg_list_contains_pc (const hx_insn * insn,
    hx_uint8 start_index);

static hx_boolean hoox_thumb_relocator_rewrite_ldr (HooxThumbRelocator * self,
    HooxThumbCodeGenCtx * ctx);
static hx_boolean hoox_thumb_relocator_rewrite_vldr (HooxThumbRelocator * self,
    HooxThumbCodeGenCtx * ctx);
static hx_boolean hoox_thumb_relocator_rewrite_adr (HooxThumbRelocator * self,
    HooxThumbCodeGenCtx * ctx);
static hx_boolean hoox_thumb_relocator_rewrite_add (HooxThumbRelocator * self,
    HooxThumbCodeGenCtx * ctx);
static hx_boolean hoox_thumb_relocator_rewrite_b (HooxThumbRelocator * self,
    hx_mode target_mode, HooxThumbCodeGenCtx * ctx);
static hx_boolean hoox_thumb_relocator_rewrite_b_cond (HooxThumbRelocator * self,
    HooxThumbCodeGenCtx * ctx);
static hx_boolean hoox_thumb_relocator_rewrite_bl (HooxThumbRelocator * self,
    hx_mode target_mode, HooxThumbCodeGenCtx * ctx);
static hx_boolean hoox_thumb_relocator_rewrite_cbz (HooxThumbRelocator * self,
    HooxThumbCodeGenCtx * ctx);
static hx_boolean hoox_thumb_relocator_rewrite_it_block_start (
    HooxThumbRelocator * self, HooxThumbCodeGenCtx * ctx);
static void hoox_thumb_relocator_rewrite_it_block_else (HooxThumbRelocator * self,
    HooxITBlock * block);
static void hoox_thumb_relocator_rewrite_it_block_end (HooxThumbRelocator * self,
    HooxITBlock * block);
static void hoox_thumb_relocator_parse_it_block (HooxThumbRelocator * self,
    HooxITBlock * block, hx_uint16 it_insn);

static void hoox_commit_it_branch (HooxThumbWriter * writer, hx_pointer * id);

static hx_uint8 hoox_parse_it_instruction_block_size (hx_uint16 insn);

HooxThumbRelocator *
hoox_thumb_relocator_new (hx_constpointer input_code,
                         HooxThumbWriter * output)
{
  HooxThumbRelocator * relocator;

  relocator = hx_slice_new (HooxThumbRelocator);

  hoox_thumb_relocator_init (relocator, input_code, output);

  return relocator;
}

HooxThumbRelocator *
hoox_thumb_relocator_ref (HooxThumbRelocator * relocator)
{
  hx_atomic_int_inc (&relocator->ref_count);

  return relocator;
}

void
hoox_thumb_relocator_unref (HooxThumbRelocator * relocator)
{
  if (hx_atomic_int_dec_and_test (&relocator->ref_count))
  {
    hoox_thumb_relocator_clear (relocator);

    hx_slice_free (HooxThumbRelocator, relocator);
  }
}

void
hoox_thumb_relocator_init (HooxThumbRelocator * relocator,
                          hx_constpointer input_code,
                          HooxThumbWriter * output)
{
  relocator->ref_count = 1;

  hx_arch_register_arm ();
  hx_open (HX_ARCH_ARM, HX_MODE_THUMB | HX_MODE_V8, &relocator->capstone);
  hx_option (relocator->capstone, HX_OPT_DETAIL, HX_OPT_ON);
  relocator->input_insns = hx_new0 (hx_insn *, HOOX_MAX_INPUT_INSN_COUNT);

  relocator->output = NULL;

  hoox_thumb_relocator_reset (relocator, input_code, output);
  relocator->it_branch_type = HOOX_IT_BRANCH_SHORT;
}

void
hoox_thumb_relocator_clear (HooxThumbRelocator * relocator)
{
  hx_uint i;

  hoox_thumb_relocator_reset (relocator, NULL, NULL);

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
hoox_thumb_relocator_reset (HooxThumbRelocator * relocator,
                           hx_constpointer input_code,
                           HooxThumbWriter * output)
{
  relocator->input_start = input_code;
  relocator->input_cur = input_code;
  relocator->input_pc = HOOX_ADDRESS (input_code);

  if (output != NULL)
    hoox_thumb_writer_ref (output);
  if (relocator->output != NULL)
    hoox_thumb_writer_unref (relocator->output);
  relocator->output = output;

  relocator->inpos = 0;
  relocator->outpos = 0;

  relocator->eob = FALSE;
  relocator->eoi = FALSE;

  relocator->it_block.active = FALSE;
}

void
hoox_thumb_relocator_set_it_branch_type (HooxThumbRelocator * self,
                                        HooxITBranchType type)
{
  self->it_branch_type = type;
}

static hx_uint
hoox_thumb_relocator_inpos (HooxThumbRelocator * self)
{
  return self->inpos % HOOX_MAX_INPUT_INSN_COUNT;
}

static hx_uint
hoox_thumb_relocator_outpos (HooxThumbRelocator * self)
{
  return self->outpos % HOOX_MAX_INPUT_INSN_COUNT;
}

static void
hoox_thumb_relocator_increment_inpos (HooxThumbRelocator * self)
{
  self->inpos++;
  hx_assert (self->inpos > self->outpos);
}

static void
hoox_thumb_relocator_increment_outpos (HooxThumbRelocator * self)
{
  self->outpos++;
  hx_assert (self->outpos <= self->inpos);
}

hx_uint
hoox_thumb_relocator_read_one (HooxThumbRelocator * self,
                              const hx_insn ** instruction)
{
  const hx_uint8 * input_start = self->input_start;
  hx_insn ** insn_ptr, * insn;
  const uint8_t * code;
  size_t size;
  uint64_t address;
  hx_int it_block_size = 0;

  if (self->eoi)
    return 0;

  insn_ptr = &self->input_insns[hoox_thumb_relocator_inpos (self)];

  if (*insn_ptr == NULL)
    *insn_ptr = hx_insn_alloc (self->capstone);

  code = self->input_cur;
  size = 4;
  address = self->input_pc;
  insn = *insn_ptr;

  if (!hx_disasm_iter (self->capstone, &code, &size, &address, insn))
    return 0;

  if (!self->it_block.active)
  {
    switch (insn->id)
    {
      case HX_ARM_INS_B:
      case HX_ARM_INS_BX:
        self->eob = TRUE;
        self->eoi = hoox_thumb_branch_is_unconditional (insn);
        break;
      case HX_ARM_INS_CBZ:
      case HX_ARM_INS_CBNZ:
      case HX_ARM_INS_BL:
      case HX_ARM_INS_BLX:
        self->eob = TRUE;
        self->eoi = FALSE;
        break;
      case HX_ARM_INS_MOV:
      case HX_ARM_INS_LDR:
        self->eob = self->eoi = hoox_thumb_reg_dest_is_pc (insn);
        break;
      case HX_ARM_INS_POP:
        self->eob = self->eoi = hoox_thumb_reg_list_contains_pc (insn, 0);
        break;
      case HX_ARM_INS_LDM:
        self->eob = self->eoi = hoox_thumb_reg_list_contains_pc (insn, 1);
        break;
      case HX_ARM_INS_IT:
      {
        it_block_size = hoox_parse_it_instruction_block_size (
            HX_UINT16_FROM_LE (*((hx_uint16 *) self->input_cur)));
        self->eob = TRUE;
        break;
      }
      case HX_ARM_INS_TBB:
      case HX_ARM_INS_TBH:
        self->eob = self->eoi = TRUE;
        break;
      default:
        self->eob = FALSE;
        break;
    }
  }

  hoox_thumb_relocator_increment_inpos (self);

  if (instruction != NULL)
    *instruction = insn;

  self->input_cur = code;
  self->input_pc = address;

  if (it_block_size > 0)
  {
    self->it_block.active = TRUE;

    while (it_block_size--)
      hoox_thumb_relocator_read_one (self, NULL);

    self->it_block.active = FALSE;
  }

  return self->input_cur - input_start;
}

hx_boolean
hoox_thumb_relocator_is_eob_instruction (const hx_insn * instruction)
{
  switch (instruction->id)
  {
    case HX_ARM_INS_B:
    case HX_ARM_INS_BX:
    case HX_ARM_INS_CBZ:
    case HX_ARM_INS_CBNZ:
    case HX_ARM_INS_BL:
    case HX_ARM_INS_BLX:
    case HX_ARM_INS_TBB:
    case HX_ARM_INS_TBH:
      return TRUE;
    case HX_ARM_INS_LDR:
      return hoox_thumb_reg_dest_is_pc (instruction);
    case HX_ARM_INS_POP:
      return hoox_thumb_reg_list_contains_pc (instruction, 0);
    case HX_ARM_INS_LDM:
      return hoox_thumb_reg_list_contains_pc (instruction, 1);
    default:
      return FALSE;
  }
}

hx_insn *
hoox_thumb_relocator_peek_next_write_insn (HooxThumbRelocator * self)
{
  HooxITBlock * block = &self->it_block;

  if (block->active)
  {
    if (block->offset != block->size)
      return (hx_insn *) block->insns[block->offset];
  }

  if (self->outpos == self->inpos)
    return NULL;

  return self->input_insns[hoox_thumb_relocator_outpos (self)];
}

hx_pointer
hoox_thumb_relocator_peek_next_write_source (HooxThumbRelocator * self)
{
  hx_insn * next;

  next = hoox_thumb_relocator_peek_next_write_insn (self);
  if (next == NULL)
    return NULL;

  return HX_SIZE_TO_POINTER (next->address);
}

void
hoox_thumb_relocator_skip_one (HooxThumbRelocator * self)
{
  hoox_stalker_relocator_advance (self);
  hoox_stalker_relocator_write_it_branches (self);
}

hx_boolean
hoox_thumb_relocator_write_one (HooxThumbRelocator * self)
{
  const hx_insn * insn;

  insn = hoox_thumb_relocator_peek_next_write_insn (self);
  if (insn == NULL)
    return FALSE;

  hoox_stalker_relocator_advance (self);
  hoox_thumb_relocator_write_instruction (self, insn);
  hoox_stalker_relocator_write_it_branches (self);

  return TRUE;
}

hx_boolean
hoox_thumb_relocator_copy_one (HooxThumbRelocator * self)
{
  const hx_insn * insn;

  insn = hoox_thumb_relocator_peek_next_write_insn (self);
  if (insn == NULL)
    return FALSE;

  hoox_thumb_relocator_write_instruction (self, insn);

  return TRUE;
}

static void
hoox_stalker_relocator_advance (HooxThumbRelocator * self)
{
  HooxITBlock * block = &self->it_block;

  if (block->active)
    block->offset++;
  else
    hoox_thumb_relocator_increment_outpos (self);
}

static void
hoox_thumb_relocator_write_instruction (HooxThumbRelocator * self,
                                       const hx_insn * insn)
{
  HooxThumbCodeGenCtx ctx;
  hx_boolean rewritten = FALSE;

  ctx.insn = insn;
  ctx.detail = &insn->detail->arm;
  ctx.pc = insn->address + 4;
  ctx.output = self->output;

  switch (insn->id)
  {
    case HX_ARM_INS_LDR:
      rewritten = hoox_thumb_relocator_rewrite_ldr (self, &ctx);
      break;
    case HX_ARM_INS_VLDR:
      rewritten = hoox_thumb_relocator_rewrite_vldr (self, &ctx);
      break;
    case HX_ARM_INS_ADR:
      rewritten = hoox_thumb_relocator_rewrite_adr (self, &ctx);
      break;
    case HX_ARM_INS_ADD:
      rewritten = hoox_thumb_relocator_rewrite_add (self, &ctx);
      break;
    case HX_ARM_INS_B:
      if (self->it_block.active || hoox_thumb_branch_is_unconditional (ctx.insn))
        rewritten = hoox_thumb_relocator_rewrite_b (self, HX_MODE_THUMB, &ctx);
      else
        rewritten = hoox_thumb_relocator_rewrite_b_cond (self, &ctx);
      break;
    case HX_ARM_INS_BX:
      rewritten = hoox_thumb_relocator_rewrite_b (self, HX_MODE_ARM, &ctx);
      break;
    case HX_ARM_INS_BL:
      rewritten = hoox_thumb_relocator_rewrite_bl (self, HX_MODE_THUMB, &ctx);
      break;
    case HX_ARM_INS_BLX:
      rewritten = hoox_thumb_relocator_rewrite_bl (self, HX_MODE_ARM, &ctx);
      break;
    case HX_ARM_INS_CBZ:
    case HX_ARM_INS_CBNZ:
      rewritten = hoox_thumb_relocator_rewrite_cbz (self, &ctx);
      break;
    case HX_ARM_INS_IT:
      rewritten = hoox_thumb_relocator_rewrite_it_block_start (self, &ctx);
      break;
  }

  if (!rewritten)
    hoox_thumb_writer_put_bytes (ctx.output, insn->bytes, insn->size);
}

static void
hoox_stalker_relocator_write_it_branches (HooxThumbRelocator * self)
{
  HooxITBlock * block = &self->it_block;

  if (!block->active)
    return;

  if (block->offset == block->size)
  {
    hoox_thumb_relocator_rewrite_it_block_end (self, block);
    block->active = FALSE;
  }
  else if (block->offset == block->else_region_size)
  {
    hoox_thumb_relocator_rewrite_it_block_else (self, block);
  }
}

void
hoox_thumb_relocator_write_all (HooxThumbRelocator * self)
{
  HX_GNUC_UNUSED hx_uint count = 0;

  while (hoox_thumb_relocator_write_one (self))
    count++;

  hx_assert (count > 0);
}

hx_boolean
hoox_thumb_relocator_eob (HooxThumbRelocator * self)
{
  return self->eob;
}

hx_boolean
hoox_thumb_relocator_eoi (HooxThumbRelocator * self)
{
  return self->eoi;
}

hx_boolean
hoox_thumb_relocator_can_relocate (hx_pointer address,
                                  hx_uint min_bytes,
                                  HooxRelocationScenario scenario,
                                  hx_uint * maximum)
{
  hx_uint n = 0;
  hx_uint8 * buf;
  HooxThumbWriter cw;
  HooxThumbRelocator rl;
  const hx_insn * last_insn = NULL;
  hx_uint reloc_bytes;

  buf = hx_alloca (3 * min_bytes);
  hoox_thumb_writer_init (&cw, buf);

  hoox_thumb_relocator_init (&rl, address, &cw);

  do
  {
    const hx_insn * insn;
    hx_boolean safe_to_relocate_further;

    reloc_bytes = hoox_thumb_relocator_read_one (&rl, &insn);
    if (reloc_bytes == 0)
      break;
    last_insn = insn;

    n = reloc_bytes;

    if (scenario == HOOX_SCENARIO_ONLINE)
    {
      switch (insn->id)
      {
        case HX_ARM_INS_BL:
        case HX_ARM_INS_BLX:
        case HX_ARM_INS_SVC:
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

  if (rl.eoi)
  {
    if (n < min_bytes)
    {
      hx_boolean followed_by_padding =
          (last_insn->address % 4) == 0 && last_insn->size == 2 &&
          ((rl.input_cur[0] == 0x00 && rl.input_cur[1] == 0xbf) ||
           (rl.input_cur[0] == 0xd4 && rl.input_cur[1] == 0xd4));
      if (followed_by_padding)
        n += 2;
    }
  }
  else
  {
    hx_csh capstone;
    const hx_size max_code_size = 1024;
    hx_insn * insn;
    size_t count, i;
    hx_boolean eoi;

    hx_open (HX_ARCH_ARM, HX_MODE_THUMB, &capstone);
    hx_option (capstone, HX_OPT_DETAIL, HX_OPT_ON);

    hoox_ensure_code_readable (rl.input_cur, max_code_size);

    count = hx_disasm (capstone, rl.input_cur, max_code_size, rl.input_pc, 0,
        &insn);
    hx_assert (insn != NULL);

    eoi = FALSE;
    for (i = 0; i != count && !eoi; i++)
    {
      hx_uint id = insn[i].id;
      hx_arm * d = &insn[i].detail->arm;

      switch (id)
      {
        case HX_ARM_INS_B:
        case HX_ARM_INS_BX:
        case HX_ARM_INS_BL:
        case HX_ARM_INS_BLX:
        {
          hx_arm_op * op = &d->operands[0];
          if (op->type == HX_ARM_OP_IMM)
          {
            hx_ssize offset =
                (hx_ssize) op->imm - (hx_ssize) HX_POINTER_TO_SIZE (address);
            if (offset > 0 && offset < (hx_ssize) n)
              n = offset;
          }
          if (id == HX_ARM_INS_B || id == HX_ARM_INS_BX)
            eoi = d->cc == HX_ARM_CC_INVALID || d->cc == HX_ARM_CC_AL;
          break;
        }
        case HX_ARM_INS_POP:
          eoi = hx_reg_read (capstone, &insn[i], HX_ARM_REG_PC);
          break;
        default:
          break;
      }
    }

    hx_insn_free (insn, count);

    hx_close (&capstone);
  }

  hoox_thumb_relocator_clear (&rl);

  hoox_thumb_writer_clear (&cw);

  if (maximum != NULL)
    *maximum = n;

  return n >= min_bytes;
}

hx_uint
hoox_thumb_relocator_relocate (hx_pointer from,
                              hx_uint min_bytes,
                              hx_pointer to)
{
  HooxThumbWriter cw;
  HooxThumbRelocator rl;
  hx_uint reloc_bytes;

  hoox_thumb_writer_init (&cw, to);

  hoox_thumb_relocator_init (&rl, from, &cw);

  do
  {
    reloc_bytes = hoox_thumb_relocator_read_one (&rl, NULL);
    hx_assert (reloc_bytes != 0);
  }
  while (reloc_bytes < min_bytes);

  hoox_thumb_relocator_write_all (&rl);

  hoox_thumb_relocator_clear (&rl);
  hoox_thumb_writer_clear (&cw);

  return reloc_bytes;
}

static hx_boolean
hoox_thumb_branch_is_unconditional (const hx_insn * insn)
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
hoox_thumb_reg_dest_is_pc (const hx_insn * insn)
{
  return insn->detail->arm.operands[0].reg == HX_ARM_REG_PC;
}

static hx_boolean
hoox_thumb_reg_list_contains_pc (const hx_insn * insn,
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
hoox_thumb_relocator_rewrite_ldr (HooxThumbRelocator * self,
                                 HooxThumbCodeGenCtx * ctx)
{
  const hx_arm_op * dst = &ctx->detail->operands[0];
  const hx_arm_op * src = &ctx->detail->operands[1];
  HooxAddress absolute_pc;
  hx_arm_reg target;

  if (src->type != HX_ARM_OP_MEM || src->mem.base != HX_ARM_REG_PC)
    return FALSE;

  absolute_pc = ctx->pc & ~((HooxAddress) (4 - 1));
  absolute_pc += src->mem.disp;

  if (src->mem.index != HX_ARM_REG_INVALID)
  {
    /* FIXME: LDR with index register not yet supported. */
    hx_assert_not_reached ();
    return FALSE;
  }

  if (dst->reg == HX_ARM_REG_PC)
  {
    target = HX_ARM_REG_R0;

    /*
     * Push the current PC onto the stack to make space. This will be
     * overwritten with the correct address before it is popped.
     */
    hoox_thumb_writer_put_push_regs (ctx->output, 2, target, HX_ARM_REG_PC);
  }
  else
  {
    target = dst->reg;
  }

  hoox_thumb_writer_put_ldr_reg_address (ctx->output, target, absolute_pc);
  hoox_thumb_writer_put_ldr_reg_reg (ctx->output, target, target);

  if (dst->reg == HX_ARM_REG_PC)
  {
    hoox_thumb_writer_put_str_reg_reg_offset (ctx->output, target, HX_ARM_REG_SP,
        4);

    hoox_thumb_writer_put_pop_regs (ctx->output, 2, target, HX_ARM_REG_PC);
  }

  return TRUE;
}

static hx_boolean
hoox_thumb_relocator_rewrite_vldr (HooxThumbRelocator * self,
                                  HooxThumbCodeGenCtx * ctx)
{
  const hx_arm_op * dst = &ctx->detail->operands[0];
  const hx_arm_op * src = &ctx->detail->operands[1];
  HooxAddress absolute_pc;

  if (src->type != HX_ARM_OP_MEM || src->mem.base != HX_ARM_REG_PC)
    return FALSE;

  absolute_pc = ctx->pc & ~((HooxAddress) (4 - 1));
  absolute_pc += src->mem.disp;

  hoox_thumb_writer_put_push_regs (ctx->output, 1, HX_ARM_REG_R0);

  hoox_thumb_writer_put_ldr_reg_address (ctx->output, HX_ARM_REG_R0, absolute_pc);
  hoox_thumb_writer_put_vldr_reg_reg_offset (ctx->output, dst->reg, HX_ARM_REG_R0,
      0);

  hoox_thumb_writer_put_pop_regs (ctx->output, 1, HX_ARM_REG_R0);

  return TRUE;
}

static hx_boolean
hoox_thumb_relocator_rewrite_adr (HooxThumbRelocator * self,
                                 HooxThumbCodeGenCtx * ctx)
{
  const hx_arm_op * dst = &ctx->detail->operands[0];
  const hx_arm_op * offset = &ctx->detail->operands[1];
  HooxAddress absolute_pc;
  hx_arm_reg temp_reg;

  absolute_pc = ctx->pc & ~((HooxAddress) (4 - 1));
  temp_reg = (dst->reg != HX_ARM_REG_R0) ? HX_ARM_REG_R0 : HX_ARM_REG_R1;

  hoox_thumb_writer_put_push_regs (ctx->output, 1, temp_reg);
  hoox_thumb_writer_put_ldr_reg_address (ctx->output, dst->reg, absolute_pc);
  hoox_thumb_writer_put_ldr_reg_address (ctx->output, temp_reg, offset->imm);
  hoox_thumb_writer_put_add_reg_reg (ctx->output, dst->reg, temp_reg);
  hoox_thumb_writer_put_pop_regs (ctx->output, 1, temp_reg);

  return TRUE;
}

static hx_boolean
hoox_thumb_relocator_rewrite_add (HooxThumbRelocator * self,
                                 HooxThumbCodeGenCtx * ctx)
{
  const hx_arm_op * dst = &ctx->detail->operands[0];
  const hx_arm_op * src = &ctx->detail->operands[1];
  hx_arm_reg temp_reg;

  if (dst->reg == HX_ARM_REG_PC)
  {
    /* FIXME: ADD targeting PC not yet supported. */
    hx_assert_not_reached ();
    return FALSE;
  }

  if (ctx->detail->op_count != 2)
    return FALSE;
  else if (src->type != HX_ARM_OP_REG || src->reg != HX_ARM_REG_PC)
    return FALSE;

  temp_reg = (dst->reg != HX_ARM_REG_R0) ? HX_ARM_REG_R0 : HX_ARM_REG_R1;

  hoox_thumb_writer_put_push_regs (ctx->output, 1, temp_reg);
  hoox_thumb_writer_put_ldr_reg_address (ctx->output, temp_reg, ctx->pc);
  hoox_thumb_writer_put_add_reg_reg (ctx->output, dst->reg, temp_reg);
  hoox_thumb_writer_put_pop_regs (ctx->output, 1, temp_reg);

  return TRUE;
}

static hx_boolean
hoox_thumb_relocator_rewrite_b (HooxThumbRelocator * self,
                               hx_mode target_mode,
                               HooxThumbCodeGenCtx * ctx)
{
  const hx_arm_op * target = &ctx->detail->operands[0];

  if (target->type != HX_ARM_OP_IMM)
    return FALSE;

  hoox_thumb_writer_put_push_regs (ctx->output, 1, HX_ARM_REG_R0);
  hoox_thumb_writer_put_push_regs (ctx->output, 1, HX_ARM_REG_R0);
  hoox_thumb_writer_put_ldr_reg_address (ctx->output, HX_ARM_REG_R0,
      (target_mode == HX_MODE_THUMB) ? target->imm | 1 : target->imm);
  hoox_thumb_writer_put_str_reg_reg_offset (ctx->output, HX_ARM_REG_R0,
      HX_ARM_REG_SP, 4);
  hoox_thumb_writer_put_pop_regs (ctx->output, 2, HX_ARM_REG_R0, HX_ARM_REG_PC);

  return TRUE;
}

static hx_boolean
hoox_thumb_relocator_rewrite_b_cond (HooxThumbRelocator * self,
                                    HooxThumbCodeGenCtx * ctx)
{
  const hx_arm_op * target = &ctx->detail->operands[0];
  hx_size unique_id = HX_POINTER_TO_SIZE (ctx->output->code) << 1;
  hx_constpointer is_true = HX_SIZE_TO_POINTER (unique_id | 1);
  hx_constpointer is_false = HX_SIZE_TO_POINTER (unique_id | 0);

  if (target->type != HX_ARM_OP_IMM)
    return FALSE;

  hoox_thumb_writer_put_b_cond_label (ctx->output, ctx->detail->cc, is_true);
  hoox_thumb_writer_put_b_label (ctx->output, is_false);

  hoox_thumb_writer_put_label (ctx->output, is_true);
  hoox_thumb_writer_put_push_regs (ctx->output, 1, HX_ARM_REG_R0);
  hoox_thumb_writer_put_push_regs (ctx->output, 1, HX_ARM_REG_R0);
  hoox_thumb_writer_put_ldr_reg_address (ctx->output, HX_ARM_REG_R0,
      target->imm | 1);
  hoox_thumb_writer_put_str_reg_reg_offset (ctx->output, HX_ARM_REG_R0,
      HX_ARM_REG_SP, 4);
  hoox_thumb_writer_put_pop_regs (ctx->output, 2, HX_ARM_REG_R0, HX_ARM_REG_PC);

  hoox_thumb_writer_put_label (ctx->output, is_false);

  return TRUE;
}

static hx_boolean
hoox_thumb_relocator_rewrite_bl (HooxThumbRelocator * self,
                                hx_mode target_mode,
                                HooxThumbCodeGenCtx * ctx)
{
  const hx_arm_op * target = &ctx->detail->operands[0];

  if (target->type != HX_ARM_OP_IMM)
    return FALSE;

  hoox_thumb_writer_put_push_regs (ctx->output, 1, HX_ARM_REG_R0);
  hoox_thumb_writer_put_ldr_reg_address (ctx->output, HX_ARM_REG_R0,
      (target_mode == HX_MODE_THUMB) ? target->imm | 1 : target->imm);
  hoox_thumb_writer_put_mov_reg_reg (ctx->output, HX_ARM_REG_LR, HX_ARM_REG_R0);
  hoox_thumb_writer_put_pop_regs (ctx->output, 1, HX_ARM_REG_R0);
  hoox_thumb_writer_put_blx_reg (ctx->output, HX_ARM_REG_LR);

  return TRUE;
}

static hx_boolean
hoox_thumb_relocator_rewrite_cbz (HooxThumbRelocator * self,
                                 HooxThumbCodeGenCtx * ctx)
{
  const hx_arm_op * source = &ctx->detail->operands[0];
  const hx_arm_op * target = &ctx->detail->operands[1];
  hx_size unique_id = HX_POINTER_TO_SIZE (ctx->output->code) << 1;
  hx_constpointer is_true = HX_SIZE_TO_POINTER (unique_id | 1);
  hx_constpointer is_false = HX_SIZE_TO_POINTER (unique_id | 0);

  if (ctx->insn->id == HX_ARM_INS_CBZ)
    hoox_thumb_writer_put_cbz_reg_label (ctx->output, source->reg, is_true);
  else
    hoox_thumb_writer_put_cbnz_reg_label (ctx->output, source->reg, is_true);
  hoox_thumb_writer_put_b_label (ctx->output, is_false);

  hoox_thumb_writer_put_label (ctx->output, is_true);
  hoox_thumb_writer_put_push_regs (ctx->output, 1, HX_ARM_REG_R0);
  hoox_thumb_writer_put_push_regs (ctx->output, 1, HX_ARM_REG_R0);
  hoox_thumb_writer_put_ldr_reg_address (ctx->output, HX_ARM_REG_R0,
      target->imm | 1);
  hoox_thumb_writer_put_str_reg_reg_offset (ctx->output, HX_ARM_REG_R0,
      HX_ARM_REG_SP, 4);
  hoox_thumb_writer_put_pop_regs (ctx->output, 2, HX_ARM_REG_R0, HX_ARM_REG_PC);

  hoox_thumb_writer_put_label (ctx->output, is_false);

  return TRUE;
}

static hx_boolean
hoox_thumb_relocator_rewrite_it_block_start (HooxThumbRelocator * self,
                                            HooxThumbCodeGenCtx * ctx)
{
  HooxITBlock * block = &self->it_block;
  const hx_insn * insn = ctx->insn;
  hx_arm_cc cc = insn->detail->arm.cc;
  hx_uint16 it_insn;

  memcpy (&it_insn, ctx->insn->bytes, sizeof (hx_uint16));
  it_insn = HX_UINT16_FROM_LE (it_insn);

  hoox_thumb_relocator_parse_it_block (self, block, it_insn);

  block->active = TRUE;
  block->cc = cc;
  block->then_label = self->output->code + 1;
  block->end_label = NULL;

  if (block->cc == HX_ARM_CC_AL)
    return TRUE;

  switch (self->it_branch_type)
  {
    case HOOX_IT_BRANCH_SHORT:
      hoox_thumb_writer_put_b_cond_label (self->output, cc, block->then_label);
      break;
    case HOOX_IT_BRANCH_LONG:
      hoox_thumb_writer_put_b_cond_label_wide (self->output, cc,
          block->then_label);
      break;
    default:
      hx_assert_not_reached ();
  }

  return TRUE;
}

static void
hoox_thumb_relocator_rewrite_it_block_else (HooxThumbRelocator * self,
                                           HooxITBlock * block)
{
  block->end_label = self->output->code + 1;

  if (block->cc == HX_ARM_CC_AL)
    return;

  switch (self->it_branch_type)
  {
    case HOOX_IT_BRANCH_SHORT:
      hoox_thumb_writer_put_b_label (self->output, block->end_label);
      break;
    case HOOX_IT_BRANCH_LONG:
      hoox_thumb_writer_put_b_label_wide (self->output, block->end_label);
      break;
    default:
      hx_assert_not_reached ();
  }

  hoox_commit_it_branch (self->output, &block->then_label);
}

static void
hoox_thumb_relocator_rewrite_it_block_end (HooxThumbRelocator * self,
                                          HooxITBlock * block)
{
  if (block->cc == HX_ARM_CC_AL)
    return;

  hoox_commit_it_branch (self->output, &block->then_label);

  hoox_commit_it_branch (self->output, &block->end_label);
}

static void
hoox_thumb_relocator_parse_it_block (HooxThumbRelocator * self,
                                    HooxITBlock * block,
                                    hx_uint16 it_insn)
{
  hx_uint8 then_bit, then_insn_count, i;
  const hx_insn * then_insn[4];

  block->offset = 0;
  block->size = hoox_parse_it_instruction_block_size (it_insn);
  block->else_region_size = 0;

  then_bit = (it_insn >> 4) & 1;
  then_insn_count = 0;

  for (i = 0; i != block->size; i++)
  {
    const hx_insn * child;
    hx_uint8 cond_bit;

    child = hoox_thumb_relocator_peek_next_write_insn (self);
    hx_assert (child != NULL);
    hoox_thumb_relocator_increment_outpos (self);

    cond_bit = (it_insn >> (4 - i)) & 1;
    if (cond_bit != then_bit)
      block->insns[block->else_region_size++] = child;
    else
      then_insn[then_insn_count++] = child;
  }

  for (i = block->else_region_size; i != block->size; i++)
    block->insns[i] = then_insn[i - block->else_region_size];
}

static void
hoox_commit_it_branch (HooxThumbWriter * writer,
                      hx_pointer * id)
{
  if (*id == NULL)
    return;

  hoox_thumb_writer_put_label (writer, *id);
  hoox_thumb_writer_commit_label (writer, *id);
  *id = NULL;
}

static hx_uint8
hoox_parse_it_instruction_block_size (hx_uint16 insn)
{
  if ((insn & 0x1) != 0)
    return 4;

  if ((insn & 0x2) != 0)
    return 3;

  if ((insn & 0x4) != 0)
    return 2;

  return 1;
}

