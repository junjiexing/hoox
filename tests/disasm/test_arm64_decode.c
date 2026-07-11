/*
 * Golden-vector test for the in-tree AArch64 decoder (hx_disasm_arm64.c).
 *
 * The decoder is architecture-neutral C, so this runs on any host — it is what
 * lets us validate arm64 instruction decoding without arm64 hardware. It feeds
 * hand-verified 32-bit encodings and checks exactly the fields the frida-gum
 * relocator/reader/interceptor consume: instruction id, condition code, and the
 * operand list (registers, and PC-relative targets computed as absolute
 * addresses).
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include <hx_disasm.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                       \
  do                                                                           \
  {                                                                            \
    checks++;                                                                  \
    if (!(cond))                                                               \
    {                                                                          \
      failures++;                                                              \
      printf ("FAIL: ");                                                       \
      printf (__VA_ARGS__);                                                    \
      printf ("\n");                                                           \
    }                                                                          \
  }                                                                            \
  while (0)

static const uint64_t BASE = 0x100000;

static void
decode (uint32_t word, hx_csh h, hx_insn * insn)
{
  uint8_t bytes[4];
  const uint8_t * code = bytes;
  size_t size = 4;
  uint64_t addr = BASE;

  bytes[0] = (uint8_t) (word);
  bytes[1] = (uint8_t) (word >> 8);
  bytes[2] = (uint8_t) (word >> 16);
  bytes[3] = (uint8_t) (word >> 24);

  if (!hx_disasm_iter (h, &code, &size, &addr, insn))
  {
    failures++;
    printf ("FAIL: hx_disasm_iter returned false for 0x%08x\n", word);
  }
}

static int
has_reg_operand (const hx_arm64 * d, hx_arm64_reg reg)
{
  uint8_t i;
  for (i = 0; i < d->op_count; i++)
  {
    if (d->operands[i].type == HX_ARM64_OP_REG && d->operands[i].reg == reg)
      return 1;
  }
  return 0;
}

int
main (void)
{
  hx_csh h;
  hx_insn * insn;
  const hx_arm64 * d;

  hx_arch_register_arm64 ();
  if (hx_open (HX_ARCH_AARCH64, HX_MODE_LITTLE_ENDIAN, &h) != HX_ERR_OK)
  {
    printf ("FAIL: hx_open\n");
    return 1;
  }
  hx_option (h, HX_OPT_DETAIL, HX_OPT_ON);
  insn = hx_insn_alloc (h);
  d = &insn->detail->arm64;

  /* b .+8 */
  decode (0x14000002, h, insn);
  CHECK (insn->id == HX_ARM64_INS_B, "b: id");
  CHECK (d->cc == HX_ARM64_CC_INVALID, "b: unconditional");
  CHECK (d->operands[0].type == HX_ARM64_OP_IMM &&
      (uint64_t) d->operands[0].imm == BASE + 8, "b: target");

  /* bl .+0x10 */
  decode (0x94000004, h, insn);
  CHECK (insn->id == HX_ARM64_INS_BL, "bl: id");
  CHECK ((uint64_t) d->operands[0].imm == BASE + 0x10, "bl: target");

  /* b.eq .+8 */
  decode (0x54000040, h, insn);
  CHECK (insn->id == HX_ARM64_INS_B, "b.eq: id");
  CHECK (d->cc == HX_ARM64_CC_EQ, "b.eq: cc");
  CHECK ((uint64_t) d->operands[0].imm == BASE + 8, "b.eq: target");

  /* cbz x0, .+8 */
  decode (0xb4000040, h, insn);
  CHECK (insn->id == HX_ARM64_INS_CBZ, "cbz: id");
  CHECK (d->operands[0].reg == HX_ARM64_REG_X0, "cbz: reg");
  CHECK ((uint64_t) d->operands[1].imm == BASE + 8, "cbz: target");

  /* cbnz w1, .+8 */
  decode (0x35000041, h, insn);
  CHECK (insn->id == HX_ARM64_INS_CBNZ, "cbnz: id");
  CHECK (d->operands[0].reg == HX_ARM64_REG_W1, "cbnz: reg");

  /* tbz x0, #40, .+8  (bit >= 32 -> X reg) */
  decode (0xb6400040, h, insn);
  CHECK (insn->id == HX_ARM64_INS_TBZ, "tbz: id");
  CHECK (d->operands[0].reg == HX_ARM64_REG_X0, "tbz: reg");
  CHECK (d->operands[1].imm == 40, "tbz: bit");
  CHECK ((uint64_t) d->operands[2].imm == BASE + 8, "tbz: target");

  /* adr x0, .+8 */
  decode (0x10000040, h, insn);
  CHECK (insn->id == HX_ARM64_INS_ADR, "adr: id");
  CHECK (d->operands[0].reg == HX_ARM64_REG_X0, "adr: reg");
  CHECK ((uint64_t) d->operands[1].imm == BASE + 8, "adr: target");

  /* adrp x0, . */
  decode (0x90000000, h, insn);
  CHECK (insn->id == HX_ARM64_INS_ADRP, "adrp: id");
  CHECK ((uint64_t) d->operands[1].imm == (BASE & ~(uint64_t) 0xfff),
      "adrp: target page");

  /* ldr x1, .+8  (literal, 64-bit) */
  decode (0x58000041, h, insn);
  CHECK (insn->id == HX_ARM64_INS_LDR, "ldr-lit: id");
  CHECK (d->operands[0].reg == HX_ARM64_REG_X1, "ldr-lit: reg");
  CHECK (d->operands[1].type == HX_ARM64_OP_IMM &&
      (uint64_t) d->operands[1].imm == BASE + 8, "ldr-lit: literal addr");

  /* ldrsw x2, .+8  (literal) */
  decode (0x98000042, h, insn);
  CHECK (insn->id == HX_ARM64_INS_LDRSW, "ldrsw-lit: id");
  CHECK (d->operands[0].reg == HX_ARM64_REG_X2, "ldrsw-lit: reg");

  /* br x16 */
  decode (0xd61f0200, h, insn);
  CHECK (insn->id == HX_ARM64_INS_BR, "br: id");
  CHECK (d->operands[0].reg == HX_ARM64_REG_X16, "br: reg");

  /* blr x17 */
  decode (0xd63f0220, h, insn);
  CHECK (insn->id == HX_ARM64_INS_BLR, "blr: id");
  CHECK (d->operands[0].reg == HX_ARM64_REG_X17, "blr: reg");

  /* ret (x30) */
  decode (0xd65f03c0, h, insn);
  CHECK (insn->id == HX_ARM64_INS_RET, "ret: id");

  /* svc #0 */
  decode (0xd4000001, h, insn);
  CHECK (insn->id == HX_ARM64_INS_SVC, "svc: id");

  /* mov x0, x1  (ORR alias) */
  decode (0xaa0103e0, h, insn);
  CHECK (insn->id == HX_ARM64_INS_MOV, "mov: id");
  CHECK (d->operands[0].reg == HX_ARM64_REG_X0 &&
      d->operands[1].reg == HX_ARM64_REG_X1, "mov: regs");

  /* stp x0, x30, [sp, #-16]!  */
  decode (0xa9bf7be0, h, insn);
  CHECK (insn->id == HX_ARM64_INS_STP, "stp: id");
  CHECK (d->operands[0].reg == HX_ARM64_REG_X0, "stp: rt");
  CHECK (d->operands[1].reg == HX_ARM64_REG_LR, "stp: rt2 == lr");

  /* add x16, x1, x2  -> unclassified, but X16 must be reported busy */
  decode (0x8b020030, h, insn);
  CHECK (insn->id == HX_ARM64_INS_INVALID, "add: verbatim (invalid id)");
  CHECK (has_reg_operand (d, HX_ARM64_REG_X16), "add: reports X16 busy");
  CHECK (has_reg_operand (d, HX_ARM64_REG_X1), "add: reports X1 busy");
  CHECK (has_reg_operand (d, HX_ARM64_REG_X2), "add: reports X2 busy");

  /* mov x16, x0  -> reported so scratch selection avoids X16 */
  decode (0xaa0003f0, h, insn);
  CHECK (insn->id == HX_ARM64_INS_MOV, "mov x16: id");
  CHECK (has_reg_operand (d, HX_ARM64_REG_X16), "mov x16: reports X16");

  hx_insn_free (insn, 1);
  hx_close (&h);

  printf ("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
