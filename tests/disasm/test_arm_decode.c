/*
 * Golden-vector test for the in-tree 32-bit ARM decoder (hx_disasm_arm.c).
 *
 * The decoder is architecture-neutral C, so this runs on any host — it is what
 * lets us validate A32 / Thumb instruction decoding without ARM hardware. It
 * feeds hand-verified encodings and checks exactly the fields the frida-gum
 * relocator/reader/interceptor consume: instruction id, condition code, and the
 * operand list (registers, PC-relative targets as absolute addresses, and
 * literal memory operands).
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

static const uint64_t BASE = 0x1000;

/* Feed one A32 word (4 bytes, little-endian). */
static void
decode_arm (uint32_t word, hx_csh h, hx_insn * insn)
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
    printf ("FAIL: hx_disasm_iter returned false for A32 0x%08x\n", word);
  }
}

/*
 * Feed a Thumb instruction: two halfwords in memory order (h1 is only
 * consumed for 32-bit encodings). Four bytes are always provided; the
 * decoder decides whether the instruction is 16 or 32 bits wide.
 */
static void
decode_thumb (uint32_t h0, uint32_t h1, hx_csh h, hx_insn * insn)
{
  uint8_t bytes[4];
  const uint8_t * code = bytes;
  size_t size = 4;
  uint64_t addr = BASE;

  bytes[0] = (uint8_t) (h0);
  bytes[1] = (uint8_t) (h0 >> 8);
  bytes[2] = (uint8_t) (h1);
  bytes[3] = (uint8_t) (h1 >> 8);

  if (!hx_disasm_iter (h, &code, &size, &addr, insn))
  {
    failures++;
    printf ("FAIL: hx_disasm_iter returned false for T 0x%04x 0x%04x\n",
        h0, h1);
  }
}

static int
has_reg_operand (const hx_arm * d, hx_arm_reg reg)
{
  uint8_t i;
  for (i = 0; i < d->op_count; i++)
  {
    if (d->operands[i].type == HX_ARM_OP_REG && d->operands[i].reg == reg)
      return 1;
  }
  return 0;
}

int
main (void)
{
  hx_csh ha;   /* ARM (A32)  */
  hx_csh ht;   /* Thumb      */
  hx_insn * insn;
  const hx_arm * d;

  hx_arch_register_arm ();

  if (hx_open (HX_ARCH_ARM, HX_MODE_ARM, &ha) != HX_ERR_OK)
  {
    printf ("FAIL: hx_open ARM\n");
    return 1;
  }
  if (hx_open (HX_ARCH_ARM, HX_MODE_THUMB, &ht) != HX_ERR_OK)
  {
    printf ("FAIL: hx_open THUMB\n");
    return 1;
  }
  hx_option (ha, HX_OPT_DETAIL, HX_OPT_ON);
  hx_option (ht, HX_OPT_DETAIL, HX_OPT_ON);
  insn = hx_insn_alloc (ha);
  d = &insn->detail->arm;

  /* --- A32 -------------------------------------------------------------- */

  /* b .+8   (0xEA000000, pc = addr+8, offset 0 -> BASE + 8) */
  decode_arm (0xEA000000, ha, insn);
  CHECK (insn->id == HX_ARM_INS_B, "arm b: id");
  CHECK (d->cc == HX_ARM_CC_AL, "arm b: cc AL");
  CHECK (d->operands[0].type == HX_ARM_OP_IMM &&
      (uint64_t) d->operands[0].imm == BASE + 8, "arm b: target");

  /* bl .   (0xEB000001 -> BASE + 8 + (1<<2) = BASE + 0xC) */
  decode_arm (0xEB000001, ha, insn);
  CHECK (insn->id == HX_ARM_INS_BL, "arm bl: id");
  CHECK ((uint64_t) d->operands[0].imm == BASE + 0xC, "arm bl: target");

  /* b.eq .  (0x0A000002 -> BASE + 8 + (2<<2) = BASE + 0x10) */
  decode_arm (0x0A000002, ha, insn);
  CHECK (insn->id == HX_ARM_INS_B, "arm b.eq: id");
  CHECK (d->cc == HX_ARM_CC_EQ, "arm b.eq: cc");
  CHECK ((uint64_t) d->operands[0].imm == BASE + 0x10, "arm b.eq: target");

  /* bx lr   (0xE12FFF1E) */
  decode_arm (0xE12FFF1E, ha, insn);
  CHECK (insn->id == HX_ARM_INS_BX, "arm bx: id");
  CHECK (d->operands[0].type == HX_ARM_OP_REG &&
      d->operands[0].reg == HX_ARM_REG_LR, "arm bx: reg lr");

  /* ldr r0, [pc, #4]   (0xE59F0004) */
  decode_arm (0xE59F0004, ha, insn);
  CHECK (insn->id == HX_ARM_INS_LDR, "arm ldr: id");
  CHECK (d->operands[0].reg == HX_ARM_REG_R0, "arm ldr: rt");
  CHECK (d->operands[1].type == HX_ARM_OP_MEM &&
      d->operands[1].mem.base == HX_ARM_REG_PC &&
      d->operands[1].mem.index == HX_ARM_REG_INVALID &&
      d->operands[1].mem.disp == 4, "arm ldr: [pc,#4]");
  CHECK (!d->operands[1].subtracted, "arm ldr: add (U=1)");
  CHECK (!d->writeback, "arm ldr: no writeback");

  /* ldr pc, [pc, #-4]  (0xE51FF004, dest PC, U=0 -> subtracted) */
  decode_arm (0xE51FF004, ha, insn);
  CHECK (insn->id == HX_ARM_INS_LDR, "arm ldr-pc: id");
  CHECK (d->operands[0].reg == HX_ARM_REG_PC, "arm ldr-pc: rt == pc");
  CHECK (d->operands[1].mem.base == HX_ARM_REG_PC &&
      d->operands[1].mem.disp == 4 &&
      d->operands[1].subtracted, "arm ldr-pc: [pc,#-4]");

  /* add r0, pc, #4  (0xE28F0004) */
  decode_arm (0xE28F0004, ha, insn);
  CHECK (insn->id == HX_ARM_INS_ADD, "arm add: id");
  CHECK (d->op_count == 3, "arm add: op_count");
  CHECK (d->operands[0].reg == HX_ARM_REG_R0, "arm add: rd");
  CHECK (d->operands[1].type == HX_ARM_OP_REG &&
      d->operands[1].reg == HX_ARM_REG_PC, "arm add: rn == pc");
  CHECK (d->operands[2].type == HX_ARM_OP_IMM &&
      d->operands[2].imm == 4, "arm add: imm resolved");

  /* mov r1, pc  (0xE1A0100F) */
  decode_arm (0xE1A0100F, ha, insn);
  CHECK (insn->id == HX_ARM_INS_MOV, "arm mov: id");
  CHECK (d->op_count == 2, "arm mov: op_count");
  CHECK (d->operands[0].reg == HX_ARM_REG_R1, "arm mov: rd");
  CHECK (d->operands[1].type == HX_ARM_OP_REG &&
      d->operands[1].reg == HX_ARM_REG_PC, "arm mov: rm == pc");

  /* mov r2, #imm  (0xE3A02405: modified imm = ror32(0x05, 2*4) = 0x05000000) */
  decode_arm (0xE3A02405, ha, insn);
  CHECK (insn->id == HX_ARM_INS_MOV, "arm mov-imm: id");
  CHECK (d->operands[0].reg == HX_ARM_REG_R2, "arm mov-imm: rd");
  CHECK (d->operands[1].type == HX_ARM_OP_IMM &&
      (uint32_t) d->operands[1].imm == 0x05000000u,
      "arm mov-imm: resolved modified immediate");

  /* pop {r4, pc}  (0xE8BD8010) */
  decode_arm (0xE8BD8010, ha, insn);
  CHECK (insn->id == HX_ARM_INS_POP, "arm pop: id");
  CHECK (d->op_count == 2, "arm pop: op_count");
  CHECK (has_reg_operand (d, HX_ARM_REG_R4), "arm pop: r4");
  CHECK (has_reg_operand (d, HX_ARM_REG_PC), "arm pop: pc");
  CHECK (hx_reg_read (ha, insn, HX_ARM_REG_PC), "arm pop: reg_read pc");

  /* push {r4, lr}  (0xE92D4010) */
  decode_arm (0xE92D4010, ha, insn);
  CHECK (insn->id == HX_ARM_INS_PUSH, "arm push: id");
  CHECK (d->op_count == 0, "arm push: op_count 0 (verbatim)");

  /* sub r0, r1, r2  (0xE0410002, register form) */
  decode_arm (0xE0410002, ha, insn);
  CHECK (insn->id == HX_ARM_INS_SUB, "arm sub: id");
  CHECK (d->operands[0].reg == HX_ARM_REG_R0 &&
      d->operands[1].reg == HX_ARM_REG_R1 &&
      d->operands[2].type == HX_ARM_OP_REG &&
      d->operands[2].reg == HX_ARM_REG_R2, "arm sub: regs");

  /* svc #0  (0xEF000000) */
  decode_arm (0xEF000000, ha, insn);
  CHECK (insn->id == HX_ARM_INS_SVC, "arm svc: id");

  /* nop-ish / unhandled: mul r0,r1,r2 (0xE0000291) -> verbatim */
  decode_arm (0xE0000291, ha, insn);
  CHECK (insn->id == HX_ARM_INS_INVALID, "arm mul: verbatim");
  CHECK (d->op_count == 0, "arm mul: op_count 0");

  /* vldr s0, [pc] must be rejected instead of copied at a new PC. */
  decode_arm (0xED9F0A00, ha, insn);
  CHECK (insn->id == HX_ARM_INS_UNSUPPORTED_PC_RELATIVE,
      "arm vldr literal: unsupported PC-relative");

  /* --- Thumb (same insn allocation; the arm detail layout is shared) ---- */

  /* cbz r3, .  (0xB11B: i=0, imm5=3 -> imm 6, target = addr+4+6) */
  decode_thumb (0xB11B, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_CBZ, "t cbz: id");
  CHECK (insn->size == 2, "t cbz: size 2");
  CHECK (d->operands[0].reg == HX_ARM_REG_R3, "t cbz: rn");
  CHECK (d->operands[1].type == HX_ARM_OP_IMM &&
      (uint64_t) d->operands[1].imm == BASE + 4 + 6, "t cbz: target");

  /* b.n .  (0xE002: imm11=2 -> off 4, target = addr+4+4) */
  decode_thumb (0xE002, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_B, "t b.n: id");
  CHECK (d->cc == HX_ARM_CC_INVALID, "t b.n: uncond");
  CHECK ((uint64_t) d->operands[0].imm == BASE + 4 + 4, "t b.n: target");

  /* beq.n .  (0xD002: cond=EQ, imm8=2 -> off 4) */
  decode_thumb (0xD002, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_B, "t beq.n: id");
  CHECK (d->cc == HX_ARM_CC_EQ, "t beq.n: cc");
  CHECK ((uint64_t) d->operands[0].imm == BASE + 4 + 4, "t beq.n: target");

  /* bx r0  (0x4700) */
  decode_thumb (0x4700, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_BX, "t bx: id");
  CHECK (d->operands[0].reg == HX_ARM_REG_R0, "t bx: rm");

  /* ldr r2, [pc, #20]  (0x4A05) */
  decode_thumb (0x4A05, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_LDR, "t ldr-lit: id");
  CHECK (d->operands[0].reg == HX_ARM_REG_R2, "t ldr-lit: rt");
  CHECK (d->operands[1].type == HX_ARM_OP_MEM &&
      d->operands[1].mem.base == HX_ARM_REG_PC &&
      d->operands[1].mem.disp == 20, "t ldr-lit: [pc,#20]");

  /* adr r1, #12  (0xA103) */
  decode_thumb (0xA103, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_ADR, "t adr: id");
  CHECK (d->operands[0].reg == HX_ARM_REG_R1, "t adr: rd");
  CHECK (d->operands[1].type == HX_ARM_OP_IMM &&
      d->operands[1].imm == 12, "t adr: imm");

  /* add r0, pc  (0x4478) */
  decode_thumb (0x4478, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_ADD, "t add-pc: id");
  CHECK (d->op_count == 2, "t add-pc: op_count 2");
  CHECK (d->operands[0].reg == HX_ARM_REG_R0, "t add-pc: rdn");
  CHECK (d->operands[1].type == HX_ARM_OP_REG &&
      d->operands[1].reg == HX_ARM_REG_PC, "t add-pc: rm == pc");

  /* mov r8, r1  (0x4688: D=1 -> Rd=8, Rm=1) */
  decode_thumb (0x4688, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_MOV, "t mov-hi: id");
  CHECK (d->operands[0].reg == HX_ARM_REG_R8 &&
      d->operands[1].reg == HX_ARM_REG_R1, "t mov-hi: regs");

  /* pop {r4, pc}  (0xBD10) */
  decode_thumb (0xBD10, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_POP, "t pop: id");
  CHECK (d->op_count == 2, "t pop: op_count");
  CHECK (has_reg_operand (d, HX_ARM_REG_R4), "t pop: r4");
  CHECK (has_reg_operand (d, HX_ARM_REG_PC), "t pop: pc");
  CHECK (hx_reg_write (ht, insn, HX_ARM_REG_PC), "t pop: reg_write pc");

  /* push {lr}  (0xB500) */
  decode_thumb (0xB500, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_PUSH, "t push: id");
  CHECK (d->op_count == 0, "t push: op_count 0");

  /* it eq  (0xBF08) */
  decode_thumb (0xBF08, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_IT, "t it: id");
  CHECK (d->cc == HX_ARM_CC_EQ, "t it: cc");
  CHECK (insn->size == 2, "t it: size 2");

  /* nop  (0xBF00: IT mask 0 -> verbatim) */
  decode_thumb (0xBF00, 0, ht, insn);
  CHECK (insn->id == HX_ARM_INS_INVALID, "t nop: verbatim");

  /* bl  (0xF000 0xF880 -> BASE + 4 + 256 = BASE + 0x104) */
  decode_thumb (0xF000, 0xF880, ht, insn);
  CHECK (insn->id == HX_ARM_INS_BL, "t bl: id");
  CHECK (insn->size == 4, "t bl: size 4");
  CHECK ((uint64_t) d->operands[0].imm == BASE + 4 + 256, "t bl: target");

  /* b.w  (0xF000 0xB900 -> T4 uncond, BASE + 4 + 512 = BASE + 0x204) */
  decode_thumb (0xF000, 0xB900, ht, insn);
  CHECK (insn->id == HX_ARM_INS_B, "t b.w: id");
  CHECK (d->cc == HX_ARM_CC_INVALID, "t b.w: uncond");
  CHECK (insn->size == 4, "t b.w: size 4");
  CHECK ((uint64_t) d->operands[0].imm == BASE + 4 + 512, "t b.w: target");

  /* b.w.eq  (0xF000 0x8000 -> T3 cond EQ, offset 0, target = BASE + 4) */
  decode_thumb (0xF000, 0x8000, ht, insn);
  CHECK (insn->id == HX_ARM_INS_B, "t b.w.eq: id");
  CHECK (d->cc == HX_ARM_CC_EQ, "t b.w.eq: cc");
  CHECK ((uint64_t) d->operands[0].imm == BASE + 4, "t b.w.eq: target");

  /* ldrsb.w r4, [pc, #0x33f] must not be copied verbatim. */
  decode_thumb (0xF99F, 0x433F, ht, insn);
  CHECK (insn->id == HX_ARM_INS_UNSUPPORTED_PC_RELATIVE,
      "t ldrsb literal: unsupported PC-relative");

  hx_insn_free (insn, 1);
  hx_close (&ha);
  hx_close (&ht);

  printf ("%d checks, %d failures\n", checks, failures);
  return failures != 0;
}
