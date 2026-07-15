/*
 * hoox — compact 32-bit ARM (A32 + Thumb/T32) decoder backing the
 * capstone-compatible shim.
 *
 * hoox does not depend on capstone. This decoder provides just enough of the
 * capstone arm surface that the extracted frida-gum relocator/reader/
 * interceptor consume: it precisely decodes the PC-relative and branch/control
 * instructions those paths rewrite or key off (B/BL/BLX/BX, CBZ/CBNZ, literal
 * LDR/VLDR, ADR, ADD rd,pc, MOV, POP/PUSH/LDM, IT, TBB/TBH, SVC), plus the
 * handful of data-processing forms a couple of paths inspect. Every other
 * instruction is returned with a valid (id == HX_ARM_INS_INVALID) result and
 * op_count == 0 so the relocator copies it verbatim; the ARM/Thumb relocators
 * use fixed scratch registers, so no conservative register reporting is needed.
 *
 * Both A32 and T32 are decoded from a little-endian code stream, so decoding is
 * arch-neutral C: it produces identical results on any host, which is what lets
 * the differential test validate it without ARM hardware.
 *
 * All immediate branch targets are reported as absolute addresses (capstone
 * convention): A32 pc = insn addr + 8, Thumb pc = insn addr + 4.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include <hx_disasm.h>

#include <stdlib.h>
#include <string.h>

/* ---- helpers ------------------------------------------------------------ */

static int32_t
sign_extend (uint32_t v, unsigned bits)
{
  uint32_t m = (uint32_t) 1 << (bits - 1);
  return (int32_t) ((v ^ m) - m);
}

/* Map a 4-bit GP field to an ARM register enum value. */
static hx_arm_reg
r_reg (unsigned n)
{
  if (n <= 12)
    return (hx_arm_reg) (HX_ARM_REG_R0 + n);
  if (n == 13)
    return HX_ARM_REG_SP;
  if (n == 14)
    return HX_ARM_REG_LR;
  return HX_ARM_REG_PC;   /* 15 */
}

static uint32_t
ror32 (uint32_t v, unsigned amount)
{
  amount &= 31;
  if (amount == 0)
    return v;
  return (v >> amount) | (v << (32 - amount));
}

static hx_arm_shifter
shifter_from_bits (uint32_t bits)
{
  switch (bits & 0x3)
  {
    case 0: return HX_ARM_SFT_LSL;
    case 1: return HX_ARM_SFT_LSR;
    case 2: return HX_ARM_SFT_ASR;
    default: return HX_ARM_SFT_ROR;
  }
}

static void
set_reg_op (hx_arm_op * op, hx_arm_reg reg)
{
  op->type = HX_ARM_OP_REG;
  op->reg = reg;
}

static void
set_imm_op (hx_arm_op * op, int64_t imm)
{
  op->type = HX_ARM_OP_IMM;
  op->imm = imm;
}

static void
init_insn (const uint8_t * code, uint64_t address, uint16_t size,
    hx_insn * insn)
{
  hx_arm * d = &insn->detail->arm;

  insn->id = HX_ARM_INS_INVALID;
  insn->address = address;
  insn->size = size;
  memcpy (insn->bytes, code, size);
  insn->mnemonic[0] = '\0';
  insn->op_str[0] = '\0';

  d->cc = HX_ARM_CC_INVALID;
  d->writeback = false;
  d->op_count = 0;
}

static void
mark_unsupported_pc_relative (hx_insn * insn)
{
  insn->id = HX_ARM_INS_UNSUPPORTED_PC_RELATIVE;
  strcpy (insn->mnemonic, "unsupported-pcrel");
}

/* ---- A32 decode --------------------------------------------------------- */

static void
hx_decode_arm (const uint8_t * code, uint64_t address, hx_insn * insn)
{
  uint32_t w = (uint32_t) code[0] | ((uint32_t) code[1] << 8) |
      ((uint32_t) code[2] << 16) | ((uint32_t) code[3] << 24);
  hx_arm * d = &insn->detail->arm;
  uint32_t cond = (w >> 28) & 0xf;

  init_insn (code, address, 4, insn);

  /* B / BL / BLX(imm): bits[27:25] == 0b101 */
  if (((w >> 25) & 0x7) == 0x5)
  {
    uint32_t imm24 = w & 0x00ffffff;
    int32_t off = (int32_t) ((uint32_t) sign_extend (imm24, 24) << 2);

    if (cond == 0xf)
    {
      /* BLX (immediate) */
      uint32_t h = (w >> 24) & 1;
      int64_t target = (int64_t) (address + 8) + off + (int64_t) (h << 1);
      insn->id = HX_ARM_INS_BLX;
      strcpy (insn->mnemonic, "blx");
      set_imm_op (&d->operands[0], target);
      d->cc = HX_ARM_CC_INVALID;
      d->op_count = 1;
      return;
    }

    if ((w & 0x01000000) != 0)
    {
      insn->id = HX_ARM_INS_BL;
      strcpy (insn->mnemonic, "bl");
    }
    else
    {
      insn->id = HX_ARM_INS_B;
      strcpy (insn->mnemonic, "b");
    }
    set_imm_op (&d->operands[0], (int64_t) (address + 8) + off);
    d->cc = (hx_arm_cc) (cond + 1);
    d->op_count = 1;
    return;
  }

  /* BX reg */
  if ((w & 0x0ffffff0) == 0x012fff10)
  {
    insn->id = HX_ARM_INS_BX;
    strcpy (insn->mnemonic, "bx");
    set_reg_op (&d->operands[0], r_reg (w & 0xf));
    d->cc = (cond <= 14) ? (hx_arm_cc) (cond + 1) : HX_ARM_CC_INVALID;
    d->op_count = 1;
    return;
  }

  /* BLX reg */
  if ((w & 0x0ffffff0) == 0x012fff30)
  {
    insn->id = HX_ARM_INS_BLX;
    strcpy (insn->mnemonic, "blx");
    set_reg_op (&d->operands[0], r_reg (w & 0xf));
    d->cc = (cond <= 14) ? (hx_arm_cc) (cond + 1) : HX_ARM_CC_INVALID;
    d->op_count = 1;
    return;
  }

  /* SVC */
  if ((w & 0x0f000000) == 0x0f000000)
  {
    insn->id = HX_ARM_INS_SVC;
    strcpy (insn->mnemonic, "svc");
    d->cc = (cond <= 14) ? (hx_arm_cc) (cond + 1) : HX_ARM_CC_INVALID;
    d->op_count = 0;
    return;
  }

  /* LDR (word, load): bits[27:26]==0b01, B==0, L==1 */
  if (((w >> 26) & 0x3) == 0x1 && ((w >> 22) & 1) == 0 && ((w >> 20) & 1) == 1)
  {
    uint32_t rt = (w >> 12) & 0xf;
    uint32_t rn = (w >> 16) & 0xf;
    uint32_t i_bit = (w >> 25) & 1;
    uint32_t p_bit = (w >> 24) & 1;
    uint32_t u_bit = (w >> 23) & 1;
    uint32_t wb = (w >> 21) & 1;
    uint32_t imm12 = w & 0xfff;
    uint32_t rm = w & 0xf;

    insn->id = HX_ARM_INS_LDR;
    strcpy (insn->mnemonic, "ldr");
    set_reg_op (&d->operands[0], r_reg (rt));
    d->operands[1].type = HX_ARM_OP_MEM;
    d->operands[1].mem.base = r_reg (rn);
    if (i_bit == 0)
    {
      d->operands[1].mem.index = HX_ARM_REG_INVALID;
      d->operands[1].mem.disp = (int32_t) imm12;
      d->operands[1].shift.type = HX_ARM_SFT_INVALID;
      d->operands[1].shift.value = 0;
    }
    else
    {
      d->operands[1].mem.index = r_reg (rm);
      d->operands[1].mem.disp = 0;
      d->operands[1].shift.type = shifter_from_bits ((w >> 5) & 0x3);
      d->operands[1].shift.value = (w >> 7) & 0x1f;
    }
    d->operands[1].subtracted = (u_bit == 0);
    d->writeback = (p_bit == 0) || (wb == 1);
    d->cc = (cond <= 14) ? (hx_arm_cc) (cond + 1) : HX_ARM_CC_INVALID;
    d->op_count = 2;
    return;
  }

  /* Other single-data-transfer forms using PC as the base (LDRB/STR/etc.)
   * are PC-relative too, but the relocator cannot rewrite them yet. */
  if (((w >> 26) & 0x3) == 0x1 && ((w >> 16) & 0xf) == 0xf)
  {
    mark_unsupported_pc_relative (insn);
    return;
  }

  /* Halfword, signed-byte/halfword, and dual-register transfers occupy the
   * extra-load/store space. Their Rn field is still the memory base. */
  if ((w & 0x0e000090) == 0x00000090 && ((w >> 16) & 0xf) == 0xf)
  {
    mark_unsupported_pc_relative (insn);
    return;
  }

  /* Data-processing: bits[27:26] == 0b00 */
  if (((w >> 26) & 0x3) == 0x0)
  {
    uint32_t opcode = (w >> 21) & 0xf;
    uint32_t i_bit = (w >> 25) & 1;
    uint32_t rn = (w >> 16) & 0xf;
    uint32_t rd = (w >> 12) & 0xf;

    if (opcode == 0x4 || opcode == 0x2 || opcode == 0xd)
    {
      hx_arm_op right;
      memset (&right, 0, sizeof (right));

      if (i_bit == 1)
      {
        uint32_t imm8 = w & 0xff;
        uint32_t rot = (w >> 8) & 0xf;
        set_imm_op (&right, (int64_t) (uint32_t) ror32 (imm8, 2 * rot));
        right.shift.type = HX_ARM_SFT_INVALID;
        right.shift.value = 0;
      }
      else
      {
        set_reg_op (&right, r_reg (w & 0xf));
        right.shift.type = shifter_from_bits ((w >> 5) & 0x3);
        right.shift.value = (w >> 7) & 0x1f;
      }

      d->cc = (cond <= 14) ? (hx_arm_cc) (cond + 1) : HX_ARM_CC_INVALID;

      if (opcode == 0xd)
      {
        insn->id = HX_ARM_INS_MOV;
        strcpy (insn->mnemonic, "mov");
        set_reg_op (&d->operands[0], r_reg (rd));
        d->operands[1] = right;
        d->op_count = 2;
      }
      else
      {
        insn->id = (opcode == 0x4) ? HX_ARM_INS_ADD : HX_ARM_INS_SUB;
        strcpy (insn->mnemonic, (opcode == 0x4) ? "add" : "sub");
        set_reg_op (&d->operands[0], r_reg (rd));
        set_reg_op (&d->operands[1], r_reg (rn));
        d->operands[2] = right;
        d->op_count = 3;
      }
      return;
    }
    /* Unknown data-processing instructions that read PC cannot be copied to a
     * trampoline verbatim. Register-shifted forms may read both Rm and Rs. */
    if ((opcode != 0xd && opcode != 0xf && rn == 0xf) ||
        (i_bit == 0 && (w & 0xf) == 0xf) ||
        (i_bit == 0 && (w & 0x10) != 0 && ((w >> 8) & 0xf) == 0xf))
    {
      mark_unsupported_pc_relative (insn);
      return;
    }

    /* other data-processing opcodes: verbatim */
    return;
  }

  /* Coprocessor loads/stores include VFP literal loads such as
   * `vldr s0, [pc]`. Keep them out of the relocatable prefix until an A32
   * rewrite is available. */
  if (((w >> 25) & 0x7) == 0x6 && ((w >> 16) & 0xf) == 0xf)
  {
    mark_unsupported_pc_relative (insn);
    return;
  }

  /* Block data transfer: bits[27:25] == 0b100 */
  if (((w >> 25) & 0x7) == 0x4)
  {
    uint32_t l_bit = (w >> 20) & 1;
    uint32_t p_bit = (w >> 24) & 1;
    uint32_t u_bit = (w >> 23) & 1;
    uint32_t wb = (w >> 21) & 1;
    uint32_t rn = (w >> 16) & 0xf;
    uint32_t list = w & 0xffff;
    unsigned i;

    d->cc = (cond <= 14) ? (hx_arm_cc) (cond + 1) : HX_ARM_CC_INVALID;

    if (l_bit == 1 && rn == 13 && p_bit == 0 && u_bit == 1 && wb == 1)
    {
      insn->id = HX_ARM_INS_POP;
      strcpy (insn->mnemonic, "pop");
      d->op_count = 0;
      for (i = 0; i < 16; i++)
      {
        if ((list >> i) & 1)
          set_reg_op (&d->operands[d->op_count++], r_reg (i));
      }
      return;
    }
    if (l_bit == 0 && rn == 13 && p_bit == 1 && u_bit == 0 && wb == 1)
    {
      insn->id = HX_ARM_INS_PUSH;
      strcpy (insn->mnemonic, "push");
      d->op_count = 0;
      return;
    }
    if (l_bit == 1)
    {
      insn->id = HX_ARM_INS_LDM;
      strcpy (insn->mnemonic, "ldm");
      d->op_count = 0;
      set_reg_op (&d->operands[d->op_count++], r_reg (rn));
      for (i = 0; i < 16; i++)
      {
        if ((list >> i) & 1)
          set_reg_op (&d->operands[d->op_count++], r_reg (i));
      }
      return;
    }
    /* store multiple (non-push): verbatim */
    return;
  }

  /* Anything else: leave id == INVALID, verbatim. */
}

/* ---- Thumb decode ------------------------------------------------------- */

static void
hx_decode_thumb (const uint8_t * code, uint64_t address, hx_insn * insn)
{
  uint32_t h0 = (uint32_t) code[0] | ((uint32_t) code[1] << 8);
  bool is32 = ((h0 >> 11) & 0x1f) >= 0x1d;   /* 0x1d..0x1f */
  hx_arm * d = &insn->detail->arm;
  uint64_t pc = address + 4;

  if (is32)
  {
    uint32_t h1 = (uint32_t) code[2] | ((uint32_t) code[3] << 8);

    init_insn (code, address, 4, insn);

    /*
     * Branch discriminator: the conditional/unconditional B forms (T3/T4)
     * have h1 bit14 == 0, while BL/BLX have bit14 == 1. B T4 shares bit15
     * with BL, so the B forms (which pin bit14 via the 0xd000 mask) must be
     * tested BEFORE the looser BL/BLX check.
     */

    /* B (T3 conditional, 32-bit) */
    if ((h0 & 0xf800) == 0xf000 && (h1 & 0xd000) == 0x8000)
    {
      uint32_t cond = (h0 >> 6) & 0xf;
      if (cond < 0xe)
      {
        uint32_t s = (h0 >> 10) & 1;
        uint32_t j1 = (h1 >> 13) & 1;
        uint32_t j2 = (h1 >> 11) & 1;
        uint32_t imm6 = h0 & 0x3f;
        uint32_t imm11 = h1 & 0x7ff;
        uint32_t raw = (s << 20) | (j2 << 19) | (j1 << 18) |
            (imm6 << 12) | (imm11 << 1);
        int32_t off = sign_extend (raw, 21);
        insn->id = HX_ARM_INS_B;
        strcpy (insn->mnemonic, "b");
        d->cc = (hx_arm_cc) (cond + 1);
        set_imm_op (&d->operands[0], (int64_t) pc + off);
        d->op_count = 1;
        return;
      }
      /*
       * cond == 0b111x is NOT a conditional branch: this slot is the Thumb-2
       * hint/barrier/system space (NOP.W, YIELD/WFE/WFI/SEV, DMB/DSB/ISB,
       * CLREX, MSR/MRS, ...). None of it is PC-relative, so return here and let
       * it be copied verbatim rather than falling through to the looser BL/BLX
       * matcher below, which (bit14 untested) would misread it as a BLX to a
       * garbage target and corrupt the trampoline.
       */
      return;
    }

    /* B (T4 unconditional, 32-bit) */
    if ((h0 & 0xf800) == 0xf000 && (h1 & 0xd000) == 0x9000)
    {
      uint32_t s = (h0 >> 10) & 1;
      uint32_t imm10 = h0 & 0x3ff;
      uint32_t j1 = (h1 >> 13) & 1;
      uint32_t j2 = (h1 >> 11) & 1;
      uint32_t imm11 = h1 & 0x7ff;
      uint32_t i1 = (~(j1 ^ s)) & 1;
      uint32_t i2 = (~(j2 ^ s)) & 1;
      uint32_t raw = (s << 24) | (i1 << 23) | (i2 << 22) |
          (imm10 << 12) | (imm11 << 1);
      int32_t off = sign_extend (raw, 25);
      insn->id = HX_ARM_INS_B;
      strcpy (insn->mnemonic, "b");
      d->cc = HX_ARM_CC_INVALID;
      set_imm_op (&d->operands[0], (int64_t) pc + off);
      d->op_count = 1;
      return;
    }

    /* BL / BLX (T1, 32-bit) — bit14 set (checked after the B forms) */
    if ((h0 & 0xf800) == 0xf000 && (h1 & 0x8000) == 0x8000)
    {
      uint32_t s = (h0 >> 10) & 1;
      uint32_t imm10 = h0 & 0x3ff;
      uint32_t j1 = (h1 >> 13) & 1;
      uint32_t j2 = (h1 >> 11) & 1;
      uint32_t imm11 = h1 & 0x7ff;
      uint32_t i1 = (~(j1 ^ s)) & 1;
      uint32_t i2 = (~(j2 ^ s)) & 1;
      uint32_t raw = (s << 24) | (i1 << 23) | (i2 << 22) |
          (imm10 << 12) | (imm11 << 1);
      int32_t off = sign_extend (raw, 25);

      if (h1 & 0x1000)
      {
        int64_t target = (int64_t) pc + off;
        insn->id = HX_ARM_INS_BL;
        strcpy (insn->mnemonic, "bl");
        set_imm_op (&d->operands[0], target);
      }
      else
      {
        int64_t target = ((int64_t) pc + off) & ~(int64_t) 3;
        insn->id = HX_ARM_INS_BLX;
        strcpy (insn->mnemonic, "blx");
        set_imm_op (&d->operands[0], target);
      }
      d->op_count = 1;
      return;
    }

    /* LDR literal (T2, 32-bit) */
    if ((h0 & 0xff7f) == 0xf85f)
    {
      uint32_t rt = (h1 >> 12) & 0xf;
      uint32_t u_bit = (h0 >> 7) & 1;
      uint32_t imm12 = h1 & 0xfff;
      insn->id = HX_ARM_INS_LDR;
      strcpy (insn->mnemonic, "ldr");
      set_reg_op (&d->operands[0], r_reg (rt));
      d->operands[1].type = HX_ARM_OP_MEM;
      d->operands[1].mem.base = HX_ARM_REG_PC;
      d->operands[1].mem.index = HX_ARM_REG_INVALID;
      d->operands[1].mem.disp = u_bit ? (int32_t) imm12 : -(int32_t) imm12;
      d->op_count = 2;
      return;
    }

    /* VLDR literal (32-bit) */
    if ((h0 & 0xff3f) == 0xed1f && (((h1 >> 8) & 0xf) == 0xa ||
        ((h1 >> 8) & 0xf) == 0xb))
    {
      uint32_t u_bit = (h0 >> 7) & 1;
      uint32_t imm8 = h1 & 0xff;
      bool single = ((h1 >> 8) & 0xf) == 0xa;
      uint32_t d_bit = (h0 >> 6) & 1;
      uint32_t vd_field = (h1 >> 12) & 0xf;
      uint32_t vd;
      int32_t disp = (u_bit ? 1 : -1) * (int32_t) (imm8 << 2);

      insn->id = HX_ARM_INS_VLDR;
      strcpy (insn->mnemonic, "vldr");
      if (single)
      {
        vd = (vd_field << 1) | d_bit;
        set_reg_op (&d->operands[0], (hx_arm_reg) (HX_ARM_REG_S0 + vd));
      }
      else
      {
        vd = (d_bit << 4) | vd_field;
        set_reg_op (&d->operands[0], (hx_arm_reg) (HX_ARM_REG_D0 + vd));
      }
      d->operands[1].type = HX_ARM_OP_MEM;
      d->operands[1].mem.base = HX_ARM_REG_PC;
      d->operands[1].mem.index = HX_ARM_REG_INVALID;
      d->operands[1].mem.disp = disp;
      d->op_count = 2;
      return;
    }

    /* TBB / TBH */
    if ((h0 & 0xfff0) == 0xe8d0 && (h1 & 0xffe0) == 0xf000)
    {
      uint32_t hbit = (h1 >> 4) & 1;
      insn->id = hbit ? HX_ARM_INS_TBH : HX_ARM_INS_TBB;
      strcpy (insn->mnemonic, hbit ? "tbh" : "tbb");
      d->op_count = 0;
      return;
    }

    /* POP (T2, 32-bit): LDM SP! */
    if (h0 == 0xe8bd)
    {
      uint32_t list = h1;
      unsigned i;
      insn->id = HX_ARM_INS_POP;
      strcpy (insn->mnemonic, "pop");
      d->op_count = 0;
      for (i = 0; i < 16; i++)
      {
        if ((list >> i) & 1)
          set_reg_op (&d->operands[d->op_count++], r_reg (i));
      }
      return;
    }

    /* PUSH (T2, 32-bit): STMDB SP! */
    if (h0 == 0xe92d)
    {
      insn->id = HX_ARM_INS_PUSH;
      strcpy (insn->mnemonic, "push");
      d->op_count = 0;
      return;
    }

    /* LDM (T2, 32-bit) */
    if ((h0 & 0xffd0) == 0xe890 && ((h0 >> 4) & 1) == 1)
    {
      uint32_t rn = h0 & 0xf;
      uint32_t list = h1;
      unsigned i;
      insn->id = HX_ARM_INS_LDM;
      strcpy (insn->mnemonic, "ldm");
      d->op_count = 0;
      set_reg_op (&d->operands[d->op_count++], r_reg (rn));
      for (i = 0; i < 16; i++)
      {
        if ((list >> i) & 1)
          set_reg_op (&d->operands[d->op_count++], r_reg (i));
      }
      return;
    }

    /* Remaining literal-load families (LDRB/LDRSB/LDRH/LDRSH/LDRD and
     * coprocessor forms), plus ADR.W, all derive an address from PC. */
    if ((((h0 & 0xfe00) == 0xf800) && (h0 & 0xf) == 0xf) ||
        (h0 & 0xfb0f) == 0xf20f ||
        (h0 & 0xff7f) == 0xe95f ||
        (h0 & 0xec0f) == 0xec0f)
    {
      mark_unsupported_pc_relative (insn);
      return;
    }

    /* Anything else 32-bit: verbatim. */
    return;
  }

  /* 16-bit forms */
  init_insn (code, address, 2, insn);

  /* CBZ / CBNZ */
  if ((h0 & 0xf500) == 0xb100)
  {
    uint32_t op = (h0 >> 11) & 1;
    uint32_t rn = h0 & 0x7;
    /* imm = i:imm5:0  where i == bit9, imm5 == bits[7:3] */
    uint32_t imm = (((h0 >> 3) & 0x1f) << 1) | (((h0 >> 9) & 1) << 6);
    insn->id = op ? HX_ARM_INS_CBNZ : HX_ARM_INS_CBZ;
    strcpy (insn->mnemonic, op ? "cbnz" : "cbz");
    set_reg_op (&d->operands[0], r_reg (rn));
    set_imm_op (&d->operands[1], (int64_t) pc + (int32_t) imm);
    d->op_count = 2;
    return;
  }

  /* B (T2 unconditional) */
  if ((h0 & 0xf800) == 0xe000)
  {
    uint32_t imm11 = h0 & 0x7ff;
    int32_t off = (int32_t) ((uint32_t) sign_extend (imm11, 11) << 1);
    insn->id = HX_ARM_INS_B;
    strcpy (insn->mnemonic, "b");
    d->cc = HX_ARM_CC_INVALID;
    set_imm_op (&d->operands[0], (int64_t) pc + off);
    d->op_count = 1;
    return;
  }

  /* B (T1 conditional) / SVC / UDF */
  if ((h0 & 0xf000) == 0xd000)
  {
    uint32_t cond = (h0 >> 8) & 0xf;
    if (cond == 0xf)
    {
      insn->id = HX_ARM_INS_SVC;
      strcpy (insn->mnemonic, "svc");
      d->op_count = 0;
      return;
    }
    if (cond == 0xe)
    {
      /* UDF: verbatim */
      return;
    }
    {
      uint32_t imm8 = h0 & 0xff;
      int32_t off = (int32_t) ((uint32_t) sign_extend (imm8, 8) << 1);
      insn->id = HX_ARM_INS_B;
      strcpy (insn->mnemonic, "b");
      d->cc = (hx_arm_cc) (cond + 1);
      set_imm_op (&d->operands[0], (int64_t) pc + off);
      d->op_count = 1;
      return;
    }
  }

  /* BX / BLX reg */
  if ((h0 & 0xff87) == 0x4700)
  {
    uint32_t l_bit = (h0 >> 7) & 1;
    uint32_t rm = (h0 >> 3) & 0xf;
    insn->id = l_bit ? HX_ARM_INS_BLX : HX_ARM_INS_BX;
    strcpy (insn->mnemonic, l_bit ? "blx" : "bx");
    set_reg_op (&d->operands[0], r_reg (rm));
    d->op_count = 1;
    return;
  }

  /* LDR literal (T1) */
  if ((h0 & 0xf800) == 0x4800)
  {
    uint32_t rt = (h0 >> 8) & 0x7;
    uint32_t imm8 = h0 & 0xff;
    insn->id = HX_ARM_INS_LDR;
    strcpy (insn->mnemonic, "ldr");
    set_reg_op (&d->operands[0], r_reg (rt));
    d->operands[1].type = HX_ARM_OP_MEM;
    d->operands[1].mem.base = HX_ARM_REG_PC;
    d->operands[1].mem.index = HX_ARM_REG_INVALID;
    d->operands[1].mem.disp = (int32_t) (imm8 << 2);
    d->op_count = 2;
    return;
  }

  /* ADR (T1) */
  if ((h0 & 0xf800) == 0xa000)
  {
    uint32_t rd = (h0 >> 8) & 0x7;
    uint32_t imm8 = h0 & 0xff;
    insn->id = HX_ARM_INS_ADR;
    strcpy (insn->mnemonic, "adr");
    set_reg_op (&d->operands[0], r_reg (rd));
    set_imm_op (&d->operands[1], (int64_t) (imm8 << 2));
    d->op_count = 2;
    return;
  }

  /* ADD Rd, PC (T1 hi-reg) — must be checked before generic MOV-hi */
  if ((h0 & 0xff78) == 0x4478)
  {
    uint32_t dn = (h0 >> 7) & 1;
    uint32_t rdn = (dn << 3) | (h0 & 0x7);
    insn->id = HX_ARM_INS_ADD;
    strcpy (insn->mnemonic, "add");
    set_reg_op (&d->operands[0], r_reg (rdn));
    set_reg_op (&d->operands[1], HX_ARM_REG_PC);
    d->op_count = 2;
    return;
  }

  /* MOV hi (T1) */
  if ((h0 & 0xff00) == 0x4600)
  {
    uint32_t dbit = (h0 >> 7) & 1;
    uint32_t rd = (dbit << 3) | (h0 & 0x7);
    uint32_t rm = (h0 >> 3) & 0xf;
    insn->id = HX_ARM_INS_MOV;
    strcpy (insn->mnemonic, "mov");
    set_reg_op (&d->operands[0], r_reg (rd));
    set_reg_op (&d->operands[1], r_reg (rm));
    d->op_count = 2;
    return;
  }

  /* POP (T1) */
  if ((h0 & 0xfe00) == 0xbc00)
  {
    uint32_t p_bit = (h0 >> 8) & 1;
    uint32_t list = h0 & 0xff;
    unsigned i;
    insn->id = HX_ARM_INS_POP;
    strcpy (insn->mnemonic, "pop");
    d->op_count = 0;
    for (i = 0; i < 8; i++)
    {
      if ((list >> i) & 1)
        set_reg_op (&d->operands[d->op_count++], r_reg (i));
    }
    if (p_bit)
      set_reg_op (&d->operands[d->op_count++], HX_ARM_REG_PC);
    return;
  }

  /* PUSH (T1) */
  if ((h0 & 0xfe00) == 0xb400)
  {
    insn->id = HX_ARM_INS_PUSH;
    strcpy (insn->mnemonic, "push");
    d->op_count = 0;
    return;
  }

  /* IT */
  if ((h0 & 0xff00) == 0xbf00 && (h0 & 0x000f) != 0)
  {
    uint32_t firstcond = (h0 >> 4) & 0xf;
    insn->id = HX_ARM_INS_IT;
    strcpy (insn->mnemonic, "it");
    d->cc = (hx_arm_cc) (firstcond + 1);
    d->op_count = 0;
    return;
  }

  /* Anything else 16-bit: verbatim. */
}

/* ---- capstone shim API -------------------------------------------------- */

typedef struct
{
  bool detail;
  bool thumb;
} HxArmHandle;

hx_err
hx_open (hx_arch arch, hx_mode mode, hx_csh * handle)
{
  HxArmHandle * h;

  if (arch != HX_ARCH_ARM)
    return HX_ERR_ARCH;

  h = calloc (1, sizeof (HxArmHandle));
  if (h == NULL)
    return HX_ERR_MEM;

  h->detail = false;
  h->thumb = (mode & HX_MODE_THUMB) != 0;
  *handle = (hx_csh) (size_t) h;

  return HX_ERR_OK;
}

hx_err
hx_close (hx_csh * handle)
{
  if (handle == NULL || *handle == 0)
    return HX_ERR_HANDLE;
  free ((void *) (size_t) *handle);
  *handle = 0;
  return HX_ERR_OK;
}

hx_err
hx_option (hx_csh handle, hx_opt_type type, size_t value)
{
  HxArmHandle * h = (HxArmHandle *) (size_t) handle;

  if (type == HX_OPT_DETAIL)
    h->detail = (value == HX_OPT_ON);

  return HX_ERR_OK;
}

hx_insn *
hx_insn_alloc (hx_csh handle)
{
  hx_insn * insn = calloc (1, sizeof (hx_insn));
  (void) handle;
  if (insn == NULL)
    return NULL;
  insn->detail = calloc (1, sizeof (hx_detail));
  if (insn->detail == NULL)
  {
    free (insn);
    return NULL;
  }
  return insn;
}

void
hx_insn_free (hx_insn * insn, size_t count)
{
  size_t i;
  for (i = 0; i < count; i++)
    free (insn[i].detail);
  free (insn);
}

static uint16_t
hx_thumb_insn_size (const uint8_t * code)
{
  uint32_t h0 = (uint32_t) code[0] | ((uint32_t) code[1] << 8);
  return (((h0 >> 11) & 0x1f) >= 0x1d) ? 4 : 2;
}

bool
hx_disasm_iter (hx_csh handle, const uint8_t ** code, size_t * size,
    uint64_t * address, hx_insn * insn)
{
  HxArmHandle * h = (HxArmHandle *) (size_t) handle;

  if (h->thumb)
  {
    uint16_t insn_size;
    if (*size < 2)
      return false;
    insn_size = hx_thumb_insn_size (*code);
    if (*size < insn_size)
      return false;
    hx_decode_thumb (*code, *address, insn);
  }
  else
  {
    if (*size < 4)
      return false;
    hx_decode_arm (*code, *address, insn);
  }

  *code += insn->size;
  *size -= insn->size;
  *address += insn->size;

  return true;
}

size_t
hx_disasm (hx_csh handle, const uint8_t * code, size_t code_size,
    uint64_t address, size_t count, hx_insn ** insn)
{
  HxArmHandle * h = (HxArmHandle *) (size_t) handle;
  size_t cap = (count != 0) ? count : 64;
  size_t n = 0;
  hx_insn * arr;
  const uint8_t * p = code;
  size_t remaining = code_size;
  uint64_t addr = address;

  arr = calloc (cap, sizeof (hx_insn));
  if (arr == NULL)
    return 0;

  while (remaining >= (h->thumb ? 2u : 4u) && (count == 0 || n < count))
  {
    uint16_t isize;

    if (h->thumb)
    {
      isize = hx_thumb_insn_size (p);
      if (remaining < isize)
        break;
    }
    else
    {
      isize = 4;
    }

    arr[n].detail = calloc (1, sizeof (hx_detail));
    if (arr[n].detail == NULL)
      break;

    if (h->thumb)
      hx_decode_thumb (p, addr, &arr[n]);
    else
      hx_decode_arm (p, addr, &arr[n]);

    p += arr[n].size;
    remaining -= arr[n].size;
    addr += arr[n].size;
    n++;
    if (n == cap && count == 0)
    {
      hx_insn * grown;
      cap *= 2;
      grown = realloc (arr, cap * sizeof (hx_insn));
      if (grown == NULL)
        break;
      arr = grown;
    }
  }

  *insn = arr;
  return n;
}

static bool
hx_op_touches_reg (const hx_insn * insn, unsigned int reg_id)
{
  const hx_arm * d = &insn->detail->arm;
  uint8_t i;
  for (i = 0; i < d->op_count; i++)
  {
    if (d->operands[i].type == HX_ARM_OP_REG &&
        (unsigned int) d->operands[i].reg == reg_id)
      return true;
  }
  return false;
}

bool
hx_reg_read (hx_csh handle, const hx_insn * insn, unsigned int reg_id)
{
  (void) handle;
  return hx_op_touches_reg (insn, reg_id);
}

bool
hx_reg_write (hx_csh handle, const hx_insn * insn, unsigned int reg_id)
{
  (void) handle;
  return hx_op_touches_reg (insn, reg_id);
}

void
hx_arch_register_arm (void)
{
}
