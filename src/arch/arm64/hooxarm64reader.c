/*
 * Copyright (C) 2015-2025 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxarm64reader.h"

#include <hx_disasm.h>

static hx_boolean hoox_is_bl_imm (hx_uint32 insn);

hx_pointer
hoox_arm64_reader_find_next_bl_target (hx_constpointer address)
{
  const hx_uint32 * cursor = address;

  do
  {
    hx_uint32 insn = *cursor;

    if (hoox_is_bl_imm (insn))
    {
      union
      {
        hx_int32 i;
        hx_uint32 u;
      } distance;

      distance.u = insn & HOOX_INT26_MASK;
      if ((distance.u & (1 << (26 - 1))) != 0)
        distance.u |= 0xfc000000;

      return (hx_pointer) (cursor + distance.i);
    }

    cursor++;
  }
  while (TRUE);
}

hx_pointer
hoox_arm64_reader_try_get_relative_jump_target (hx_constpointer address)
{
  hx_pointer result = NULL;
  hx_csh capstone;
  hx_insn * insn;
  const uint8_t * code;
  size_t size;
  uint64_t pc;
  const hx_arm64_op * ops;

  hx_arch_register_arm64 ();
  hx_open (HX_ARCH_ARM64, HOOX_DEFAULT_HX_ENDIAN, &capstone);
  hx_option (capstone, HX_OPT_DETAIL, HX_OPT_ON);

  insn = hx_insn_alloc (capstone);

  code = address;
  size = 16;
  pc = HX_POINTER_TO_SIZE (address);

#define HOOX_DISASM_NEXT() \
    if (!hx_disasm_iter (capstone, &code, &size, &pc, insn)) \
      goto beach; \
    ops = insn->detail->arm64.operands
#define HOOX_CHECK_ID(i) \
    if (insn->id != HX_PASTE (HX_ARM64_INS_, i)) \
      goto beach
#define HOOX_CHECK_OP_TYPE(n, t) \
    if (ops[n].type != HX_PASTE (HX_ARM64_OP_, t)) \
      goto beach
#define HOOX_CHECK_OP_REG(n, r) \
    if (ops[n].reg != HX_PASTE (HX_ARM64_REG_, r)) \
      goto beach
#define HOOX_CHECK_OP_MEM(n, b, i, d) \
    if (ops[n].mem.base != HX_PASTE (HX_ARM64_REG_, b)) \
      goto beach; \
    if (ops[n].mem.index != HX_PASTE (HX_ARM64_REG_, i)) \
      goto beach; \
    if (ops[n].mem.disp != d) \
      goto beach

  HOOX_DISASM_NEXT ();

  switch (insn->id)
  {
    case HX_ARM64_INS_B:
      result = HX_SIZE_TO_POINTER (ops[0].imm);
      break;
#ifdef HAVE_DARWIN
    case HX_ARM64_INS_ADRP:
    {
      HooxAddress target;

      HOOX_CHECK_OP_REG (0, X17);
      target = ops[1].imm;

      HOOX_DISASM_NEXT ();
      HOOX_CHECK_ID (ADD);
      HOOX_CHECK_OP_REG (0, X17);
      HOOX_CHECK_OP_REG (1, X17);
      HOOX_CHECK_OP_TYPE (2, IMM);
      target += ops[2].imm;

      HOOX_DISASM_NEXT ();
      HOOX_CHECK_ID (LDR);
      HOOX_CHECK_OP_REG (0, X16);
      HOOX_CHECK_OP_TYPE (1, MEM);
      HOOX_CHECK_OP_MEM (1, X17, INVALID, 0);

      HOOX_DISASM_NEXT ();
      HOOX_CHECK_ID (BRAA);
      HOOX_CHECK_OP_REG (0, X16);
      HOOX_CHECK_OP_REG (1, X17);

      result = *((hx_pointer *) HX_SIZE_TO_POINTER (target));

      break;
    }
#endif
    default:
      break;
  }

beach:
  hx_insn_free (insn, 1);

  hx_close (&capstone);

  return result;
}

hx_insn *
hoox_arm64_reader_disassemble_instruction_at (hx_constpointer address)
{
  hx_csh capstone;
  hx_insn * insn = NULL;

  hx_arch_register_arm64 ();
  hx_open (HX_ARCH_ARM64, HOOX_DEFAULT_HX_ENDIAN, &capstone);
  hx_option (capstone, HX_OPT_DETAIL, HX_OPT_ON);

  hx_disasm (capstone, address, 16, HX_POINTER_TO_SIZE (address), 1, &insn);

  hx_close (&capstone);

  return insn;
}

static hx_boolean
hoox_is_bl_imm (hx_uint32 insn)
{
  return (insn & ~HOOX_INT26_MASK) == 0x94000000;
}
