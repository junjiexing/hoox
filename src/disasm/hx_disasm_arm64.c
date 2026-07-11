/*
 * hoox — compact AArch64 (arm64) decoder backing the capstone-compatible shim.
 *
 * hoox does not depend on capstone. This decoder provides just enough of the
 * capstone arm64 surface that the extracted frida-gum relocator/reader/
 * interceptor consume: it precisely decodes the PC-relative and branch/control
 * instructions those paths rewrite or key off (ADR/ADRP, literal LDR/LDRSW,
 * B/B.cond, BL, CBZ/CBNZ, TBZ/TBNZ, BR/BLR/RET, SVC), plus MOV(reg) and STP
 * for two backend heuristics. Every other instruction is returned with a valid
 * (id == HX_ARM64_INS_INVALID) result so the relocator copies it verbatim; for
 * those, general-purpose register operands are still reported conservatively so
 * scratch-register selection never picks a register the moved code uses.
 *
 * AArch64 is fixed-width (4 bytes, little-endian code stream), so decoding is
 * arch-neutral C: it produces identical results on any host, which is what lets
 * the differential test validate it without arm64 hardware.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include <hx_disasm.h>

#include <stdlib.h>
#include <string.h>

/* ---- helpers ------------------------------------------------------------ */

static int64_t
hx_sign_extend (uint64_t value, unsigned bits)
{
  uint64_t m = (uint64_t) 1 << (bits - 1);
  return (int64_t) ((value ^ m) - m);
}

/* Map a 5-bit GP field to an X-register enum value (31 -> XZR). */
static hx_arm64_reg
hx_x_reg (uint32_t n)
{
  return (n == 31) ? HX_ARM64_REG_XZR : (hx_arm64_reg) (HX_ARM64_REG_X0 + n);
}

/* Map a 5-bit GP field to a W-register enum value (31 -> WZR). */
static hx_arm64_reg
hx_w_reg (uint32_t n)
{
  return (n == 31) ? HX_ARM64_REG_WZR : (hx_arm64_reg) (HX_ARM64_REG_W0 + n);
}

static void
hx_set_reg_op (hx_arm64_op * op, hx_arm64_reg reg)
{
  op->type = HX_ARM64_OP_REG;
  op->reg = reg;
}

static void
hx_set_imm_op (hx_arm64_op * op, int64_t imm)
{
  op->type = HX_ARM64_OP_IMM;
  op->imm = imm;
}

/*
 * Conservative operand fill for instructions we don't individually classify:
 * report the four canonical GP register-field positions so a later scratch-reg
 * scan never mistakes a busy register for a free one. Over-reporting is safe
 * (it only makes selection stricter); the scan compares reg identity only.
 */
static void
hx_fill_conservative_regs (hx_arm64 * d, uint32_t insn)
{
  uint32_t op0 = (insn >> 25) & 0xf;   /* bits[28:25] */
  bool is_dp_reg = (op0 & 0x7) == 0x5;             /* x101 */
  bool is_dp_imm = (op0 & 0xe) == 0x8;             /* 100x */
  bool is_ldst = ((insn >> 27) & 0x1) == 0x1 &&
      ((insn >> 25) & 0x1) == 0x0;                       /* x1x0 */
  uint32_t fields[4];
  unsigned nf = 0, i;

  if (!(is_dp_reg || is_dp_imm || is_ldst))
    return;

  fields[nf++] = insn & 0x1f;              /* Rd / Rt   [4:0]   */
  fields[nf++] = (insn >> 5) & 0x1f;       /* Rn        [9:5]   */
  if (is_dp_reg || is_ldst)
    fields[nf++] = (insn >> 16) & 0x1f;    /* Rm        [20:16] */
  if (is_ldst)
    fields[nf++] = (insn >> 10) & 0x1f;    /* Rt2       [14:10] */

  for (i = 0; i < nf && d->op_count < HX_ARM64_MAX_OPS; i++)
    hx_set_reg_op (&d->operands[d->op_count++], hx_x_reg (fields[i]));
}

/* ---- core decode -------------------------------------------------------- */

static void
hx_decode_arm64 (const uint8_t * code, uint64_t address, hx_insn * insn)
{
  uint32_t w = (uint32_t) code[0] | ((uint32_t) code[1] << 8) |
      ((uint32_t) code[2] << 16) | ((uint32_t) code[3] << 24);
  hx_arm64 * d = &insn->detail->arm64;

  insn->id = HX_ARM64_INS_INVALID;
  insn->address = address;
  insn->size = 4;
  memcpy (insn->bytes, code, 4);
  insn->mnemonic[0] = '\0';
  insn->op_str[0] = '\0';

  d->cc = HX_ARM64_CC_INVALID;
  d->op_count = 0;

  /* PC-relative addressing: ADR / ADRP (bits[28:24] == 10000). */
  if ((w & 0x1f000000) == 0x10000000)
  {
    uint32_t rd = w & 0x1f;
    uint64_t immlo = (w >> 29) & 0x3;
    uint64_t immhi = (w >> 5) & 0x7ffff;
    int64_t imm = hx_sign_extend ((immhi << 2) | immlo, 21);

    hx_set_reg_op (&d->operands[0], hx_x_reg (rd));
    if ((w & 0x80000000) != 0)
    {
      insn->id = HX_ARM64_INS_ADRP;
      strcpy (insn->mnemonic, "adrp");
      hx_set_imm_op (&d->operands[1],
          (int64_t) ((address & ~(uint64_t) 0xfff) + (imm << 12)));
    }
    else
    {
      insn->id = HX_ARM64_INS_ADR;
      strcpy (insn->mnemonic, "adr");
      hx_set_imm_op (&d->operands[1], (int64_t) (address + (uint64_t) imm));
    }
    d->op_count = 2;
    return;
  }

  /* Unconditional branch (immediate): B / BL (bits[30:26] == 00101). */
  if ((w & 0x7c000000) == 0x14000000)
  {
    int64_t off = hx_sign_extend ((uint64_t) (w & 0x03ffffff) << 2, 28);
    hx_set_imm_op (&d->operands[0], (int64_t) (address + (uint64_t) off));
    d->op_count = 1;
    if ((w & 0x80000000) != 0)
    {
      insn->id = HX_ARM64_INS_BL;
      strcpy (insn->mnemonic, "bl");
    }
    else
    {
      insn->id = HX_ARM64_INS_B;
      strcpy (insn->mnemonic, "b");
    }
    return;
  }

  /* Conditional branch (immediate): B.cond (bits[31:24]==01010100, bit4==0). */
  if ((w & 0xff000010) == 0x54000000)
  {
    int64_t off = hx_sign_extend ((uint64_t) ((w >> 5) & 0x7ffff) << 2, 21);
    insn->id = HX_ARM64_INS_B;
    strcpy (insn->mnemonic, "b");
    d->cc = (hx_arm64_cc) ((w & 0xf) + 1);
    hx_set_imm_op (&d->operands[0], (int64_t) (address + (uint64_t) off));
    d->op_count = 1;
    return;
  }

  /* Compare and branch: CBZ / CBNZ (bits[30:25] == 011010). */
  if ((w & 0x7e000000) == 0x34000000)
  {
    int64_t off = hx_sign_extend ((uint64_t) ((w >> 5) & 0x7ffff) << 2, 21);
    uint32_t rt = w & 0x1f;
    bool is64 = (w & 0x80000000) != 0;

    insn->id = (w & 0x01000000) ? HX_ARM64_INS_CBNZ : HX_ARM64_INS_CBZ;
    strcpy (insn->mnemonic, (w & 0x01000000) ? "cbnz" : "cbz");
    hx_set_reg_op (&d->operands[0], is64 ? hx_x_reg (rt) : hx_w_reg (rt));
    hx_set_imm_op (&d->operands[1], (int64_t) (address + (uint64_t) off));
    d->op_count = 2;
    return;
  }

  /* Test and branch: TBZ / TBNZ (bits[30:25] == 011011). */
  if ((w & 0x7e000000) == 0x36000000)
  {
    int64_t off = hx_sign_extend ((uint64_t) ((w >> 5) & 0x3fff) << 2, 16);
    uint32_t rt = w & 0x1f;
    uint32_t bit = (((w >> 31) & 0x1) << 5) | ((w >> 19) & 0x1f);
    bool is64 = (w & 0x80000000) != 0;

    insn->id = (w & 0x01000000) ? HX_ARM64_INS_TBNZ : HX_ARM64_INS_TBZ;
    strcpy (insn->mnemonic, (w & 0x01000000) ? "tbnz" : "tbz");
    hx_set_reg_op (&d->operands[0], is64 ? hx_x_reg (rt) : hx_w_reg (rt));
    hx_set_imm_op (&d->operands[1], (int64_t) bit);
    hx_set_imm_op (&d->operands[2], (int64_t) (address + (uint64_t) off));
    d->op_count = 3;
    return;
  }

  /* Load register (literal): LDR / LDRSW (bits[29:27]==011, bits[25:24]==00). */
  if ((w & 0x3b000000) == 0x18000000)
  {
    uint32_t opc = (w >> 30) & 0x3;
    bool is_vector = (w & 0x04000000) != 0;
    uint32_t rt = w & 0x1f;
    int64_t off = hx_sign_extend ((uint64_t) ((w >> 5) & 0x7ffff) << 2, 21);
    hx_arm64_reg reg = HX_ARM64_REG_INVALID;
    bool handled = true;

    if (is_vector)
    {
      insn->id = HX_ARM64_INS_LDR;
      if (opc == 0)
        reg = (hx_arm64_reg) (HX_ARM64_REG_S0 + rt);
      else if (opc == 1)
        reg = (hx_arm64_reg) (HX_ARM64_REG_D0 + rt);
      else if (opc == 2)
        reg = (hx_arm64_reg) (HX_ARM64_REG_Q0 + rt);
      else
        handled = false;
    }
    else if (opc == 0)
    {
      insn->id = HX_ARM64_INS_LDR;
      reg = hx_w_reg (rt);
    }
    else if (opc == 1)
    {
      insn->id = HX_ARM64_INS_LDR;
      reg = hx_x_reg (rt);
    }
    else if (opc == 2)
    {
      insn->id = HX_ARM64_INS_LDRSW;
      reg = hx_x_reg (rt);
    }
    else
    {
      handled = false;   /* opc==3: PRFM literal — leave verbatim */
    }

    if (handled)
    {
      strcpy (insn->mnemonic,
          (insn->id == HX_ARM64_INS_LDRSW) ? "ldrsw" : "ldr");
      hx_set_reg_op (&d->operands[0], reg);
      hx_set_imm_op (&d->operands[1], (int64_t) (address + (uint64_t) off));
      d->op_count = 2;
      return;
    }
    insn->id = HX_ARM64_INS_INVALID;
  }

  /* Unconditional branch (register): BR / BLR / RET (+ pointer-auth). */
  if ((w & 0xfe000000) == 0xd6000000)
  {
    uint32_t opc = (w >> 21) & 0xf;
    uint32_t op3 = (w >> 10) & 0x3f;
    uint32_t rn = (w >> 5) & 0x1f;

    hx_set_reg_op (&d->operands[0], hx_x_reg (rn));
    d->op_count = 1;

    if (opc == 0x0)
    {
      insn->id = (op3 == 0) ? HX_ARM64_INS_BR : HX_ARM64_INS_BRAAZ;
      strcpy (insn->mnemonic, "br");
    }
    else if (opc == 0x1)
    {
      insn->id = (op3 == 0) ? HX_ARM64_INS_BLR : HX_ARM64_INS_BLRAAZ;
      strcpy (insn->mnemonic, "blr");
    }
    else if (opc == 0x2)
    {
      insn->id = HX_ARM64_INS_RET;
      strcpy (insn->mnemonic, "ret");
    }
    else
    {
      /* Other register-branch forms (e.g. ERET) — still terminating. */
      insn->id = HX_ARM64_INS_BR;
      strcpy (insn->mnemonic, "br");
    }
    return;
  }

  /* Exception generation: SVC (bits[31:24]==11010100, LL==01). */
  if ((w & 0xffe0001f) == 0xd4000001)
  {
    insn->id = HX_ARM64_INS_SVC;
    strcpy (insn->mnemonic, "svc");
    d->op_count = 0;
    return;
  }

  /* MOV (register) — alias of ORR (shifted), Rn == 11111, no shift. */
  if ((w & 0x7fe0ffe0) == 0x2a0003e0)
  {
    bool is64 = (w & 0x80000000) != 0;
    uint32_t rd = w & 0x1f;
    uint32_t rm = (w >> 16) & 0x1f;

    insn->id = HX_ARM64_INS_MOV;
    strcpy (insn->mnemonic, "mov");
    hx_set_reg_op (&d->operands[0], is64 ? hx_x_reg (rd) : hx_w_reg (rd));
    hx_set_reg_op (&d->operands[1], is64 ? hx_x_reg (rm) : hx_w_reg (rm));
    d->op_count = 2;
    return;
  }

  /* Load/store register pair — recognise STP for hook-size detection. */
  if ((w & 0x3c000000) == 0x28000000 && ((w >> 22) & 0x1) == 0)
  {
    uint32_t opc = (w >> 30) & 0x3;
    uint32_t rt = w & 0x1f;
    uint32_t rt2 = (w >> 10) & 0x1f;
    uint32_t rn = (w >> 5) & 0x1f;
    bool is64 = (opc == 2);

    insn->id = HX_ARM64_INS_STP;
    strcpy (insn->mnemonic, "stp");
    hx_set_reg_op (&d->operands[0], is64 ? hx_x_reg (rt) : hx_w_reg (rt));
    hx_set_reg_op (&d->operands[1], is64 ? hx_x_reg (rt2) : hx_w_reg (rt2));
    d->operands[2].type = HX_ARM64_OP_MEM;
    d->operands[2].mem.base = hx_x_reg (rn);
    d->operands[2].mem.index = HX_ARM64_REG_INVALID;
    d->operands[2].mem.disp = 0;
    d->op_count = 3;
    return;
  }

  /* Anything else: leave id == INVALID; report GP regs conservatively. */
  hx_fill_conservative_regs (d, w);
}

/* ---- capstone shim API -------------------------------------------------- */

typedef struct
{
  bool detail;
} HxArm64Handle;

hx_err
hx_open (hx_arch arch, hx_mode mode, hx_csh * handle)
{
  HxArm64Handle * h;

  (void) mode;

  if (arch != HX_ARCH_AARCH64)
    return HX_ERR_ARCH;

  h = calloc (1, sizeof (HxArm64Handle));
  if (h == NULL)
    return HX_ERR_MEM;

  h->detail = false;
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
  HxArm64Handle * h = (HxArm64Handle *) (size_t) handle;

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

bool
hx_disasm_iter (hx_csh handle, const uint8_t ** code, size_t * size,
    uint64_t * address, hx_insn * insn)
{
  (void) handle;

  if (*size < 4)
    return false;

  hx_decode_arm64 (*code, *address, insn);

  *code += 4;
  *size -= 4;
  *address += 4;

  return true;
}

size_t
hx_disasm (hx_csh handle, const uint8_t * code, size_t code_size,
    uint64_t address, size_t count, hx_insn ** insn)
{
  size_t cap = (count != 0) ? count : 64;
  size_t n = 0;
  hx_insn * arr;
  const uint8_t * p = code;
  size_t remaining = code_size;
  uint64_t addr = address;

  (void) handle;

  arr = calloc (cap, sizeof (hx_insn));
  if (arr == NULL)
    return 0;

  while (remaining >= 4 && (count == 0 || n < count))
  {
    arr[n].detail = calloc (1, sizeof (hx_detail));
    if (arr[n].detail == NULL)
      break;
    hx_decode_arm64 (p, addr, &arr[n]);
    p += 4;
    remaining -= 4;
    addr += 4;
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

bool
hx_reg_read (hx_csh handle, const hx_insn * insn, unsigned int reg_id)
{
  (void) handle;
  (void) insn;
  (void) reg_id;
  return false;
}

bool
hx_reg_write (hx_csh handle, const hx_insn * insn, unsigned int reg_id)
{
  (void) handle;
  (void) insn;
  (void) reg_id;
  return false;
}

void
hx_arch_register_arm64 (void)
{
}
