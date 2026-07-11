/*
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxarmreader.h"

static hx_uint hoox_rotate_right_32bit (hx_uint val, hx_uint rotation);

hx_pointer
hoox_arm_reader_try_get_relative_jump_target (hx_constpointer address)
{
  hx_pointer result = NULL;
  hx_insn * insn;
  hx_arm_op * op;

  insn = hoox_arm_reader_disassemble_instruction_at (address);
  if (insn == NULL)
    return NULL;

  op = &insn->detail->arm.operands[0];
  if (insn->id == HX_ARM_INS_B && op->type == HX_ARM_OP_IMM)
    result = HX_SIZE_TO_POINTER (op->imm);

  hx_insn_free (insn, 1);

  return result;
}

hx_pointer
hoox_arm_reader_try_get_indirect_jump_target (hx_constpointer address)
{
  hx_pointer result = NULL;
  hx_insn * insn;
  hx_arm_op * op0;
  hx_arm_op * op1;
  hx_arm_op * op2;
  hx_arm_op * op3;

  /*
   * First instruction: add r12, pc, 0
   */
  insn = hoox_arm_reader_disassemble_instruction_at (address);
  if (insn == NULL)
    return NULL;
  op0 = &insn->detail->arm.operands[0];
  op1 = &insn->detail->arm.operands[1];
  op2 = &insn->detail->arm.operands[2];
  op3 = &insn->detail->arm.operands[3];
  if (insn->id == HX_ARM_INS_ADD &&
      op0->type == HX_ARM_OP_REG && op0->reg == HX_ARM_REG_R12 &&
      op1->type == HX_ARM_OP_REG && op1->reg == HX_ARM_REG_PC &&
      op2->type == HX_ARM_OP_IMM)
  {
    result = (hx_pointer) address + 8 +
        hoox_rotate_right_32bit (op2->imm, op3->imm);
  }
  else
    goto beach;

  /*
   * Second instruction: add r12, r12, 96, 20
   */
  insn = hoox_arm_reader_disassemble_instruction_at (address + 4);
  op0 = &insn->detail->arm.operands[0];
  op1 = &insn->detail->arm.operands[1];
  op2 = &insn->detail->arm.operands[2];
  op3 = &insn->detail->arm.operands[3];
  if (insn->id == HX_ARM_INS_ADD &&
      op0->type == HX_ARM_OP_REG && op0->reg == HX_ARM_REG_R12 &&
      op1->type == HX_ARM_OP_REG && op1->reg == HX_ARM_REG_R12 &&
      op2->type == HX_ARM_OP_IMM)
  {
    if (insn->detail->arm.op_count == 4)
    {
      /*
       * I couldn't really find the documentation of WHY this
       * should be shifted by 12, but it seems to be how both
       * objdump and IDA decode.
       */
      result += (op2->imm << 12);
    }
    else
      result += op2->imm;
  }
  else
  {
    result = NULL;
    goto beach;
  }

  /*
   * Third instruction: ldr pc, [r12, x]
   */
  insn = hoox_arm_reader_disassemble_instruction_at (address + 8);
  op0 = &insn->detail->arm.operands[0];
  op1 = &insn->detail->arm.operands[1];
  if (insn->id == HX_ARM_INS_LDR &&
      op0->type == HX_ARM_OP_REG && op0->reg == HX_ARM_REG_PC &&
      op1->type == HX_ARM_OP_MEM && op1->mem.base == HX_ARM_REG_R12)
  {
    result = *((hx_pointer *) (result + op1->mem.disp));
  }
  else
  {
    result = NULL;
  }

beach:
  hx_insn_free (insn, 1);

  return result;
}

hx_insn *
hoox_arm_reader_disassemble_instruction_at (hx_constpointer address)
{
  hx_csh capstone;
  hx_insn * insn = NULL;

  hx_arch_register_arm ();
  hx_open (HX_ARCH_ARM, HX_MODE_ARM | HX_MODE_V8, &capstone);
  hx_option (capstone, HX_OPT_DETAIL, HX_OPT_ON);

  hx_disasm (capstone, address, 4, HX_POINTER_TO_SIZE (address), 1, &insn);

  hx_close (&capstone);

  return insn;
}

static hx_uint
hoox_rotate_right_32bit (hx_uint val,
                        hx_uint rotation)
{
  if (rotation == 0x0)
    return val;
  return ((val >> rotation) & (-1 << (32 - rotation))) |
      (val << (32 - rotation));
}
