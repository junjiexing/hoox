/*
 * Copyright (C) 2015-2023 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxthumbreader.h"

#include <hx_disasm.h>

hx_pointer
hoox_thumb_reader_try_get_relative_jump_target (hx_constpointer address)
{
  hx_pointer result = NULL;
  hx_insn * insn;
  hx_arm_op * op;

  insn = hoox_thumb_reader_disassemble_instruction_at (address);
  if (insn == NULL)
    return NULL;

  op = &insn->detail->arm.operands[0];
  if (insn->id == HX_ARM_INS_B && op->type == HX_ARM_OP_IMM)
    result = HX_SIZE_TO_POINTER (op->imm | 1);
  else if (insn->id == HX_ARM_INS_BX && op->type == HX_ARM_OP_IMM)
    result = HX_SIZE_TO_POINTER (op->imm);

  hx_insn_free (insn, 1);

  return result;
}

hx_insn *
hoox_thumb_reader_disassemble_instruction_at (hx_constpointer address)
{
  hx_constpointer code = HX_SIZE_TO_POINTER (HX_POINTER_TO_SIZE (address) & ~1);
  hx_csh capstone;
  hx_insn * insn = NULL;

  hx_arch_register_arm ();
  hx_open (HX_ARCH_ARM, HX_MODE_THUMB | HX_MODE_V8, &capstone);
  hx_option (capstone, HX_OPT_DETAIL, HX_OPT_ON);

  hx_disasm (capstone, code, 16, HX_POINTER_TO_SIZE (code), 1, &insn);

  hx_close (&capstone);

  return insn;
}
