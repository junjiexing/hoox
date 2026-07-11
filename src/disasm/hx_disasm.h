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
  HX_ARCH_X86 = 3,
} hx_arch;

typedef enum hx_mode
{
  HX_MODE_32 = 1 << 2,
  HX_MODE_64 = 1 << 3,
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

typedef struct hx_detail
{
  uint16_t regs_read[16];
  uint8_t regs_read_count;
  uint16_t regs_write[20];
  uint8_t regs_write_count;

  union
  {
    hx_x86 x86;
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

#ifdef __cplusplus
}
#endif

#endif
