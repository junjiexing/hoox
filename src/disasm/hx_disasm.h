/*
 * hoox — capstone-compatible shim.
 *
 * hoox does not depend on capstone. This header provides the *subset* of
 * capstone's public x86 API that the extracted frida-gum hook engine uses,
 * so those sources compile with their `#include <hx_disasm.h>` unchanged. It
 * is backed by a compact in-tree decoder (hx_disasm_x86.c) informed by
 * Microsoft Detours' relocation engine.
 *
 * Enum values need not match upstream capstone: the extracted code only ever
 * compares against these symbols (e.g. switch on HX_INS_JMP), so any set of
 * distinct values that the decoder and the consumers agree on is correct.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 * Portions derive from Microsoft Detours (MIT); see NOTICE.
 */

#ifndef __HOOX_DISASM_H__
#define __HOOX_DISASM_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t hx_csh;

typedef enum hx_arch
{
  HX_ARCH_ARM = 0,
  HX_ARCH_AARCH64 = 1,
  HX_ARCH_X86 = 3,
} hx_arch;

/* Upstream spells the arm64 arch id HX_ARCH_ARM64; keep that alias regardless
 * of the capstone API-version gate in hooxdefs.h. */
#define HX_ARCH_ARM64 HX_ARCH_AARCH64

typedef enum hx_mode
{
  HX_MODE_LITTLE_ENDIAN = 0,
  HX_MODE_ARM = 0,
  HX_MODE_32 = 1 << 2,
  HX_MODE_64 = 1 << 3,
  HX_MODE_THUMB = 1 << 4,
  HX_MODE_V8 = 1 << 6,
  HX_MODE_BIG_ENDIAN = 1 << 31,
} hx_mode;

typedef enum hx_err
{
  HX_ERR_OK = 0,
  HX_ERR_MEM,
  HX_ERR_ARCH,
  HX_ERR_HANDLE,
} hx_err;

typedef enum hx_opt_type
{
  HX_OPT_DETAIL = 2,
  HX_OPT_MODE = 1,
  HX_OPT_MEM = 3,
} hx_opt_type;

typedef enum hx_opt_value
{
  HX_OPT_OFF = 0,
  HX_OPT_ON = 3,
} hx_opt_value;

/* ---- x86 registers ------------------------------------------------------ */

typedef enum hx_x86_reg
{
  HX_REG_INVALID = 0,

  /* 64-bit GP */
  HX_REG_RAX, HX_REG_RCX, HX_REG_RDX, HX_REG_RBX,
  HX_REG_RSP, HX_REG_RBP, HX_REG_RSI, HX_REG_RDI,
  HX_REG_R8, HX_REG_R9, HX_REG_R10, HX_REG_R11,
  HX_REG_R12, HX_REG_R13, HX_REG_R14, HX_REG_R15,

  /* 32-bit GP */
  HX_REG_EAX, HX_REG_ECX, HX_REG_EDX, HX_REG_EBX,
  HX_REG_ESP, HX_REG_EBP, HX_REG_ESI, HX_REG_EDI,
  HX_REG_R8D, HX_REG_R9D, HX_REG_R10D, HX_REG_R11D,
  HX_REG_R12D, HX_REG_R13D, HX_REG_R14D, HX_REG_R15D,

  /* instruction pointer */
  HX_REG_EIP, HX_REG_RIP,

  /* segment */
  HX_REG_CS, HX_REG_DS, HX_REG_ES, HX_REG_FS, HX_REG_GS, HX_REG_SS,

  HX_REG_ENDING
} hx_x86_reg;

/* ---- x86 instruction ids (only those the hook engine references) -------- */

typedef enum hx_x86_insn
{
  HX_INS_INVALID = 0,

  HX_INS_CALL,
  HX_INS_JMP,
  HX_INS_RET,
  HX_INS_RETF,

  HX_INS_JCXZ, HX_INS_JECXZ, HX_INS_JRCXZ,
  HX_INS_JO, HX_INS_JNO, HX_INS_JB, HX_INS_JAE,
  HX_INS_JE, HX_INS_JNE, HX_INS_JBE, HX_INS_JA,
  HX_INS_JS, HX_INS_JNS, HX_INS_JP, HX_INS_JNP,
  HX_INS_JL, HX_INS_JGE, HX_INS_JLE, HX_INS_JG,
  HX_INS_LOOP, HX_INS_LOOPE, HX_INS_LOOPNE,

  HX_INS_CMPXCHG,
  HX_INS_PUSH, HX_INS_POP,
  HX_INS_INT, HX_INS_INT3,
  HX_INS_SYSCALL, HX_INS_SYSENTER,
  HX_INS_NOP,

  /* common data-processing ids some relocator/reader/test paths switch on */
  HX_INS_MOV, HX_INS_LEA,
  HX_INS_ADD, HX_INS_OR, HX_INS_ADC, HX_INS_SBB,
  HX_INS_AND, HX_INS_SUB, HX_INS_XOR, HX_INS_CMP,
  HX_INS_TEST, HX_INS_INC, HX_INS_DEC,

  HX_INS_OTHER,   /* any instruction not individually classified */

  HX_INS_ENDING
} hx_x86_insn;

/* ---- operands ----------------------------------------------------------- */

typedef enum hx_x86_op_type
{
  HX_OP_INVALID = 0,
  HX_OP_REG,
  HX_OP_IMM,
  HX_OP_MEM,
} hx_x86_op_type;

typedef struct hx_x86_op_mem
{
  hx_x86_reg segment;
  hx_x86_reg base;
  hx_x86_reg index;
  int scale;
  int64_t disp;
} hx_x86_op_mem;

typedef struct hx_x86_op
{
  hx_x86_op_type type;
  union
  {
    hx_x86_reg reg;
    int64_t imm;
    hx_x86_op_mem mem;
  };
  uint8_t size;
} hx_x86_op;

typedef struct hx_x86_encoding
{
  uint8_t modrm_offset;
  uint8_t disp_offset;
  uint8_t disp_size;
  uint8_t imm_offset;
  uint8_t imm_size;
} hx_x86_encoding;

#define HX_HX_MAX_OPS 4

typedef struct hx_x86
{
  uint8_t prefix[4];
  uint8_t opcode[4];
  uint8_t rex;
  uint8_t addr_size;
  uint8_t modrm;
  uint8_t sib;
  int64_t disp;
  hx_x86_encoding encoding;

  uint8_t op_count;
  hx_x86_op operands[HX_HX_MAX_OPS];
} hx_x86;

/* ---- arm64 (AArch64) registers ------------------------------------------ */

/*
 * Layout is chosen so the arm64 writer's arithmetic holds: X0..X30 contiguous,
 * and W/S/D/Q each a contiguous block indexable as (reg - Wn/Sn/Dn/Qn). It
 * need not match upstream capstone — only the in-tree decoder and consumers.
 */
typedef enum hx_arm64_reg
{
  HX_ARM64_REG_INVALID = 0,

  /* 64-bit GP: X0..X30 (must stay contiguous) */
  HX_ARM64_REG_X0, HX_ARM64_REG_X1, HX_ARM64_REG_X2, HX_ARM64_REG_X3,
  HX_ARM64_REG_X4, HX_ARM64_REG_X5, HX_ARM64_REG_X6, HX_ARM64_REG_X7,
  HX_ARM64_REG_X8, HX_ARM64_REG_X9, HX_ARM64_REG_X10, HX_ARM64_REG_X11,
  HX_ARM64_REG_X12, HX_ARM64_REG_X13, HX_ARM64_REG_X14, HX_ARM64_REG_X15,
  HX_ARM64_REG_X16, HX_ARM64_REG_X17, HX_ARM64_REG_X18, HX_ARM64_REG_X19,
  HX_ARM64_REG_X20, HX_ARM64_REG_X21, HX_ARM64_REG_X22, HX_ARM64_REG_X23,
  HX_ARM64_REG_X24, HX_ARM64_REG_X25, HX_ARM64_REG_X26, HX_ARM64_REG_X27,
  HX_ARM64_REG_X28, HX_ARM64_REG_X29, HX_ARM64_REG_X30,

  HX_ARM64_REG_SP,
  HX_ARM64_REG_XZR,

  /* 32-bit GP: W0..W30 (contiguous) */
  HX_ARM64_REG_W0, HX_ARM64_REG_W1, HX_ARM64_REG_W2, HX_ARM64_REG_W3,
  HX_ARM64_REG_W4, HX_ARM64_REG_W5, HX_ARM64_REG_W6, HX_ARM64_REG_W7,
  HX_ARM64_REG_W8, HX_ARM64_REG_W9, HX_ARM64_REG_W10, HX_ARM64_REG_W11,
  HX_ARM64_REG_W12, HX_ARM64_REG_W13, HX_ARM64_REG_W14, HX_ARM64_REG_W15,
  HX_ARM64_REG_W16, HX_ARM64_REG_W17, HX_ARM64_REG_W18, HX_ARM64_REG_W19,
  HX_ARM64_REG_W20, HX_ARM64_REG_W21, HX_ARM64_REG_W22, HX_ARM64_REG_W23,
  HX_ARM64_REG_W24, HX_ARM64_REG_W25, HX_ARM64_REG_W26, HX_ARM64_REG_W27,
  HX_ARM64_REG_W28, HX_ARM64_REG_W29, HX_ARM64_REG_W30,
  HX_ARM64_REG_WZR,

  /* FP/SIMD scalar views: S0..S31, D0..D31, Q0..Q31 (each contiguous) */
  HX_ARM64_REG_S0, HX_ARM64_REG_S1, HX_ARM64_REG_S2, HX_ARM64_REG_S3,
  HX_ARM64_REG_S4, HX_ARM64_REG_S5, HX_ARM64_REG_S6, HX_ARM64_REG_S7,
  HX_ARM64_REG_S8, HX_ARM64_REG_S9, HX_ARM64_REG_S10, HX_ARM64_REG_S11,
  HX_ARM64_REG_S12, HX_ARM64_REG_S13, HX_ARM64_REG_S14, HX_ARM64_REG_S15,
  HX_ARM64_REG_S16, HX_ARM64_REG_S17, HX_ARM64_REG_S18, HX_ARM64_REG_S19,
  HX_ARM64_REG_S20, HX_ARM64_REG_S21, HX_ARM64_REG_S22, HX_ARM64_REG_S23,
  HX_ARM64_REG_S24, HX_ARM64_REG_S25, HX_ARM64_REG_S26, HX_ARM64_REG_S27,
  HX_ARM64_REG_S28, HX_ARM64_REG_S29, HX_ARM64_REG_S30, HX_ARM64_REG_S31,

  HX_ARM64_REG_D0, HX_ARM64_REG_D1, HX_ARM64_REG_D2, HX_ARM64_REG_D3,
  HX_ARM64_REG_D4, HX_ARM64_REG_D5, HX_ARM64_REG_D6, HX_ARM64_REG_D7,
  HX_ARM64_REG_D8, HX_ARM64_REG_D9, HX_ARM64_REG_D10, HX_ARM64_REG_D11,
  HX_ARM64_REG_D12, HX_ARM64_REG_D13, HX_ARM64_REG_D14, HX_ARM64_REG_D15,
  HX_ARM64_REG_D16, HX_ARM64_REG_D17, HX_ARM64_REG_D18, HX_ARM64_REG_D19,
  HX_ARM64_REG_D20, HX_ARM64_REG_D21, HX_ARM64_REG_D22, HX_ARM64_REG_D23,
  HX_ARM64_REG_D24, HX_ARM64_REG_D25, HX_ARM64_REG_D26, HX_ARM64_REG_D27,
  HX_ARM64_REG_D28, HX_ARM64_REG_D29, HX_ARM64_REG_D30, HX_ARM64_REG_D31,

  HX_ARM64_REG_Q0, HX_ARM64_REG_Q1, HX_ARM64_REG_Q2, HX_ARM64_REG_Q3,
  HX_ARM64_REG_Q4, HX_ARM64_REG_Q5, HX_ARM64_REG_Q6, HX_ARM64_REG_Q7,
  HX_ARM64_REG_Q8, HX_ARM64_REG_Q9, HX_ARM64_REG_Q10, HX_ARM64_REG_Q11,
  HX_ARM64_REG_Q12, HX_ARM64_REG_Q13, HX_ARM64_REG_Q14, HX_ARM64_REG_Q15,
  HX_ARM64_REG_Q16, HX_ARM64_REG_Q17, HX_ARM64_REG_Q18, HX_ARM64_REG_Q19,
  HX_ARM64_REG_Q20, HX_ARM64_REG_Q21, HX_ARM64_REG_Q22, HX_ARM64_REG_Q23,
  HX_ARM64_REG_Q24, HX_ARM64_REG_Q25, HX_ARM64_REG_Q26, HX_ARM64_REG_Q27,
  HX_ARM64_REG_Q28, HX_ARM64_REG_Q29, HX_ARM64_REG_Q30, HX_ARM64_REG_Q31,

  HX_ARM64_REG_ENDING,

  /* aliases */
  HX_ARM64_REG_FP = HX_ARM64_REG_X29,
  HX_ARM64_REG_LR = HX_ARM64_REG_X30,
} hx_arm64_reg;

/* ---- arm64 instruction ids (only those the hook engine references) ------- */

typedef enum hx_arm64_insn
{
  HX_ARM64_INS_INVALID = 0,

  HX_ARM64_INS_ADD,
  HX_ARM64_INS_ADR,
  HX_ARM64_INS_ADRP,
  HX_ARM64_INS_B,
  HX_ARM64_INS_BL,
  HX_ARM64_INS_BLR,
  HX_ARM64_INS_BLRAA, HX_ARM64_INS_BLRAAZ,
  HX_ARM64_INS_BLRAB, HX_ARM64_INS_BLRABZ,
  HX_ARM64_INS_BR,
  HX_ARM64_INS_BRAA, HX_ARM64_INS_BRAAZ,
  HX_ARM64_INS_BRAB, HX_ARM64_INS_BRABZ,
  HX_ARM64_INS_CBNZ,
  HX_ARM64_INS_CBZ,
  HX_ARM64_INS_LDR,
  HX_ARM64_INS_LDRSW,
  HX_ARM64_INS_MOV,
  HX_ARM64_INS_RET,
  HX_ARM64_INS_RETAA, HX_ARM64_INS_RETAB,
  HX_ARM64_INS_STP,
  HX_ARM64_INS_SVC,
  HX_ARM64_INS_TBNZ,
  HX_ARM64_INS_TBZ,

  HX_ARM64_INS_ENDING
} hx_arm64_insn;

/* ---- arm64 condition codes (value == encoded field + 1) ----------------- */

typedef enum hx_arm64_cc
{
  HX_ARM64_CC_INVALID = 0,
  HX_ARM64_CC_EQ, HX_ARM64_CC_NE,
  HX_ARM64_CC_HS, HX_ARM64_CC_LO,
  HX_ARM64_CC_MI, HX_ARM64_CC_PL,
  HX_ARM64_CC_VS, HX_ARM64_CC_VC,
  HX_ARM64_CC_HI, HX_ARM64_CC_LS,
  HX_ARM64_CC_GE, HX_ARM64_CC_LT,
  HX_ARM64_CC_GT, HX_ARM64_CC_LE,
  HX_ARM64_CC_AL, HX_ARM64_CC_NV,
} hx_arm64_cc;

/* ---- arm64 operands ----------------------------------------------------- */

typedef enum hx_arm64_op_type
{
  HX_ARM64_OP_INVALID = 0,
  HX_ARM64_OP_REG,
  HX_ARM64_OP_IMM,
  HX_ARM64_OP_MEM,
} hx_arm64_op_type;

typedef struct hx_arm64_op_mem
{
  hx_arm64_reg base;
  hx_arm64_reg index;
  int32_t disp;
} hx_arm64_op_mem;

typedef struct hx_arm64_op
{
  hx_arm64_op_type type;
  union
  {
    hx_arm64_reg reg;
    int64_t imm;
    hx_arm64_op_mem mem;
  };
} hx_arm64_op;

#define HX_ARM64_MAX_OPS 8

typedef struct hx_arm64
{
  hx_arm64_cc cc;
  uint8_t op_count;
  hx_arm64_op operands[HX_ARM64_MAX_OPS];
} hx_arm64;

/* ---- arm (A32 / Thumb) registers ---------------------------------------- */

/*
 * R0..R12 must stay contiguous (the writer's reg helper does reg - R0). SP/LR/PC
 * are matched by equality only, so their values are free. S/D/Q each form a
 * contiguous block (reg - Sn/Dn/Qn). Values need not match upstream capstone.
 */
typedef enum hx_arm_reg
{
  HX_ARM_REG_INVALID = 0,

  HX_ARM_REG_R0, HX_ARM_REG_R1, HX_ARM_REG_R2, HX_ARM_REG_R3,
  HX_ARM_REG_R4, HX_ARM_REG_R5, HX_ARM_REG_R6, HX_ARM_REG_R7,
  HX_ARM_REG_R8, HX_ARM_REG_R9, HX_ARM_REG_R10, HX_ARM_REG_R11,
  HX_ARM_REG_R12,
  HX_ARM_REG_SP, HX_ARM_REG_LR, HX_ARM_REG_PC,

  HX_ARM_REG_S0, HX_ARM_REG_S1, HX_ARM_REG_S2, HX_ARM_REG_S3,
  HX_ARM_REG_S4, HX_ARM_REG_S5, HX_ARM_REG_S6, HX_ARM_REG_S7,
  HX_ARM_REG_S8, HX_ARM_REG_S9, HX_ARM_REG_S10, HX_ARM_REG_S11,
  HX_ARM_REG_S12, HX_ARM_REG_S13, HX_ARM_REG_S14, HX_ARM_REG_S15,
  HX_ARM_REG_S16, HX_ARM_REG_S17, HX_ARM_REG_S18, HX_ARM_REG_S19,
  HX_ARM_REG_S20, HX_ARM_REG_S21, HX_ARM_REG_S22, HX_ARM_REG_S23,
  HX_ARM_REG_S24, HX_ARM_REG_S25, HX_ARM_REG_S26, HX_ARM_REG_S27,
  HX_ARM_REG_S28, HX_ARM_REG_S29, HX_ARM_REG_S30, HX_ARM_REG_S31,

  HX_ARM_REG_D0, HX_ARM_REG_D1, HX_ARM_REG_D2, HX_ARM_REG_D3,
  HX_ARM_REG_D4, HX_ARM_REG_D5, HX_ARM_REG_D6, HX_ARM_REG_D7,
  HX_ARM_REG_D8, HX_ARM_REG_D9, HX_ARM_REG_D10, HX_ARM_REG_D11,
  HX_ARM_REG_D12, HX_ARM_REG_D13, HX_ARM_REG_D14, HX_ARM_REG_D15,
  HX_ARM_REG_D16, HX_ARM_REG_D17, HX_ARM_REG_D18, HX_ARM_REG_D19,
  HX_ARM_REG_D20, HX_ARM_REG_D21, HX_ARM_REG_D22, HX_ARM_REG_D23,
  HX_ARM_REG_D24, HX_ARM_REG_D25, HX_ARM_REG_D26, HX_ARM_REG_D27,
  HX_ARM_REG_D28, HX_ARM_REG_D29, HX_ARM_REG_D30, HX_ARM_REG_D31,

  HX_ARM_REG_Q0, HX_ARM_REG_Q1, HX_ARM_REG_Q2, HX_ARM_REG_Q3,
  HX_ARM_REG_Q4, HX_ARM_REG_Q5, HX_ARM_REG_Q6, HX_ARM_REG_Q7,
  HX_ARM_REG_Q8, HX_ARM_REG_Q9, HX_ARM_REG_Q10, HX_ARM_REG_Q11,
  HX_ARM_REG_Q12, HX_ARM_REG_Q13, HX_ARM_REG_Q14, HX_ARM_REG_Q15,

  HX_ARM_REG_ENDING
} hx_arm_reg;

/* ---- arm instruction ids (only those the hook engine references) --------- */

typedef enum hx_arm_insn
{
  HX_ARM_INS_INVALID = 0,

  HX_ARM_INS_ADD,
  HX_ARM_INS_ADR,
  HX_ARM_INS_B,
  HX_ARM_INS_BL,
  HX_ARM_INS_BLX,
  HX_ARM_INS_BX,
  HX_ARM_INS_CBNZ,
  HX_ARM_INS_CBZ,
  HX_ARM_INS_IT,
  HX_ARM_INS_LDM,
  HX_ARM_INS_LDR,
  HX_ARM_INS_MOV,
  HX_ARM_INS_POP,
  HX_ARM_INS_PUSH,
  HX_ARM_INS_SUB,
  HX_ARM_INS_SVC,
  HX_ARM_INS_TBB,
  HX_ARM_INS_TBH,
  HX_ARM_INS_VLDR,

  HX_ARM_INS_ENDING
} hx_arm_insn;

/* condition codes: value == (encoded 4-bit cond field) + 1 */
typedef enum hx_arm_cc
{
  HX_ARM_CC_INVALID = 0,
  HX_ARM_CC_EQ, HX_ARM_CC_NE,
  HX_ARM_CC_HS, HX_ARM_CC_LO,
  HX_ARM_CC_MI, HX_ARM_CC_PL,
  HX_ARM_CC_VS, HX_ARM_CC_VC,
  HX_ARM_CC_HI, HX_ARM_CC_LS,
  HX_ARM_CC_GE, HX_ARM_CC_LT,
  HX_ARM_CC_GT, HX_ARM_CC_LE,
  HX_ARM_CC_AL,
} hx_arm_cc;

typedef enum hx_arm_shifter
{
  HX_ARM_SFT_INVALID = 0,
  HX_ARM_SFT_LSL,
  HX_ARM_SFT_LSR,
  HX_ARM_SFT_ASR,
  HX_ARM_SFT_ROR,
} hx_arm_shifter;

typedef enum hx_arm_sysreg
{
  HX_ARM_SYSREG_INVALID = 0,
  HX_ARM_SYSREG_APSR_NZCVQ,
} hx_arm_sysreg;

typedef enum hx_arm_op_type
{
  HX_ARM_OP_INVALID = 0,
  HX_ARM_OP_REG,
  HX_ARM_OP_IMM,
  HX_ARM_OP_MEM,
} hx_arm_op_type;

typedef struct hx_arm_op_mem
{
  hx_arm_reg base;
  hx_arm_reg index;
  int32_t disp;
} hx_arm_op_mem;

typedef struct hx_arm_op
{
  hx_arm_op_type type;
  union
  {
    hx_arm_reg reg;
    int64_t imm;
    hx_arm_op_mem mem;
  };
  bool subtracted;
  struct
  {
    hx_arm_shifter type;
    unsigned int value;
  } shift;
} hx_arm_op;

#define HX_ARM_MAX_OPS 20

typedef struct hx_arm
{
  hx_arm_cc cc;
  bool writeback;
  uint8_t op_count;
  hx_arm_op operands[HX_ARM_MAX_OPS];
} hx_arm;

typedef struct hx_detail
{
  uint16_t regs_read[16];
  uint8_t regs_read_count;
  uint16_t regs_write[20];
  uint8_t regs_write_count;

  union
  {
    hx_x86 x86;
    hx_arm64 arm64;
    hx_arm arm;
  };
} hx_detail;

typedef struct hx_insn
{
  unsigned int id;
  uint64_t address;
  uint16_t size;
  uint8_t bytes[24];
  char mnemonic[32];
  char op_str[160];
  hx_detail * detail;
} hx_insn;

typedef uint16_t hx_regs[64];

/* ---- API ---------------------------------------------------------------- */

hx_err hx_open (hx_arch arch, hx_mode mode, hx_csh * handle);
hx_err hx_close (hx_csh * handle);
hx_err hx_option (hx_csh handle, hx_opt_type type, size_t value);

hx_insn * hx_insn_alloc (hx_csh handle);
void hx_insn_free (hx_insn * insn, size_t count);

bool hx_disasm_iter (hx_csh handle, const uint8_t ** code, size_t * size,
    uint64_t * address, hx_insn * insn);
size_t hx_disasm (hx_csh handle, const uint8_t * code, size_t code_size,
    uint64_t address, size_t count, hx_insn ** insn);

bool hx_reg_read (hx_csh handle, const hx_insn * insn, unsigned int reg_id);
bool hx_reg_write (hx_csh handle, const hx_insn * insn, unsigned int reg_id);

void hx_arch_register_x86 (void);
void hx_arch_register_arm64 (void);
void hx_arch_register_arm (void);

#ifdef __cplusplus
}
#endif

#endif
