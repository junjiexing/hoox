/*
 * hoox — compact x86/x64 instruction decoder backing the capstone shim.
 *
 * A length + control-flow + relocation-metadata decoder, sufficient for the
 * frida-gum x86 relocator/reader/interceptor. It is NOT a general
 * disassembler: it computes instruction length, classifies the control-flow /
 * relocatable instruction set, and exposes the ModRM/displacement encoding the
 * RIP-relative rewriter needs.
 *
 * The opcode-attribute tables and decoding structure are ported from Microsoft
 * Detours' relocation engine (src/disasm.cpp).
 *
 * Licence: wxWindows Library Licence, Version 3.1
 * Portions derive from Microsoft Detours (MIT), Copyright (C) Microsoft
 * Corporation; see NOTICE.
 */

#include "capstone.h"

#include <string.h>
#include <stdlib.h>

/* ---- opcode attribute classes ------------------------------------------- */

enum
{
  OC_INVALID = 0,
  OC_L1,          /* 1 byte, no modrm, no imm */
  OC_IB,          /* + imm8 */
  OC_IW,          /* + imm16 */
  OC_ENTER,       /* + imm16 + imm8 (ENTER) */
  OC_IZ,          /* + immz (2/4 by opsize), no modrm */
  OC_MOFFS,       /* + moffs (addr-size), no modrm (A0-A3) */
  OC_MOVIMM,      /* + immv (immz, or 8 if REX.W) (B8-BF) */
  OC_REL_Z,       /* + rel z, code target (E8/E9, 0F80-8F) */
  OC_JCC8,        /* + rel8 short jcc/jmp (70-7F, EB) */
  OC_LOOP8,       /* + rel8, no-enlarge (E0-E3) */
  OC_M,           /* modrm, no imm */
  OC_M_IB,        /* modrm + imm8 */
  OC_M_IZ,        /* modrm + immz */
  OC_IW_EOI,      /* + imm16, end-of-instr (C2 RET imm16) */
  OC_L1_DYN,      /* 1 byte dynamic (CB/CC/CF/F1) */
  OC_IB_DYN,      /* + imm8 dynamic (CD INT ib) */
  OC_FARPTR,      /* 9A/EA far ptr (x86 only) */
  OC_PREFIX,      /* generic prefix (F0 lock) */
  OC_SEG,         /* segment override prefix */
  OC_P66, OC_P67, OC_PF2, OC_PF3,
  OC_REX,         /* 40-4F (x64) */
  OC_REX2,        /* D5 (x64 APX) */
  OC_ESC0F,       /* 0F escape */
  OC_ESC38,       /* 0F 38 (3-byte) modrm */
  OC_ESC3A,       /* 0F 3A (3-byte) modrm + imm8 */
  OC_VEX2, OC_VEX3, OC_EVEX, OC_XOP,
  OC_GRP_F6, OC_GRP_F7, OC_GRP_FF, OC_GRP_C7
};

/* one-byte opcode map (values identical for x86/x64 except where the decoder
 * special-cases prefixes/REX at runtime) */
static const uint8_t oc1[256] =
{
  /* 00 */ OC_M, OC_M, OC_M, OC_M, OC_IB, OC_IZ, OC_L1, OC_L1,
  /* 08 */ OC_M, OC_M, OC_M, OC_M, OC_IB, OC_IZ, OC_L1, OC_ESC0F,
  /* 10 */ OC_M, OC_M, OC_M, OC_M, OC_IB, OC_IZ, OC_L1, OC_L1,
  /* 18 */ OC_M, OC_M, OC_M, OC_M, OC_IB, OC_IZ, OC_L1, OC_L1,
  /* 20 */ OC_M, OC_M, OC_M, OC_M, OC_IB, OC_IZ, OC_SEG, OC_L1,
  /* 28 */ OC_M, OC_M, OC_M, OC_M, OC_IB, OC_IZ, OC_SEG, OC_L1,
  /* 30 */ OC_M, OC_M, OC_M, OC_M, OC_IB, OC_IZ, OC_SEG, OC_L1,
  /* 38 */ OC_M, OC_M, OC_M, OC_M, OC_IB, OC_IZ, OC_SEG, OC_L1,
  /* 40 */ OC_REX, OC_REX, OC_REX, OC_REX, OC_REX, OC_REX, OC_REX, OC_REX,
  /* 48 */ OC_REX, OC_REX, OC_REX, OC_REX, OC_REX, OC_REX, OC_REX, OC_REX,
  /* 50 */ OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1,
  /* 58 */ OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1,
  /* 60 */ OC_L1, OC_L1, OC_EVEX, OC_M, OC_SEG, OC_SEG, OC_P66, OC_P67,
  /* 68 */ OC_IZ, OC_M_IZ, OC_IB, OC_M_IB, OC_L1, OC_L1, OC_L1, OC_L1,
  /* 70 */ OC_JCC8, OC_JCC8, OC_JCC8, OC_JCC8, OC_JCC8, OC_JCC8, OC_JCC8, OC_JCC8,
  /* 78 */ OC_JCC8, OC_JCC8, OC_JCC8, OC_JCC8, OC_JCC8, OC_JCC8, OC_JCC8, OC_JCC8,
  /* 80 */ OC_M_IB, OC_M_IZ, OC_M_IB, OC_M_IB, OC_M, OC_M, OC_M, OC_M,
  /* 88 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_XOP,
  /* 90 */ OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1,
  /* 98 */ OC_L1, OC_L1, OC_FARPTR, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1,
  /* A0 */ OC_MOFFS, OC_MOFFS, OC_MOFFS, OC_MOFFS, OC_L1, OC_L1, OC_L1, OC_L1,
  /* A8 */ OC_IB, OC_IZ, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1,
  /* B0 */ OC_IB, OC_IB, OC_IB, OC_IB, OC_IB, OC_IB, OC_IB, OC_IB,
  /* B8 */ OC_MOVIMM, OC_MOVIMM, OC_MOVIMM, OC_MOVIMM,
           OC_MOVIMM, OC_MOVIMM, OC_MOVIMM, OC_MOVIMM,
  /* C0 */ OC_M_IB, OC_M_IB, OC_IW_EOI, OC_L1, OC_VEX3, OC_VEX2, OC_M_IB, OC_GRP_C7,
  /* C8 */ OC_ENTER, OC_L1, OC_IW_EOI, OC_L1_DYN, OC_L1_DYN, OC_IB_DYN, OC_L1_DYN, OC_L1_DYN,
  /* D0 */ OC_M, OC_M, OC_M, OC_M, OC_IB, OC_REX2, OC_INVALID, OC_L1,
  /* D8 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* E0 */ OC_LOOP8, OC_LOOP8, OC_LOOP8, OC_LOOP8, OC_IB, OC_IB, OC_IB, OC_IB,
  /* E8 */ OC_REL_Z, OC_REL_Z, OC_FARPTR, OC_JCC8, OC_L1, OC_L1, OC_L1, OC_L1,
  /* F0 */ OC_PREFIX, OC_L1_DYN, OC_PF2, OC_PF3, OC_L1, OC_L1, OC_GRP_F6, OC_GRP_F7,
  /* F8 */ OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_M, OC_GRP_FF
};

/* two-byte opcode map (0F xx) */
static const uint8_t oc0f[256] =
{
  /* 00 */ OC_M, OC_M, OC_M, OC_M, OC_INVALID, OC_L1, OC_L1, OC_L1,
  /* 08 */ OC_L1, OC_L1, OC_INVALID, OC_L1, OC_INVALID, OC_M, OC_L1, OC_M_IB,
  /* 10 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* 18 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* 20 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_INVALID, OC_M, OC_INVALID,
  /* 28 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* 30 */ OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_INVALID, OC_L1,
  /* 38 */ OC_ESC38, OC_INVALID, OC_ESC3A, OC_INVALID, OC_INVALID, OC_INVALID, OC_INVALID, OC_INVALID,
  /* 40 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* 48 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* 50 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* 58 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* 60 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* 68 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* 70 */ OC_M_IB, OC_M_IB, OC_M_IB, OC_M_IB, OC_M, OC_M, OC_M, OC_L1,
  /* 78 */ OC_M, OC_M, OC_INVALID, OC_INVALID, OC_M, OC_M, OC_M, OC_M,
  /* 80 */ OC_REL_Z, OC_REL_Z, OC_REL_Z, OC_REL_Z, OC_REL_Z, OC_REL_Z, OC_REL_Z, OC_REL_Z,
  /* 88 */ OC_REL_Z, OC_REL_Z, OC_REL_Z, OC_REL_Z, OC_REL_Z, OC_REL_Z, OC_REL_Z, OC_REL_Z,
  /* 90 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* 98 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* A0 */ OC_L1, OC_L1, OC_L1, OC_M, OC_M_IB, OC_M, OC_M, OC_M,
  /* A8 */ OC_L1, OC_L1, OC_L1, OC_M, OC_M_IB, OC_M, OC_M, OC_M,
  /* B0 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* B8 */ OC_M, OC_INVALID, OC_M_IB, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* C0 */ OC_M, OC_M, OC_M_IB, OC_M, OC_M_IB, OC_M_IB, OC_M_IB, OC_M,
  /* C8 */ OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1, OC_L1,
  /* D0 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* D8 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* E0 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* E8 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* F0 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M,
  /* F8 */ OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_M, OC_INVALID
};

/* ---- decode state ------------------------------------------------------- */

typedef struct
{
  bool mode64;

  const uint8_t * p;      /* cursor */
  const uint8_t * start;

  bool opsize;            /* 0x66 */
  bool addrsize;          /* 0x67 */
  bool has_rex;
  uint8_t rex;            /* full REX byte (0x40-0x4F) */
  uint8_t seg;            /* segment override prefix byte, 0 if none */
  bool repne;             /* F2 */
  bool rep;               /* F3 */
} DecState;

static x86_reg
gpr64 (int idx)
{
  static const x86_reg map[16] = {
    X86_REG_RAX, X86_REG_RCX, X86_REG_RDX, X86_REG_RBX,
    X86_REG_RSP, X86_REG_RBP, X86_REG_RSI, X86_REG_RDI,
    X86_REG_R8, X86_REG_R9, X86_REG_R10, X86_REG_R11,
    X86_REG_R12, X86_REG_R13, X86_REG_R14, X86_REG_R15
  };
  return map[idx & 15];
}

static int
imm_z_size (DecState * st)
{
  return st->opsize ? 2 : 4;
}

/* Decode ModRM + SIB + displacement; advance cursor past them. Fills the
 * cs_x86 detail (modrm byte, offsets, disp, first memory/reg operand). */
static void
decode_modrm (DecState * st,
              cs_insn * insn,
              cs_x86 * x86,
              const uint8_t * insn_start)
{
  uint8_t modrm = *st->p;
  int mod = modrm >> 6;
  int rm = modrm & 7;
  int reg = (modrm >> 3) & 7;
  int disp_size = 0;
  bool has_sib = false;
  uint8_t sib = 0;

  x86->modrm = modrm;
  x86->encoding.modrm_offset = (uint8_t) (st->p - insn_start);
  st->p++;

  if (mod == 3)
  {
    /* register operand */
    x86->operands[0].type = X86_OP_REG;
    x86->operands[0].reg = gpr64 (rm + ((st->rex & 0x1) ? 8 : 0));
    x86->op_count = 1;
    (void) reg;
    return;
  }

  /* memory operand */
  if (rm == 4)
  {
    has_sib = true;
    sib = *st->p;
    x86->sib = sib;
    st->p++;
  }

  if (mod == 0)
  {
    if (rm == 5)
    {
      disp_size = 4;   /* disp32; RIP-relative in 64-bit */
    }
    else if (has_sib && (sib & 7) == 5)
    {
      disp_size = 4;
    }
  }
  else if (mod == 1)
  {
    disp_size = 1;
  }
  else /* mod == 2 */
  {
    disp_size = 4;
  }

  /* first operand = memory */
  {
    cs_x86_op * op = &x86->operands[0];
    op->type = X86_OP_MEM;
    op->mem.segment = X86_REG_INVALID;
    op->mem.scale = 1;
    op->mem.index = X86_REG_INVALID;
    op->mem.base = X86_REG_INVALID;
    op->mem.disp = 0;

    if (mod == 0 && rm == 5)
    {
      op->mem.base = st->mode64 ? X86_REG_RIP : X86_REG_INVALID;
    }
    else if (has_sib)
    {
      int base = sib & 7;
      int index = (sib >> 3) & 7;
      int scale = 1 << ((sib >> 6) & 3);
      if (!(mod == 0 && base == 5))
        op->mem.base = gpr64 (base + ((st->rex & 0x1) ? 8 : 0));
      if (index != 4)
      {
        op->mem.index = gpr64 (index + ((st->rex & 0x2) ? 8 : 0));
        op->mem.scale = scale;
      }
    }
    else
    {
      op->mem.base = gpr64 (rm + ((st->rex & 0x1) ? 8 : 0));
    }
    x86->op_count = 1;
  }

  if (disp_size != 0)
  {
    int64_t disp = 0;
    x86->encoding.disp_offset = (uint8_t) (st->p - insn_start);
    x86->encoding.disp_size = (uint8_t) disp_size;
    if (disp_size == 1)
      disp = (int8_t) st->p[0];
    else
      disp = (int32_t) ((uint32_t) st->p[0] | ((uint32_t) st->p[1] << 8) |
          ((uint32_t) st->p[2] << 16) | ((uint32_t) st->p[3] << 24));
    x86->disp = disp;
    x86->operands[0].mem.disp = disp;
    st->p += disp_size;
  }

  (void) insn;
}

/* Consume the immediate of the given byte count. */
static void
skip_imm (DecState * st, int n)
{
  st->p += n;
}

/* ---- control-flow classification ---------------------------------------- */

static const x86_insn jcc_map[16] = {
  X86_INS_JO, X86_INS_JNO, X86_INS_JB, X86_INS_JAE,
  X86_INS_JE, X86_INS_JNE, X86_INS_JBE, X86_INS_JA,
  X86_INS_JS, X86_INS_JNS, X86_INS_JP, X86_INS_JNP,
  X86_INS_JL, X86_INS_JGE, X86_INS_JLE, X86_INS_JG
};

static void
set_imm_target (cs_x86 * x86, int64_t target)
{
  x86->operands[0].type = X86_OP_IMM;
  x86->operands[0].imm = target;
  x86->op_count = 1;
}

static const x86_insn alu_group[8] = {
  X86_INS_ADD, X86_INS_OR, X86_INS_ADC, X86_INS_SBB,
  X86_INS_AND, X86_INS_SUB, X86_INS_XOR, X86_INS_CMP
};

/* Classify common one-byte data-processing opcodes (best-effort; anything not
 * recognised stays X86_INS_OTHER). */
static x86_insn
classify_dp_1byte (uint8_t p, uint8_t modrm, bool mode64)
{
  if (p < 0x40 && (p & 7) <= 5)
    return alu_group[(p >> 3) & 7];      /* ADD..CMP r/imm forms */
  if (p >= 0x88 && p <= 0x8B)
    return X86_INS_MOV;
  if (p == 0x8C || p == 0x8E)
    return X86_INS_MOV;
  if (p == 0x8D)
    return X86_INS_LEA;
  if (p >= 0xB0 && p <= 0xBF)
    return X86_INS_MOV;
  if (p == 0xC6 || p == 0xC7)
    return X86_INS_MOV;
  if (p >= 0xA0 && p <= 0xA3)
    return X86_INS_MOV;
  if (p == 0x84 || p == 0x85 || p == 0xA8 || p == 0xA9)
    return X86_INS_TEST;
  if (p >= 0x58 && p <= 0x5F)
    return X86_INS_POP;
  if (p == 0x80 || p == 0x81 || p == 0x82 || p == 0x83)
    return alu_group[(modrm >> 3) & 7];  /* group 1 */
  if (p == 0xFE)
    return (((modrm >> 3) & 7) == 0) ? X86_INS_INC : X86_INS_DEC;
  if (!mode64 && p >= 0x40 && p <= 0x47)
    return X86_INS_INC;
  if (!mode64 && p >= 0x48 && p <= 0x4F)
    return X86_INS_DEC;
  return X86_INS_OTHER;
}

/* Populate detail->regs_read/write for the curated set of RIP-capable
 * instructions that implicitly use GP registers, keeping the relocator
 * scratch-register selection (via cs_reg_read/write) safe. */
static void
note_implicit_regs (cs_insn * insn, uint8_t op1, uint8_t op0f, uint8_t modrm)
{
  cs_detail * d = insn->detail;
  int ext = (modrm >> 3) & 7;

  d->regs_read_count = 0;
  d->regs_write_count = 0;

  if (op0f == 0)
  {
    if (op1 == 0xF6 || op1 == 0xF7)
    {
      if (ext >= 4) /* MUL/IMUL/DIV/IDIV */
      {
        d->regs_read[d->regs_read_count++] = X86_REG_RAX;
        d->regs_write[d->regs_write_count++] = X86_REG_RAX;
        if (op1 == 0xF7)
        {
          d->regs_read[d->regs_read_count++] = X86_REG_RDX;
          d->regs_write[d->regs_write_count++] = X86_REG_RDX;
        }
      }
    }
    else if (op1 == 0xD2 || op1 == 0xD3) /* shift by CL */
    {
      d->regs_read[d->regs_read_count++] = X86_REG_RCX;
    }
  }
  else
  {
    if (op0f == 0xB0 || op0f == 0xB1) /* CMPXCHG */
    {
      d->regs_read[d->regs_read_count++] = X86_REG_RAX;
      d->regs_write[d->regs_write_count++] = X86_REG_RAX;
    }
    else if (op0f == 0xC7) /* CMPXCHG8B/16B */
    {
      d->regs_read[d->regs_read_count++] = X86_REG_RAX;
      d->regs_read[d->regs_read_count++] = X86_REG_RDX;
      d->regs_write[d->regs_write_count++] = X86_REG_RAX;
      d->regs_write[d->regs_write_count++] = X86_REG_RDX;
    }
  }
}

/* ---- the decoder -------------------------------------------------------- */

static bool
hx_decode (bool mode64,
           const uint8_t * code,
           size_t avail,
           uint64_t address,
           cs_insn * insn)
{
  DecState st;
  cs_x86 * x86 = &insn->detail->x86;
  const uint8_t * insn_start = code;
  uint8_t op, op0f = 0;
  uint8_t oc;
  const uint8_t * map = oc1;
  int vex_map = 0;
  bool is_vex = false;

  memset (&st, 0, sizeof (st));
  st.mode64 = mode64;
  st.p = code;
  st.start = code;

  memset (x86, 0, sizeof (*x86));
  insn->id = X86_INS_OTHER;
  insn->address = address;
  insn->detail->regs_read_count = 0;
  insn->detail->regs_write_count = 0;

  if (avail == 0)
    return false;

  /* legacy prefixes + REX */
  for (;;)
  {
    op = *st.p;
    oc = oc1[op];

    if (oc == OC_P66) { st.opsize = true; st.p++; continue; }
    if (oc == OC_P67) { st.addrsize = true; st.p++; continue; }
    if (oc == OC_PF2) { st.repne = true; st.p++; continue; }
    if (oc == OC_PF3) { st.rep = true; st.p++; continue; }
    if (oc == OC_SEG) { st.seg = op; st.p++; continue; }
    if (oc == OC_PREFIX) { st.p++; continue; }
    if (oc == OC_REX && mode64)
    {
      st.has_rex = true;
      st.rex = op;
      st.p++;
      op = *st.p;
      oc = oc1[op];
      break;
    }
    break;
  }

  /* VEX / EVEX / XOP */
  if (oc == OC_VEX2 && (mode64 || (st.p[1] & 0xC0) == 0xC0))
  {
    is_vex = true; vex_map = 1;
    st.p += 2;
  }
  else if (oc == OC_VEX3 && (mode64 || (st.p[1] & 0xC0) == 0xC0))
  {
    is_vex = true;
    vex_map = st.p[1] & 0x1f;
    if (st.p[2] & 0x80) st.rex |= 0x8;
    st.p += 3;
  }
  else if (oc == OC_XOP && (st.p[1] & 0x1f) >= 8)
  {
    is_vex = true;
    vex_map = st.p[1] & 0x1f;
    st.p += 3;
  }
  else if (oc == OC_EVEX && (mode64 || (st.p[1] & 0xC0) == 0xC0))
  {
    is_vex = true;
    vex_map = st.p[1] & 0x07;
    if (st.p[2] & 0x80) st.rex |= 0x8;
    st.p += 4;
  }

  if (is_vex)
  {
    uint8_t vop = *st.p;
    st.p++;
    decode_modrm (&st, insn, x86, insn_start);
    if (vex_map == 3 || vex_map == 10 ||
        (vex_map == 1 && (vop == 0x70 || vop == 0x71 || vop == 0x72 ||
            vop == 0x73 || vop == 0xC2 || vop == 0xC4 || vop == 0xC5 ||
            vop == 0xC6)))
    {
      skip_imm (&st, 1);
    }
    goto done;
  }

  /* opcode escape */
  if (oc == OC_ESC0F)
  {
    st.p++;
    op0f = *st.p;
    op = op0f;
    oc = oc0f[op0f];

    if (oc == OC_ESC38)
    {
      st.p++;
      st.p++;
      decode_modrm (&st, insn, x86, insn_start);
      map = oc0f;
      goto done;
    }
    if (oc == OC_ESC3A)
    {
      st.p++;
      st.p++;
      decode_modrm (&st, insn, x86, insn_start);
      skip_imm (&st, 1);
      map = oc0f;
      goto done;
    }

    st.p++;
    map = oc0f;
  }
  else
  {
    st.p++;
    map = oc1;
  }

  /* dispatch on op-class */
  switch (oc)
  {
    case OC_L1:
    case OC_L1_DYN:
      break;
    case OC_IB:
    case OC_IB_DYN:
      skip_imm (&st, 1);
      break;
    case OC_IW:
    case OC_IW_EOI:
      skip_imm (&st, 2);
      break;
    case OC_ENTER:
      skip_imm (&st, 3);
      break;
    case OC_IZ:
      skip_imm (&st, imm_z_size (&st));
      break;
    case OC_MOFFS:
      skip_imm (&st, st.mode64 ? (st.addrsize ? 4 : 8) : (st.addrsize ? 2 : 4));
      break;
    case OC_MOVIMM:
      if (st.rex & 0x8)
        skip_imm (&st, 8);
      else
        skip_imm (&st, imm_z_size (&st));
      break;
    case OC_M:
      decode_modrm (&st, insn, x86, insn_start);
      break;
    case OC_M_IB:
      decode_modrm (&st, insn, x86, insn_start);
      skip_imm (&st, 1);
      break;
    case OC_M_IZ:
    case OC_GRP_C7:
      decode_modrm (&st, insn, x86, insn_start);
      skip_imm (&st, imm_z_size (&st));
      break;
    case OC_GRP_F6:
      decode_modrm (&st, insn, x86, insn_start);
      if (((x86->modrm >> 3) & 6) == 0)
        skip_imm (&st, 1);
      break;
    case OC_GRP_F7:
      decode_modrm (&st, insn, x86, insn_start);
      if (((x86->modrm >> 3) & 6) == 0)
        skip_imm (&st, imm_z_size (&st));
      break;
    case OC_GRP_FF:
      decode_modrm (&st, insn, x86, insn_start);
      break;
    case OC_JCC8:
    case OC_LOOP8:
    {
      int8_t rel = (int8_t) st.p[0];
      st.p += 1;
      set_imm_target (x86, (int64_t) (address + (uint64_t) (st.p - insn_start) + (int64_t) rel));
      break;
    }
    case OC_REL_Z:
    {
      int zsz = imm_z_size (&st);
      int32_t rel;
      if (zsz == 2)
        rel = (int16_t) ((uint16_t) st.p[0] | ((uint16_t) st.p[1] << 8));
      else
        rel = (int32_t) ((uint32_t) st.p[0] | ((uint32_t) st.p[1] << 8) |
            ((uint32_t) st.p[2] << 16) | ((uint32_t) st.p[3] << 24));
      st.p += zsz;
      set_imm_target (x86, (int64_t) (address + (uint64_t) (st.p - insn_start) + (int64_t) rel));
      break;
    }
    case OC_FARPTR:
      if (st.mode64) { insn->size = 1; return false; }
      skip_imm (&st, st.opsize ? 4 : 6);
      break;
    case OC_INVALID:
    default:
      insn->size = 1;
      return false;
  }

done:
  insn->size = (uint16_t) (st.p - insn_start);
  if ((size_t) insn->size > avail)
  {
    insn->size = 1;
    return false;
  }

  memcpy (insn->bytes, insn_start, insn->size);
  x86->prefix[0] = st.seg;

  /* classify id */
  {
    uint8_t primary = op;
    bool two_byte = (map == oc0f);

    if (!is_vex && !two_byte)
    {
      if (primary == 0xE8) insn->id = X86_INS_CALL;
      else if (primary == 0xE9 || primary == 0xEB) insn->id = X86_INS_JMP;
      else if (primary >= 0x70 && primary <= 0x7F)
        insn->id = jcc_map[primary - 0x70];
      else if (primary == 0xE3)
        insn->id = st.mode64 ? X86_INS_JRCXZ : X86_INS_JECXZ;
      else if (primary == 0xE0) insn->id = X86_INS_LOOPNE;
      else if (primary == 0xE1) insn->id = X86_INS_LOOPE;
      else if (primary == 0xE2) insn->id = X86_INS_LOOP;
      else if (primary == 0xC2 || primary == 0xC3) insn->id = X86_INS_RET;
      else if (primary == 0xCA || primary == 0xCB) insn->id = X86_INS_RETF;
      else if (primary == 0xCC) insn->id = X86_INS_INT3;
      else if (primary == 0xCD || primary == 0xF1) insn->id = X86_INS_INT;
      else if (primary == 0x90 && !st.rep) insn->id = X86_INS_NOP;
      else if (primary >= 0x50 && primary <= 0x57) insn->id = X86_INS_PUSH;
      else if (primary == 0x68 || primary == 0x6A) insn->id = X86_INS_PUSH;
      else if (primary == 0xFF)
      {
        int ext = (x86->modrm >> 3) & 7;
        if (ext == 2 || ext == 3) insn->id = X86_INS_CALL;
        else if (ext == 4 || ext == 5) insn->id = X86_INS_JMP;
        else if (ext == 6) insn->id = X86_INS_PUSH;
        else if (ext == 0) insn->id = X86_INS_INC;
        else if (ext == 1) insn->id = X86_INS_DEC;
      }

      if (insn->id == X86_INS_OTHER)
        insn->id = classify_dp_1byte (primary, x86->modrm, st.mode64);
    }
    else if (!is_vex && two_byte)
    {
      if (primary >= 0x80 && primary <= 0x8F)
        insn->id = jcc_map[primary - 0x80];   /* near Jcc (0F 8x rel32) */
      else if (primary == 0x05) insn->id = X86_INS_SYSCALL;
      else if (primary == 0x34) insn->id = X86_INS_SYSENTER;
      else if (primary == 0x1F) insn->id = X86_INS_NOP;
      else if (primary == 0xB0 || primary == 0xB1) insn->id = X86_INS_CMPXCHG;
    }

    if (!is_vex)
      note_implicit_regs (insn, two_byte ? 0 : primary, two_byte ? primary : 0,
          x86->modrm);
  }

  return true;
}

/* ---- capstone shim API -------------------------------------------------- */

typedef struct
{
  bool mode64;
  bool detail;
} HxHandle;

cs_err
cs_open (cs_arch arch, cs_mode mode, csh * handle)
{
  HxHandle * h;

  if (arch != CS_ARCH_X86)
    return CS_ERR_ARCH;

  h = calloc (1, sizeof (HxHandle));
  if (h == NULL)
    return CS_ERR_MEM;

  h->mode64 = (mode & CS_MODE_64) != 0;
  h->detail = false;
  *handle = (csh) (size_t) h;

  return CS_ERR_OK;
}

cs_err
cs_close (csh * handle)
{
  if (handle == NULL || *handle == 0)
    return CS_ERR_HANDLE;
  free ((void *) (size_t) *handle);
  *handle = 0;
  return CS_ERR_OK;
}

cs_err
cs_option (csh handle, cs_opt_type type, size_t value)
{
  HxHandle * h = (HxHandle *) (size_t) handle;

  if (type == CS_OPT_DETAIL)
    h->detail = (value == CS_OPT_ON);
  else if (type == CS_OPT_MODE)
    h->mode64 = (value & CS_MODE_64) != 0;

  return CS_ERR_OK;
}

cs_insn *
cs_malloc (csh handle)
{
  cs_insn * insn = calloc (1, sizeof (cs_insn));
  (void) handle;
  if (insn == NULL)
    return NULL;
  insn->detail = calloc (1, sizeof (cs_detail));
  if (insn->detail == NULL)
  {
    free (insn);
    return NULL;
  }
  return insn;
}

void
cs_free (cs_insn * insn, size_t count)
{
  size_t i;
  for (i = 0; i < count; i++)
    free (insn[i].detail);
  free (insn);
}

bool
cs_disasm_iter (csh handle, const uint8_t ** code, size_t * size,
    uint64_t * address, cs_insn * insn)
{
  HxHandle * h = (HxHandle *) (size_t) handle;

  if (*size == 0)
    return false;

  if (!hx_decode (h->mode64, *code, *size, *address, insn))
    return false;

  *code += insn->size;
  *size -= insn->size;
  *address += insn->size;

  return true;
}

size_t
cs_disasm (csh handle, const uint8_t * code, size_t code_size,
    uint64_t address, size_t count, cs_insn ** insn)
{
  HxHandle * h = (HxHandle *) (size_t) handle;
  size_t cap = (count != 0) ? count : 64;
  size_t n = 0;
  cs_insn * arr = calloc (cap, sizeof (cs_insn));
  const uint8_t * p = code;
  size_t remaining = code_size;
  uint64_t addr = address;

  if (arr == NULL)
    return 0;

  while (remaining > 0 && (count == 0 || n < count))
  {
    cs_detail * detail = calloc (1, sizeof (cs_detail));
    arr[n].detail = detail;
    if (!hx_decode (h->mode64, p, remaining, addr, &arr[n]))
    {
      free (detail);
      arr[n].detail = NULL;
      break;
    }
    p += arr[n].size;
    remaining -= arr[n].size;
    addr += arr[n].size;
    n++;
    if (n == cap && count == 0)
    {
      cap *= 2;
      arr = realloc (arr, cap * sizeof (cs_insn));
    }
  }

  *insn = arr;
  return n;
}

static bool
reg_in (const uint16_t * arr, uint8_t n, unsigned int reg)
{
  uint8_t i;
  for (i = 0; i < n; i++)
    if (arr[i] == reg)
      return true;
  return false;
}

bool
cs_reg_read (csh handle, const cs_insn * insn, unsigned int reg_id)
{
  (void) handle;
  return reg_in (insn->detail->regs_read, insn->detail->regs_read_count,
      (uint16_t) reg_id);
}

bool
cs_reg_write (csh handle, const cs_insn * insn, unsigned int reg_id)
{
  (void) handle;
  return reg_in (insn->detail->regs_write, insn->detail->regs_write_count,
      (uint16_t) reg_id);
}

void
cs_arch_register_x86 (void)
{
}
