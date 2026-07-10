/*
 * hoox — capstone-compatible shim.
 *
 * hoox does not depend on capstone. This header provides the *subset* of
 * capstone's public x86 API that the extracted frida-gum hook engine uses,
 * so those sources compile with their `#include <capstone.h>` unchanged. It
 * is backed by a compact in-tree decoder (hx_disasm_x86.c) informed by
 * Microsoft Detours' relocation engine.
 *
 * Enum values need not match upstream capstone: the extracted code only ever
 * compares against these symbols (e.g. switch on X86_INS_JMP), so any set of
 * distinct values that the decoder and the consumers agree on is correct.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 * Portions derive from Microsoft Detours (MIT); see NOTICE.
 */

#ifndef __HOOX_CAPSTONE_SHIM_H__
#define __HOOX_CAPSTONE_SHIM_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t csh;

typedef enum cs_arch
{
  CS_ARCH_X86 = 3,
} cs_arch;

typedef enum cs_mode
{
  CS_MODE_32 = 1 << 2,
  CS_MODE_64 = 1 << 3,
} cs_mode;

typedef enum cs_err
{
  CS_ERR_OK = 0,
  CS_ERR_MEM,
  CS_ERR_ARCH,
  CS_ERR_HANDLE,
} cs_err;

typedef enum cs_opt_type
{
  CS_OPT_DETAIL = 2,
  CS_OPT_MODE = 1,
  CS_OPT_MEM = 3,
} cs_opt_type;

typedef enum cs_opt_value
{
  CS_OPT_OFF = 0,
  CS_OPT_ON = 3,
} cs_opt_value;

/* ---- x86 registers ------------------------------------------------------ */

typedef enum x86_reg
{
  X86_REG_INVALID = 0,

  /* 64-bit GP */
  X86_REG_RAX, X86_REG_RCX, X86_REG_RDX, X86_REG_RBX,
  X86_REG_RSP, X86_REG_RBP, X86_REG_RSI, X86_REG_RDI,
  X86_REG_R8, X86_REG_R9, X86_REG_R10, X86_REG_R11,
  X86_REG_R12, X86_REG_R13, X86_REG_R14, X86_REG_R15,

  /* 32-bit GP */
  X86_REG_EAX, X86_REG_ECX, X86_REG_EDX, X86_REG_EBX,
  X86_REG_ESP, X86_REG_EBP, X86_REG_ESI, X86_REG_EDI,
  X86_REG_R8D, X86_REG_R9D, X86_REG_R10D, X86_REG_R11D,
  X86_REG_R12D, X86_REG_R13D, X86_REG_R14D, X86_REG_R15D,

  /* instruction pointer */
  X86_REG_EIP, X86_REG_RIP,

  /* segment */
  X86_REG_CS, X86_REG_DS, X86_REG_ES, X86_REG_FS, X86_REG_GS, X86_REG_SS,

  X86_REG_ENDING
} x86_reg;

/* ---- x86 instruction ids (only those the hook engine references) -------- */

typedef enum x86_insn
{
  X86_INS_INVALID = 0,

  X86_INS_CALL,
  X86_INS_JMP,
  X86_INS_RET,
  X86_INS_RETF,

  X86_INS_JCXZ, X86_INS_JECXZ, X86_INS_JRCXZ,
  X86_INS_JO, X86_INS_JNO, X86_INS_JB, X86_INS_JAE,
  X86_INS_JE, X86_INS_JNE, X86_INS_JBE, X86_INS_JA,
  X86_INS_JS, X86_INS_JNS, X86_INS_JP, X86_INS_JNP,
  X86_INS_JL, X86_INS_JGE, X86_INS_JLE, X86_INS_JG,
  X86_INS_LOOP, X86_INS_LOOPE, X86_INS_LOOPNE,

  X86_INS_CMPXCHG,
  X86_INS_PUSH,
  X86_INS_INT, X86_INS_INT3,
  X86_INS_SYSCALL, X86_INS_SYSENTER,
  X86_INS_NOP,

  X86_INS_OTHER,   /* any instruction not individually classified */

  X86_INS_ENDING
} x86_insn;

/* ---- operands ----------------------------------------------------------- */

typedef enum x86_op_type
{
  X86_OP_INVALID = 0,
  X86_OP_REG,
  X86_OP_IMM,
  X86_OP_MEM,
} x86_op_type;

typedef struct x86_op_mem
{
  x86_reg segment;
  x86_reg base;
  x86_reg index;
  int scale;
  int64_t disp;
} x86_op_mem;

typedef struct cs_x86_op
{
  x86_op_type type;
  union
  {
    x86_reg reg;
    int64_t imm;
    x86_op_mem mem;
  };
  uint8_t size;
} cs_x86_op;

typedef struct cs_x86_encoding
{
  uint8_t modrm_offset;
  uint8_t disp_offset;
  uint8_t disp_size;
  uint8_t imm_offset;
  uint8_t imm_size;
} cs_x86_encoding;

#define CS_X86_MAX_OPS 4

typedef struct cs_x86
{
  uint8_t prefix[4];
  uint8_t opcode[4];
  uint8_t rex;
  uint8_t addr_size;
  uint8_t modrm;
  uint8_t sib;
  int64_t disp;
  cs_x86_encoding encoding;

  uint8_t op_count;
  cs_x86_op operands[CS_X86_MAX_OPS];
} cs_x86;

typedef struct cs_detail
{
  uint16_t regs_read[16];
  uint8_t regs_read_count;
  uint16_t regs_write[20];
  uint8_t regs_write_count;

  union
  {
    cs_x86 x86;
  };
} cs_detail;

typedef struct cs_insn
{
  unsigned int id;
  uint64_t address;
  uint16_t size;
  uint8_t bytes[24];
  char mnemonic[32];
  char op_str[160];
  cs_detail * detail;
} cs_insn;

typedef uint16_t cs_regs[64];

/* ---- API ---------------------------------------------------------------- */

cs_err cs_open (cs_arch arch, cs_mode mode, csh * handle);
cs_err cs_close (csh * handle);
cs_err cs_option (csh handle, cs_opt_type type, size_t value);

cs_insn * cs_malloc (csh handle);
void cs_free (cs_insn * insn, size_t count);

bool cs_disasm_iter (csh handle, const uint8_t ** code, size_t * size,
    uint64_t * address, cs_insn * insn);
size_t cs_disasm (csh handle, const uint8_t * code, size_t code_size,
    uint64_t address, size_t count, cs_insn ** insn);

bool cs_reg_read (csh handle, const cs_insn * insn, unsigned int reg_id);
bool cs_reg_write (csh handle, const cs_insn * insn, unsigned int reg_id);

void cs_arch_register_x86 (void);

#ifdef __cplusplus
}
#endif

#endif
