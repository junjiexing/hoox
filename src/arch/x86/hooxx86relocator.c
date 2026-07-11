/*
 * Copyright (C) 2009-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxx86relocator.h"

#include "hooxlibc.h"
#include "hooxmemory.h"
#include "hooxx86reader.h"

#include <string.h>

#define HOOX_MAX_INPUT_INSN_COUNT (100)

typedef struct _HooxCodeGenCtx HooxCodeGenCtx;

struct _HooxCodeGenCtx
{
  hx_insn * insn;
  HooxAddress pc;

  HooxX86Writer * code_writer;
};

static hx_boolean hoox_x86_relocator_write_one_instruction (
    HooxX86Relocator * self);
static void hoox_x86_relocator_put_label_for (HooxX86Relocator * self,
    hx_insn * insn);

static hx_boolean hoox_x86_relocator_rewrite_unconditional_branch (
    HooxX86Relocator * self, HooxCodeGenCtx * ctx);
static hx_boolean hoox_x86_relocator_rewrite_conditional_branch (
    HooxX86Relocator * self, HooxCodeGenCtx * ctx);
static hx_boolean hoox_x86_relocator_rewrite_if_rip_relative (
    HooxX86Relocator * self, HooxCodeGenCtx * ctx);

static hx_boolean hoox_x86_call_is_to_next_instruction (hx_insn * insn);
static hx_boolean hoox_x86_call_try_parse_get_pc_thunk (hx_insn * insn,
    HooxCpuType cpu_type, HooxX86Reg * pc_reg);

HooxX86Relocator *
hoox_x86_relocator_new (hx_constpointer input_code,
                       HooxX86Writer * output)
{
  HooxX86Relocator * relocator;

  relocator = hx_slice_new (HooxX86Relocator);

  hoox_x86_relocator_init (relocator, input_code, output);

  return relocator;
}

HooxX86Relocator *
hoox_x86_relocator_ref (HooxX86Relocator * relocator)
{
  hx_atomic_int_inc (&relocator->ref_count);

  return relocator;
}

void
hoox_x86_relocator_unref (HooxX86Relocator * relocator)
{
  if (hx_atomic_int_dec_and_test (&relocator->ref_count))
  {
    hoox_x86_relocator_clear (relocator);

    hx_slice_free (HooxX86Relocator, relocator);
  }
}

void
hoox_x86_relocator_init (HooxX86Relocator * relocator,
                        hx_constpointer input_code,
                        HooxX86Writer * output)
{
  relocator->ref_count = 1;

  hx_arch_register_x86 ();
  hx_open (HX_ARCH_X86,
      (output->target_cpu == HOOX_CPU_AMD64) ? HX_MODE_64 : HX_MODE_32,
      &relocator->capstone);
  hx_option (relocator->capstone, HX_OPT_DETAIL, HX_OPT_ON);
  relocator->input_insns = hx_new0 (hx_insn *, HOOX_MAX_INPUT_INSN_COUNT);

  relocator->output = NULL;

  hoox_x86_relocator_reset (relocator, input_code, output);
}

void
hoox_x86_relocator_clear (HooxX86Relocator * relocator)
{
  hx_uint i;

  hoox_x86_relocator_reset (relocator, NULL, NULL);

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
hoox_x86_relocator_reset (HooxX86Relocator * relocator,
                         hx_constpointer input_code,
                         HooxX86Writer * output)
{
  relocator->input_start = input_code;
  relocator->input_cur = input_code;
  relocator->input_pc = HOOX_ADDRESS (input_code);

  if (output != NULL)
    hoox_x86_writer_ref (output);
  if (relocator->output != NULL)
    hoox_x86_writer_unref (relocator->output);
  relocator->output = output;

  relocator->inpos = 0;
  relocator->outpos = 0;

  relocator->eob = FALSE;
  relocator->eoi = FALSE;
}

static hx_uint
hoox_x86_relocator_inpos (HooxX86Relocator * self)
{
  return self->inpos % HOOX_MAX_INPUT_INSN_COUNT;
}

static hx_uint
hoox_x86_relocator_outpos (HooxX86Relocator * self)
{
  return self->outpos % HOOX_MAX_INPUT_INSN_COUNT;
}

static void
hoox_x86_relocator_increment_inpos (HooxX86Relocator * self)
{
  self->inpos++;
  hx_assert (self->inpos > self->outpos);
}

static void
hoox_x86_relocator_increment_outpos (HooxX86Relocator * self)
{
  self->outpos++;
  hx_assert (self->outpos <= self->inpos);
}

hx_uint
hoox_x86_relocator_read_one (HooxX86Relocator * self,
                            const hx_insn ** instruction)
{
  hx_insn ** insn_ptr, * insn;
  const uint8_t * code;
  size_t size;
  uint64_t address;

  if (self->eoi)
    return 0;

  insn_ptr = &self->input_insns[hoox_x86_relocator_inpos (self)];

  if (*insn_ptr == NULL)
    *insn_ptr = hx_insn_alloc (self->capstone);

  code = self->input_cur;
  size = 16;
  address = self->input_pc;
  insn = *insn_ptr;

  if (!hx_disasm_iter (self->capstone, &code, &size, &address, insn))
    return 0;

  switch (insn->id)
  {
    case HX_INS_JECXZ:
    case HX_INS_JRCXZ:
      self->eob = TRUE;
      break;

    case HX_INS_JMP:
    case HX_INS_RET:
    case HX_INS_RETF:
      self->eob = TRUE;
      self->eoi = TRUE;
      break;

    case HX_INS_CALL:
      self->eob = !hoox_x86_call_is_to_next_instruction (insn) &&
          !hoox_x86_call_try_parse_get_pc_thunk (insn, self->output->target_cpu,
              NULL);
      self->eoi = FALSE;
      break;

    default:
      if (hoox_x86_reader_insn_is_jcc (insn))
        self->eob = TRUE;
      else
        self->eob = FALSE;
      break;
  }

  hoox_x86_relocator_increment_inpos (self);

  if (instruction != NULL)
    *instruction = insn;

  self->input_cur = code;
  self->input_pc = address;

  return self->input_cur - self->input_start;
}

hx_insn *
hoox_x86_relocator_peek_next_write_insn (HooxX86Relocator * self)
{
  if (self->outpos == self->inpos)
    return NULL;

  return self->input_insns[hoox_x86_relocator_outpos (self)];
}

hx_pointer
hoox_x86_relocator_peek_next_write_source (HooxX86Relocator * self)
{
  hx_insn * next;

  next = hoox_x86_relocator_peek_next_write_insn (self);
  if (next == NULL)
    return NULL;

  return HX_SIZE_TO_POINTER (next->address);
}

void
hoox_x86_relocator_skip_one (HooxX86Relocator * self)
{
  hx_insn * next;

  next = hoox_x86_relocator_peek_next_write_insn (self);
  hx_assert (next != NULL);
  hoox_x86_relocator_increment_outpos (self);

  hoox_x86_relocator_put_label_for (self, next);
}

void
hoox_x86_relocator_skip_one_no_label (HooxX86Relocator * self)
{
  hoox_x86_relocator_peek_next_write_insn (self);
  hoox_x86_relocator_increment_outpos (self);
}

hx_boolean
hoox_x86_relocator_write_one (HooxX86Relocator * self)
{
  hx_insn * cur;

  if ((cur = hoox_x86_relocator_peek_next_write_insn (self)) == NULL)
    return FALSE;

  hoox_x86_relocator_put_label_for (self, cur);

  return hoox_x86_relocator_write_one_instruction (self);
}

hx_boolean
hoox_x86_relocator_write_one_no_label (HooxX86Relocator * self)
{
  return hoox_x86_relocator_write_one_instruction (self);
}

static hx_boolean
hoox_x86_relocator_write_one_instruction (HooxX86Relocator * self)
{
  hx_insn * insn;
  HooxCodeGenCtx ctx;
  hx_boolean rewritten = FALSE;

  if ((insn = hoox_x86_relocator_peek_next_write_insn (self)) == NULL)
    return FALSE;
  hoox_x86_relocator_increment_outpos (self);

  ctx.insn = insn;
  ctx.pc = insn->address + insn->size;

  ctx.code_writer = self->output;

  switch (insn->id)
  {
    case HX_INS_CALL:
    case HX_INS_JMP:
      rewritten = hoox_x86_relocator_rewrite_unconditional_branch (self, &ctx);
      break;

    case HX_INS_JECXZ:
    case HX_INS_JRCXZ:
      rewritten = hoox_x86_relocator_rewrite_conditional_branch (self, &ctx);
      break;

#ifdef HAVE_LINUX
    case HX_INS_SYSCALL:
      /*
       * On x64 platforms in compatibility (32-bit) mode, it is typical to mode
       * switch using the SYSCALL instruction. However, the kernel hard-codes
       * the return address.
       *
       * https://github.com/torvalds/linux/blob/c3d0e3fd41b7f0f5d5d5b6022ab7e813f04ea727/arch/x86/entry/common.c#L165
       *
       * This means if we are instrumenting some code in Stalker which uses a
       * VSYSCALL instruction, we will not return to the instrumented code, but
       * rather the uninstrumented original and hence the current execution flow
       * continues, but is no longer stalked.
       *
       * The kernel states that the SYSCALL instruction should *only* occur in
       * the VDSO for this reason (and many others).
       *
       * https://github.com/torvalds/linux/blob/c3d0e3fd41b7f0f5d5d5b6022ab7e813f04ea727/arch/x86/entry/entry_64_compat.S#L158
       *
       * On some x86 processors, however, the SYSCALL instruction is not
       * supported and is instead interpreted as a NOP. For this reason,
       * __kernel_vsyscall immediately follows the SYSCALL instruction with a
       * good old fashioned INT 0x80. This form of mode-switch does preserve a
       * return address and hence does not encounter this problem.
       *
       * This is part of the reason why the return address for SYSCALL is hard
       * coded, since the return address would need to be advanced past the
       * INT 0x80 to avoid the syscall being called twice on systems which
       * support SYSCALL.
       *
       * Therefore if we simply omit any VSYSCALL instructions, our application
       * will behave as if it were running on an older CPU without support for
       * that instruction. There may be a performance penalty to pay for the
       * slower mode-switch instruction, but mode-switches are inherently slow
       * anyways.
       */
      if (self->output->target_cpu == HOOX_CPU_IA32)
        rewritten = TRUE;
      break;
#endif

    default:
      if (hoox_x86_reader_insn_is_jcc (insn))
        rewritten = hoox_x86_relocator_rewrite_conditional_branch (self, &ctx);
      else if (self->output->target_cpu == HOOX_CPU_AMD64)
        rewritten = hoox_x86_relocator_rewrite_if_rip_relative (self, &ctx);
      break;
  }

  if (!rewritten)
    hoox_x86_writer_put_bytes (ctx.code_writer, insn->bytes, insn->size);

  return TRUE;
}

void
hoox_x86_relocator_write_all (HooxX86Relocator * self)
{
  HX_GNUC_UNUSED hx_uint count = 0;

  while (hoox_x86_relocator_write_one (self))
    count++;

  hx_assert (count > 0);
}

hx_boolean
hoox_x86_relocator_eob (HooxX86Relocator * self)
{
  return self->eob;
}

hx_boolean
hoox_x86_relocator_eoi (HooxX86Relocator * self)
{
  return self->eoi;
}

static void
hoox_x86_relocator_put_label_for (HooxX86Relocator * self,
                                 hx_insn * insn)
{
  hoox_x86_writer_put_label (self->output, HX_SIZE_TO_POINTER (insn->address));
}

hx_boolean
hoox_x86_relocator_can_relocate (hx_pointer address,
                                hx_uint min_bytes,
                                HooxRelocationScenario scenario,
                                hx_uint * maximum)
{
  hx_uint n = 0;
  hx_uint8 * buf;
  HooxX86Writer cw;
  HooxX86Relocator rl;
  hx_uint reloc_bytes;

  buf = hx_alloca (3 * min_bytes);
  hoox_x86_writer_init (&cw, buf);

  hoox_x86_relocator_init (&rl, address, &cw);

  do
  {
    const hx_insn * insn;
    hx_boolean safe_to_relocate_further;

    reloc_bytes = hoox_x86_relocator_read_one (&rl, &insn);
    if (reloc_bytes == 0)
      break;

    n = reloc_bytes;

    if (scenario == HOOX_SCENARIO_ONLINE)
    {
      switch (insn->id)
      {
        case HX_INS_CALL:
        case HX_INS_SYSCALL:
        case HX_INS_SYSENTER:
        case HX_INS_INT:
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

  hoox_x86_relocator_clear (&rl);

  hoox_x86_writer_clear (&cw);

  if (maximum != NULL)
    *maximum = n;

  return n >= min_bytes;
}

hx_uint
hoox_x86_relocator_relocate (hx_pointer from,
                            hx_uint min_bytes,
                            hx_pointer to)
{
  HooxX86Writer cw;
  HooxX86Relocator rl;
  hx_uint reloc_bytes;

  hoox_x86_writer_init (&cw, to);

  hoox_x86_relocator_init (&rl, from, &cw);

  do
  {
    reloc_bytes = hoox_x86_relocator_read_one (&rl, NULL);
    hx_assert (reloc_bytes != 0);
  }
  while (reloc_bytes < min_bytes);

  hoox_x86_relocator_write_all (&rl);

  hoox_x86_relocator_clear (&rl);
  hoox_x86_writer_clear (&cw);

  return reloc_bytes;
}

static hx_boolean
hoox_x86_relocator_rewrite_unconditional_branch (HooxX86Relocator * self,
                                                HooxCodeGenCtx * ctx)
{
  hx_insn * insn = ctx->insn;
  hx_x86_op * op = &insn->detail->x86.operands[0];
  HooxX86Writer * cw = ctx->code_writer;

  if (ctx->insn->id == HX_INS_CALL)
  {
    HooxX86Reg pc_reg;

    if (hoox_x86_call_is_to_next_instruction (insn))
    {
      if (cw->target_cpu == HOOX_CPU_AMD64)
      {
        hoox_x86_writer_put_push_reg (cw, HOOX_HX_XAX);
        hoox_x86_writer_put_mov_reg_address (cw, HOOX_HX_XAX, ctx->pc);
        hoox_x86_writer_put_xchg_reg_reg_ptr (cw, HOOX_HX_XAX, HOOX_HX_XSP);
      }
      else
      {
        hoox_x86_writer_put_push_u32 (cw, ctx->pc);
      }

      return TRUE;
    }
    else if (hoox_x86_call_try_parse_get_pc_thunk (insn,
        self->output->target_cpu, &pc_reg))
    {
      hoox_x86_writer_put_mov_reg_u32 (cw, pc_reg, ctx->pc);
      return TRUE;
    }
  }

  if (op->type == HX_OP_IMM)
  {
    if (insn->id == HX_INS_CALL)
      hoox_x86_writer_put_call_address (cw, op->imm);
    else
      hoox_x86_writer_put_jmp_address (cw, op->imm);

    return TRUE;
  }
  else if ((insn->id == HX_INS_CALL || insn->id == HX_INS_JMP) &&
      op->type == HX_OP_MEM)
  {
    if (self->output->target_cpu == HOOX_CPU_AMD64)
      return hoox_x86_relocator_rewrite_if_rip_relative (self, ctx);

    return FALSE;
  }
  else if (insn->id == HX_INS_JMP && op->type == HX_OP_IMM && op->size == 8)
  {
    return FALSE;
  }
  else if (op->type == HX_OP_REG)
  {
    return FALSE;
  }
  else
  {
    /* FIXME */
    hx_abort ();
  }
}

static hx_boolean
hoox_x86_relocator_rewrite_conditional_branch (HooxX86Relocator * self,
                                              HooxCodeGenCtx * ctx)
{
  hx_x86_op * op = &ctx->insn->detail->x86.operands[0];

  if (op->type == HX_OP_IMM)
  {
    HooxAddress target = op->imm;

    if (target >= self->input_pc - (self->input_cur - self->input_start) &&
        target < self->input_pc)
    {
      hoox_x86_writer_put_jcc_short_label (ctx->code_writer, ctx->insn->id,
          HX_SIZE_TO_POINTER (target), HOOX_NO_HINT);
    }
    else if (ctx->insn->id == HX_INS_JECXZ || ctx->insn->id == HX_INS_JRCXZ ||
        !hoox_x86_writer_put_jcc_near (ctx->code_writer, ctx->insn->id,
          HX_SIZE_TO_POINTER (target), HOOX_NO_HINT))
    {
      hx_size unique_id = HX_POINTER_TO_SIZE (ctx->code_writer->code) << 1;
      hx_constpointer is_true = HX_SIZE_TO_POINTER (unique_id | 1);
      hx_constpointer is_false = HX_SIZE_TO_POINTER (unique_id | 0);

      hoox_x86_writer_put_jcc_short_label (ctx->code_writer, ctx->insn->id,
          is_true, HOOX_NO_HINT);
      hoox_x86_writer_put_jmp_short_label (ctx->code_writer, is_false);

      hoox_x86_writer_put_label (ctx->code_writer, is_true);
      hoox_x86_writer_put_jmp_address (ctx->code_writer, target);

      hoox_x86_writer_put_label (ctx->code_writer, is_false);
    }
  }
  else
  {
    /* FIXME */
    hx_abort ();
  }

  return TRUE;
}

static hx_boolean
hoox_x86_relocator_rewrite_if_rip_relative (HooxX86Relocator * self,
                                           HooxCodeGenCtx * ctx)
{
  hx_insn * insn = ctx->insn;
  hx_x86 * x86 = &insn->detail->x86;
  HooxX86Writer * cw = ctx->code_writer;
  hx_uint mod, reg, rm;
  hx_boolean is_rip_relative;
  HooxAddress address;
  hx_ssize offset;
  HooxX86Reg cpu_regs[7] = {
    HOOX_HX_RAX, HOOX_HX_RCX, HOOX_HX_RDX, HOOX_HX_RBX, HOOX_HX_RBP,
    HOOX_HX_RSI, HOOX_HX_RDI
  };
  hx_x86_reg hx_regs[7] = {
    HX_REG_RAX, HX_REG_RCX, HX_REG_RDX, HX_REG_RBX, HX_REG_RBP,
    HX_REG_RSI, HX_REG_RDI
  };
  hx_int rip_reg_index, i;
  HooxX86Reg other_reg, rip_reg;
  HooxAbiType target_abi = self->output->target_abi;
  hx_uint8 code[16];

  if (x86->encoding.modrm_offset == 0)
    return FALSE;

  mod = (x86->modrm & 0xc0) >> 6;
  reg = (x86->modrm & 0x38) >> 3;
  rm  = (x86->modrm & 0x07) >> 0;

  is_rip_relative = (mod == 0 && rm == 5);
  if (!is_rip_relative)
    return FALSE;

  address = ctx->pc + x86->disp;
  offset = address - (cw->pc + insn->size);

  if (offset >= HX_MININT32 && offset <= HX_MAXINT32)
  {
    const hx_int32 raw_offset = HX_INT32_TO_LE ((hx_int32) offset);
    hoox_memcpy (code, insn->bytes, insn->size);
    hoox_memcpy (code + x86->encoding.disp_offset, &raw_offset,
        sizeof (raw_offset));
    hoox_x86_writer_put_bytes (cw, code, insn->size);
    return TRUE;
  }

  if (insn->id == HX_INS_CALL || insn->id == HX_INS_JMP)
  {
    union
    {
      hx_int32 value;
      hx_uint8 bytes[4];
    } i32;
    hx_int32 distance;
    hx_uint64 * return_address_placeholder = NULL;

    hoox_memcpy (i32.bytes, insn->bytes + insn->size - sizeof (hx_int32),
        sizeof (i32.bytes));
    distance = HX_INT32_FROM_LE (i32.value);

    if (insn->id == HX_INS_CALL)
    {
      hoox_x86_writer_put_push_reg (cw, HOOX_HX_RAX);
      hoox_x86_writer_put_mov_reg_address (cw, HOOX_HX_RAX, 0);
      return_address_placeholder = (hx_uint64 *) (cw->code - sizeof (hx_uint64));
      hoox_x86_writer_put_xchg_reg_reg_ptr (cw, HOOX_HX_RAX, HOOX_HX_RSP);
    }

    hoox_x86_writer_put_push_reg (cw, HOOX_HX_RAX);
    hoox_x86_writer_put_mov_reg_address (cw, HOOX_HX_RAX, ctx->pc + distance);
    hoox_x86_writer_put_mov_reg_reg_ptr (cw, HOOX_HX_RAX, HOOX_HX_RAX);
    hoox_x86_writer_put_xchg_reg_reg_ptr (cw, HOOX_HX_RAX, HOOX_HX_RSP);
    hoox_x86_writer_put_ret (cw);

    if (insn->id == HX_INS_CALL)
    {
      *return_address_placeholder = cw->pc;
    }

    return TRUE;
  }

  other_reg = (HooxX86Reg) (HOOX_HX_RAX + reg);

  rip_reg_index = -1;
  for (i = 0; i != HX_N_ELEMENTS (hx_regs) && rip_reg_index == -1; i++)
  {
    if (cpu_regs[i] == other_reg)
      continue;
    if (insn->id == HX_INS_CMPXCHG && cpu_regs[i] == HOOX_HX_RAX)
      continue;
    if (hx_reg_read (self->capstone, ctx->insn, hx_regs[i]))
      continue;
    if (hx_reg_write (self->capstone, ctx->insn, hx_regs[i]))
      continue;
    rip_reg_index = i;
  }
  hx_assert (rip_reg_index != -1);
  rip_reg = cpu_regs[rip_reg_index];

  mod = 2;
  rm = rip_reg - HOOX_HX_RAX;

  if (insn->id == HX_INS_PUSH)
  {
    hoox_x86_writer_put_push_reg (cw, HOOX_HX_RAX);
  }

  if (target_abi == HOOX_ABI_UNIX)
  {
    hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_RSP, HOOX_HX_RSP,
        -HOOX_RED_ZONE_SIZE);
  }
  hoox_x86_writer_put_push_reg (cw, rip_reg);
  hoox_x86_writer_put_mov_reg_address (cw, rip_reg, ctx->pc);

  if (insn->id == HX_INS_PUSH)
  {
    hoox_x86_writer_put_mov_reg_reg_offset_ptr (cw, rip_reg, rip_reg, x86->disp);
    hoox_x86_writer_put_mov_reg_offset_ptr_reg (cw,
        HOOX_HX_RSP,
        0x08 + ((target_abi == HOOX_ABI_UNIX) ? HOOX_RED_ZONE_SIZE : 0),
        rip_reg);
  }
  else
  {
    hoox_memcpy (code, insn->bytes, insn->size);
    code[x86->encoding.modrm_offset] = (mod << 6) | (reg << 3) | rm;
    hoox_x86_writer_put_bytes (cw, code, insn->size);
  }

  hoox_x86_writer_put_pop_reg (cw, rip_reg);
  if (target_abi == HOOX_ABI_UNIX)
  {
    hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_RSP, HOOX_HX_RSP,
        HOOX_RED_ZONE_SIZE);
  }

  return TRUE;
}

static hx_boolean
hoox_x86_call_is_to_next_instruction (hx_insn * insn)
{
  hx_x86_op * op = &insn->detail->x86.operands[0];

  return (op->type == HX_OP_IMM
      && (uint64_t) op->imm == insn->address + insn->size);
}

static hx_boolean
hoox_x86_call_try_parse_get_pc_thunk (hx_insn * insn,
                                     HooxCpuType cpu_type,
                                     HooxX86Reg * pc_reg)
{
  hx_x86_op * op;
  hx_uint8 * p;
  hx_boolean is_thunk;

  if (cpu_type != HOOX_CPU_IA32)
    return FALSE;

  op = &insn->detail->x86.operands[0];
  if (op->type != HX_OP_IMM)
    return FALSE;
  p = (hx_uint8 *) HX_SIZE_TO_POINTER (op->imm);

  is_thunk =
      ( p[0]         == 0x8b) &&
      ((p[1] & 0xc7) == 0x04) &&
      ( p[2]         == 0x24) &&
      ( p[3]         == 0xc3);
  if (!is_thunk)
    return FALSE;

  if (pc_reg != NULL)
    *pc_reg = (HooxX86Reg) ((p[1] & 0x38) >> 3);
  return TRUE;
}
