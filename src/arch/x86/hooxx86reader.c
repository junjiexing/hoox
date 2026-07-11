/*
 * Copyright (C) 2009-2023 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxx86reader.h"

static hx_pointer try_get_relative_call_or_jump_target (hx_constpointer address,
    hx_uint call_or_jump);

hx_uint
hoox_x86_reader_insn_length (hx_uint8 * code)
{
  hx_uint result;
  hx_insn * insn;

  insn = hoox_x86_reader_disassemble_instruction_at (code);
  if (insn == NULL)
    return 0;
  result = insn->size;
  hx_insn_free (insn, 1);

  return result;
}

hx_boolean
hoox_x86_reader_insn_is_jcc (const hx_insn * insn)
{
  switch (insn->id)
  {
    case HX_INS_JA:
    case HX_INS_JAE:
    case HX_INS_JB:
    case HX_INS_JBE:
    case HX_INS_JE:
    case HX_INS_JG:
    case HX_INS_JGE:
    case HX_INS_JL:
    case HX_INS_JLE:
    case HX_INS_JNE:
    case HX_INS_JNO:
    case HX_INS_JNP:
    case HX_INS_JNS:
    case HX_INS_JO:
    case HX_INS_JP:
    case HX_INS_JS:
      return TRUE;

    default:
      break;
  }

  return FALSE;
}

hx_pointer
hoox_x86_reader_find_next_call_target (hx_constpointer address)
{
  hx_pointer result = NULL;
  hx_csh capstone;
  const uint8_t * code;
  size_t size;
  hx_insn * insn;
  uint64_t addr;

  hx_arch_register_x86 ();
  hx_open (HX_ARCH_X86, HOOX_CPU_MODE, &capstone);
  hx_option (capstone, HX_OPT_DETAIL, HX_OPT_ON);

  code = address;
  size = 1024;
  addr = HX_POINTER_TO_SIZE (address);

  insn = hx_insn_alloc (capstone);

  while (hx_disasm_iter (capstone, &code, &size, &addr, insn))
  {
    if (insn->id == HX_INS_CALL)
    {
      result = HX_SIZE_TO_POINTER (insn->detail->x86.operands[0].imm);
      break;
    }
  }

  hx_insn_free (insn, 1);

  hx_close (&capstone);

  return result;
}

hx_pointer
hoox_x86_reader_try_get_relative_call_target (hx_constpointer address)
{
  return try_get_relative_call_or_jump_target (address, HX_INS_CALL);
}

hx_pointer
hoox_x86_reader_try_get_relative_jump_target (hx_constpointer address)
{
  return try_get_relative_call_or_jump_target (address, HX_INS_JMP);
}

hx_pointer
hoox_x86_reader_try_get_indirect_jump_target (hx_constpointer address)
{
  hx_pointer result = NULL;
  hx_insn * insn;
  hx_x86_op * op;

  insn = hoox_x86_reader_disassemble_instruction_at (address);
  if (insn == NULL)
    return NULL;

  op = &insn->detail->x86.operands[0];
  if (insn->id == HX_INS_JMP && op->type == HX_OP_MEM)
  {
    if (op->mem.base == HX_REG_RIP && op->mem.index == HX_REG_INVALID)
    {
      result = *((hx_pointer *) ((hx_uint8 *) address + insn->size + op->mem.disp));
    }
    else if (op->mem.base == HX_REG_INVALID &&
        op->mem.index == HX_REG_INVALID)
    {
      result = *((hx_pointer *) HX_SIZE_TO_POINTER (op->mem.disp));
    }
  }

  hx_insn_free (insn, 1);

  return result;
}

static hx_pointer
try_get_relative_call_or_jump_target (hx_constpointer address,
                                      hx_uint call_or_jump)
{
  hx_pointer result = NULL;
  hx_insn * insn;
  hx_x86_op * op;

  insn = hoox_x86_reader_disassemble_instruction_at (address);
  if (insn == NULL)
    return NULL;

  op = &insn->detail->x86.operands[0];
  if (insn->id == call_or_jump && op->type == HX_OP_IMM)
    result = HX_SIZE_TO_POINTER (op->imm);

  hx_insn_free (insn, 1);

  return result;
}

hx_insn *
hoox_x86_reader_disassemble_instruction_at (hx_constpointer address)
{
  hx_csh capstone;
  hx_insn * insn = NULL;

  hx_arch_register_x86 ();
  hx_open (HX_ARCH_X86, HOOX_CPU_MODE, &capstone);
  hx_option (capstone, HX_OPT_DETAIL, HX_OPT_ON);

  hx_disasm (capstone, address, 16, HX_POINTER_TO_SIZE (address), 1, &insn);

  hx_close (&capstone);

  return insn;
}
