/*
 * Copyright (C) 2008-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2023 Håvard Sørbø <havard@hsorbo.no>
 * Copyright (C) 2024 Yannis Juglaret <yjuglaret@mozilla.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOXDEFS_H__
#define __HOOXDEFS_H__

#include <hx_disasm.h>
#include "hxglib.h"

#if HX_API_MAJOR >= 6
# define HX_ARCH_ARM64 HX_ARCH_AARCH64
#endif

#if !defined (HOOX_STATIC) && defined (HX_OS_WIN32)
#  ifdef HOOX_EXPORTS
#    define HOOX_API __declspec(dllexport)
#  else
#    define HOOX_API __declspec(dllimport)
#  endif
#else
#  define HOOX_API
#endif

HX_BEGIN_DECLS

#define HOOX_ERROR hoox_error_quark ()

typedef enum {
  HOOX_ERROR_FAILED,
  HOOX_ERROR_NOT_FOUND,
  HOOX_ERROR_EXISTS,
  HOOX_ERROR_PERMISSION_DENIED,
  HOOX_ERROR_INVALID_ARGUMENT,
  HOOX_ERROR_NOT_SUPPORTED,
  HOOX_ERROR_INVALID_DATA,
} HooxError;

typedef hx_uint64 HooxAddress;
#define HOOX_ADDRESS(a) ((HooxAddress) (hx_uintptr) (a))
#define HOOX_TYPE_ADDRESS (hoox_address_get_type ())
typedef hx_uint HooxOS;
typedef hx_uint HooxCallingConvention;
typedef hx_uint HooxAbiType;
typedef hx_uint HooxCpuFeatures;
typedef hx_uint HooxInstructionEncoding;
typedef hx_uint HooxArgType;
typedef struct _HooxArgument HooxArgument;
typedef hx_uint HooxBranchHint;
typedef struct _HooxIA32CpuContext HooxIA32CpuContext;
typedef struct _HooxX64CpuContext HooxX64CpuContext;
typedef struct _HooxArmCpuContext HooxArmCpuContext;
typedef union _HooxArmVectorReg HooxArmVectorReg;
typedef struct _HooxArm64CpuContext HooxArm64CpuContext;
typedef union _HooxArm64VectorReg HooxArm64VectorReg;
typedef struct _HooxMipsCpuContext HooxMipsCpuContext;
typedef hx_uint HooxRelocationScenario;

#if defined (_M_IX86) || defined (__i386__)
# define HOOX_NATIVE_CPU HOOX_CPU_IA32
# define HOOX_DEFAULT_HX_ARCH HX_ARCH_X86
# define hoox_cs_arch_register_native hx_arch_register_x86
/**
 * HOOX_DEFAULT_HX_MODE: (skip)
 */
# define HOOX_DEFAULT_HX_MODE HX_MODE_32
typedef HooxIA32CpuContext HooxCpuContext;
#elif defined (_M_X64) || defined (__x86_64__)
# define HOOX_NATIVE_CPU HOOX_CPU_AMD64
# define HOOX_DEFAULT_HX_ARCH HX_ARCH_X86
# define hoox_cs_arch_register_native hx_arch_register_x86
/**
 * HOOX_DEFAULT_HX_MODE: (skip)
 */
# define HOOX_DEFAULT_HX_MODE HX_MODE_64
typedef HooxX64CpuContext HooxCpuContext;
#elif defined (_M_ARM) || defined (__arm__)
# define HOOX_NATIVE_CPU HOOX_CPU_ARM
# define HOOX_DEFAULT_HX_ARCH HX_ARCH_ARM
# define hoox_cs_arch_register_native hx_arch_register_arm
/**
 * HOOX_DEFAULT_HX_MODE: (skip)
 */
# define HOOX_DEFAULT_HX_MODE \
    ((hx_mode) (HX_MODE_ARM | HX_MODE_V8 | HOOX_DEFAULT_HX_ENDIAN))
# define HOOX_PSR_T_BIT 0x20
typedef HooxArmCpuContext HooxCpuContext;
#elif defined (_M_ARM64) || defined (__aarch64__)
# define HOOX_NATIVE_CPU HOOX_CPU_ARM64
# define HOOX_DEFAULT_HX_ARCH HX_ARCH_ARM64
# define hoox_cs_arch_register_native hx_arch_register_arm64
/**
 * HOOX_DEFAULT_HX_MODE: (skip)
 */
# define HOOX_DEFAULT_HX_MODE HOOX_DEFAULT_HX_ENDIAN
typedef HooxArm64CpuContext HooxCpuContext;
#elif defined (__mips__)
# define HOOX_NATIVE_CPU HOOX_CPU_MIPS
# define HOOX_DEFAULT_HX_ARCH HX_ARCH_MIPS
# define hoox_cs_arch_register_native cs_arch_register_mips
# if HX_SIZEOF_VOID_P == 4
/**
 * HOOX_DEFAULT_HX_MODE: (skip)
 */
#  define HOOX_DEFAULT_HX_MODE ((hx_mode) \
    (HX_MODE_MIPS32 | HOOX_DEFAULT_HX_ENDIAN))
# else
/**
 * HOOX_DEFAULT_HX_MODE: (skip)
 */
#  define HOOX_DEFAULT_HX_MODE ((hx_mode) \
    (HX_MODE_MIPS64 | HOOX_DEFAULT_HX_ENDIAN))
# endif
typedef HooxMipsCpuContext HooxCpuContext;
#else
# error Unsupported architecture.
#endif
/*
 * The only non-legacy big-endian configuration on 32-bit ARM systems is BE8.
 * In this configuration, whilst the data is in big-endian, the code stream is
 * still in little-endian. Since Capstone is disassembling the code stream, it
 * should work in little-endian even on BE8 systems. On big-endian 64-bit ARM
 * systems, the code stream is likewise in little-endian.
 */
#if HX_BYTE_ORDER == HX_LITTLE_ENDIAN || \
    defined (__arm__) || \
    defined (_M_ARM64) || \
    defined (__aarch64__)
# define HOOX_DEFAULT_HX_ENDIAN HX_MODE_LITTLE_ENDIAN
#else
# define HOOX_DEFAULT_HX_ENDIAN HX_MODE_BIG_ENDIAN
#endif
#ifdef HX_OS_WIN32
# define HOOX_NATIVE_ABI            HOOX_ABI_WINDOWS
# define HOOX_NATIVE_ABI_IS_WINDOWS 1
# define HOOX_NATIVE_ABI_IS_UNIX    0
#else
# define HOOX_NATIVE_ABI            HOOX_ABI_UNIX
# define HOOX_NATIVE_ABI_IS_WINDOWS 0
# define HOOX_NATIVE_ABI_IS_UNIX    1
#endif

enum _HooxOS
{
  HOOX_OS_NONE,
  HOOX_OS_WINDOWS,
  HOOX_OS_MACOS,
  HOOX_OS_LINUX,
  HOOX_OS_IOS,
  HOOX_OS_WATCHOS,
  HOOX_OS_TVOS,
  HOOX_OS_XROS,
  HOOX_OS_ANDROID,
  HOOX_OS_FREEBSD,
  HOOX_OS_QNX
};

enum _HooxCallingConvention
{
  HOOX_CALL_CAPI,
  HOOX_CALL_SYSAPI
};

enum _HooxAbiType
{
  HOOX_ABI_UNIX,
  HOOX_ABI_WINDOWS
};

typedef enum {
  HOOX_CPU_INVALID,
  HOOX_CPU_IA32,
  HOOX_CPU_AMD64,
  HOOX_CPU_ARM,
  HOOX_CPU_ARM64,
  HOOX_CPU_MIPS
} HooxCpuType;

enum _HooxCpuFeatures
{
  HOOX_CPU_AVX2            = 1 << 0,
  HOOX_CPU_CET_SS          = 1 << 1,
  HOOX_CPU_THUMB_INTERWORK = 1 << 2,
  HOOX_CPU_VFP2            = 1 << 3,
  HOOX_CPU_VFP3            = 1 << 4,
  HOOX_CPU_VFPD32          = 1 << 5,
  HOOX_CPU_PTRAUTH         = 1 << 6,
};

typedef enum {
  HOOX_MEMORY_ACCESS_OPEN,
  HOOX_MEMORY_ACCESS_EXCLUSIVE,
} HooxMemoryAccess;

enum _HooxInstructionEncoding
{
  HOOX_INSTRUCTION_DEFAULT,
  HOOX_INSTRUCTION_SPECIAL
};

enum _HooxArgType
{
  HOOX_ARG_ADDRESS,
  HOOX_ARG_REGISTER
};

struct _HooxArgument
{
  HooxArgType type;

  union
  {
    HooxAddress address;
    hx_int reg;
  } value;
};

enum _HooxBranchHint
{
  HOOX_NO_HINT,
  HOOX_LIKELY,
  HOOX_UNLIKELY
};

struct _HooxIA32CpuContext
{
  hx_uint32 eip;

  hx_uint32 edi;
  hx_uint32 esi;
  hx_uint32 ebp;
  hx_uint32 esp;
  hx_uint32 ebx;
  hx_uint32 edx;
  hx_uint32 ecx;
  hx_uint32 eax;
};

struct _HooxX64CpuContext
{
  hx_uint64 rip;

  hx_uint64 r15;
  hx_uint64 r14;
  hx_uint64 r13;
  hx_uint64 r12;
  hx_uint64 r11;
  hx_uint64 r10;
  hx_uint64 r9;
  hx_uint64 r8;

  hx_uint64 rdi;
  hx_uint64 rsi;
  hx_uint64 rbp;
  hx_uint64 rsp;
  hx_uint64 rbx;
  hx_uint64 rdx;
  hx_uint64 rcx;
  hx_uint64 rax;
};

union _HooxArmVectorReg
{
  hx_uint8 q[16];
  hx_double d[2];
  hx_float s[4];
};

struct _HooxArmCpuContext
{
  hx_uint32 pc;
  hx_uint32 sp;
  hx_uint32 cpsr;

  hx_uint32 r8;
  hx_uint32 r9;
  hx_uint32 r10;
  hx_uint32 r11;
  hx_uint32 r12;

  HooxArmVectorReg v[16];

  hx_uint32 _padding;

  hx_uint32 r[8];
  hx_uint32 lr;
};

union _HooxArm64VectorReg
{
  hx_uint8 q[16];
  hx_double d;
  hx_float s;
  hx_uint16 h;
  hx_uint8 b;
};

struct _HooxArm64CpuContext
{
  hx_uint64 pc;
  hx_uint64 sp;
  hx_uint64 nzcv;

  hx_uint64 x[29];
  hx_uint64 fp;
  hx_uint64 lr;

#ifndef HX_OS_NONE
  /* Bare-metal threads can be entered with a near-exhausted kernel stack, so
   * there the trampolines stay integer-only and skip the vector registers. */
  HooxArm64VectorReg v[32];
#endif
};

struct _HooxMipsCpuContext
{
  /*
   * This structure represents the register state pushed onto the stack by the
   * trampoline which allows us to vector from the original minimal assembly
   * hook to architecture agnostic C code inside frida-gum. These registers are
   * natively sized. Even if some have not been expanded to 64-bits from the
   * MIPS32 architecture MIPS can only perform aligned data access and as such
   * pushing zero extended values is simpler than attempting to push minimally
   * sized data types.
   */
  hx_size pc;

  hx_size gp;
  hx_size sp;
  hx_size fp;
  hx_size ra;

  hx_size hi;
  hx_size lo;

  hx_size at;

  hx_size v0;
  hx_size v1;

  hx_size a0;
  hx_size a1;
  hx_size a2;
  hx_size a3;

  hx_size t0;
  hx_size t1;
  hx_size t2;
  hx_size t3;
  hx_size t4;
  hx_size t5;
  hx_size t6;
  hx_size t7;
  hx_size t8;
  hx_size t9;

  hx_size s0;
  hx_size s1;
  hx_size s2;
  hx_size s3;
  hx_size s4;
  hx_size s5;
  hx_size s6;
  hx_size s7;

  hx_size k0;
  hx_size k1;
};

enum _HooxRelocationScenario
{
  HOOX_SCENARIO_OFFLINE,
  HOOX_SCENARIO_ONLINE
};

typedef enum {
  HOOX_RELOCATION_DEFAULT,
  HOOX_RELOCATION_CHECKED,
  HOOX_RELOCATION_UNCHECKED,
  HOOX_RELOCATION_FORCED,
} HooxRelocationPolicy;

#ifndef __arm__
# if HX_SIZEOF_VOID_P == 8
#  define HOOX_CPU_CONTEXT_XAX(c) ((c)->rax)
#  define HOOX_CPU_CONTEXT_XCX(c) ((c)->rcx)
#  define HOOX_CPU_CONTEXT_XDX(c) ((c)->rdx)
#  define HOOX_CPU_CONTEXT_XBX(c) ((c)->rbx)
#  define HOOX_CPU_CONTEXT_XSP(c) ((c)->rsp)
#  define HOOX_CPU_CONTEXT_XBP(c) ((c)->rbp)
#  define HOOX_CPU_CONTEXT_XSI(c) ((c)->rsi)
#  define HOOX_CPU_CONTEXT_XDI(c) ((c)->rdi)
#  define HOOX_CPU_CONTEXT_XIP(c) ((c)->rip)
#  define HOOX_CPU_CONTEXT_OFFSET_XAX (HX_STRUCT_OFFSET (HooxCpuContext, rax))
#  define HOOX_CPU_CONTEXT_OFFSET_XCX (HX_STRUCT_OFFSET (HooxCpuContext, rcx))
#  define HOOX_CPU_CONTEXT_OFFSET_XDX (HX_STRUCT_OFFSET (HooxCpuContext, rdx))
#  define HOOX_CPU_CONTEXT_OFFSET_XBX (HX_STRUCT_OFFSET (HooxCpuContext, rbx))
#  define HOOX_CPU_CONTEXT_OFFSET_XSP (HX_STRUCT_OFFSET (HooxCpuContext, rsp))
#  define HOOX_CPU_CONTEXT_OFFSET_XBP (HX_STRUCT_OFFSET (HooxCpuContext, rbp))
#  define HOOX_CPU_CONTEXT_OFFSET_XSI (HX_STRUCT_OFFSET (HooxCpuContext, rsi))
#  define HOOX_CPU_CONTEXT_OFFSET_XDI (HX_STRUCT_OFFSET (HooxCpuContext, rdi))
#  define HOOX_CPU_CONTEXT_OFFSET_XIP (HX_STRUCT_OFFSET (HooxCpuContext, rip))
# else
#  define HOOX_CPU_CONTEXT_XAX(c) ((c)->eax)
#  define HOOX_CPU_CONTEXT_XCX(c) ((c)->ecx)
#  define HOOX_CPU_CONTEXT_XDX(c) ((c)->edx)
#  define HOOX_CPU_CONTEXT_XBX(c) ((c)->ebx)
#  define HOOX_CPU_CONTEXT_XSP(c) ((c)->esp)
#  define HOOX_CPU_CONTEXT_XBP(c) ((c)->ebp)
#  define HOOX_CPU_CONTEXT_XSI(c) ((c)->esi)
#  define HOOX_CPU_CONTEXT_XDI(c) ((c)->edi)
#  define HOOX_CPU_CONTEXT_XIP(c) ((c)->eip)
#  define HOOX_CPU_CONTEXT_OFFSET_XAX (HX_STRUCT_OFFSET (HooxCpuContext, eax))
#  define HOOX_CPU_CONTEXT_OFFSET_XCX (HX_STRUCT_OFFSET (HooxCpuContext, ecx))
#  define HOOX_CPU_CONTEXT_OFFSET_XDX (HX_STRUCT_OFFSET (HooxCpuContext, edx))
#  define HOOX_CPU_CONTEXT_OFFSET_XBX (HX_STRUCT_OFFSET (HooxCpuContext, ebx))
#  define HOOX_CPU_CONTEXT_OFFSET_XSP (HX_STRUCT_OFFSET (HooxCpuContext, esp))
#  define HOOX_CPU_CONTEXT_OFFSET_XBP (HX_STRUCT_OFFSET (HooxCpuContext, ebp))
#  define HOOX_CPU_CONTEXT_OFFSET_XSI (HX_STRUCT_OFFSET (HooxCpuContext, esi))
#  define HOOX_CPU_CONTEXT_OFFSET_XDI (HX_STRUCT_OFFSET (HooxCpuContext, edi))
#  define HOOX_CPU_CONTEXT_OFFSET_XIP (HX_STRUCT_OFFSET (HooxCpuContext, eip))
# endif
#endif

#define HOOX_MAX_PATH                 260
#define HOOX_MAX_TYPE_NAME             16
#define HOOX_MAX_SYMBOL_NAME         2048

#define HOOX_MAX_THREADS              768
#define HOOX_MAX_CALL_DEPTH            32
#define HOOX_MAX_BACKTRACE_DEPTH       16
#define HOOX_MAX_WORST_CASE_INFO_SIZE 128

#define HOOX_MAX_LISTENERS_PER_FUNCTION 2
#define HOOX_MAX_LISTENER_DATA       1024

#define HOOX_MAX_THREAD_RANGES 2

#if defined (HAVE_I386)
# if HX_SIZEOF_VOID_P == 8
#  define HOOX_CPU_MODE HX_MODE_64
#  define HOOX_HX_THUNK
# else
#  define HOOX_CPU_MODE HX_MODE_32
#  define HOOX_HX_THUNK HOOX_FASTCALL
# endif
#else
# if HX_BYTE_ORDER == HX_LITTLE_ENDIAN
#  define HOOX_CPU_MODE HX_MODE_LITTLE_ENDIAN
# else
#  define HOOX_CPU_MODE HX_MODE_BIG_ENDIAN
# endif
#endif
#if !defined (HX_OS_WIN32) && HX_SIZEOF_VOID_P == 8
# define HOOX_HX_THUNK_REG_ARG0 HOOX_HX_XDI
# define HOOX_HX_THUNK_REG_ARG1 HOOX_HX_XSI
#else
# define HOOX_HX_THUNK_REG_ARG0 HOOX_HX_XCX
# define HOOX_HX_THUNK_REG_ARG1 HOOX_HX_XDX
#endif
#define HOOX_RED_ZONE_SIZE 128

#if defined (_M_IX86) || defined (__i386__)
# ifdef _MSC_VER
#  define HOOX_CDECL __cdecl
#  define HOOX_STDCALL __stdcall
#  define HOOX_FASTCALL __fastcall
# else
#  define HOOX_CDECL __attribute__ ((cdecl))
#  define HOOX_STDCALL __attribute__ ((stdcall))
#  define HOOX_FASTCALL __attribute__ ((fastcall))
# endif
#else
# define HOOX_CDECL
# define HOOX_STDCALL
# define HOOX_FASTCALL
#endif

#ifdef _MSC_VER
# define HOOX_NOINLINE __declspec (noinline)
#else
# define HOOX_NOINLINE __attribute__ ((noinline))
#endif

#define HOOX_ALIGN_POINTER(t, p, b) \
    ((t) HX_SIZE_TO_POINTER (((HX_POINTER_TO_SIZE (p) + ((hx_size) (b - 1))) & \
        ~((hx_size) (b - 1)))))
#define HOOX_ALIGN_SIZE(s, b) \
    ((((hx_size) s) + ((hx_size) (b - 1))) & ~((hx_size) (b - 1)))

#define HOOX_FUNCPTR_TO_POINTER(f) (HX_SIZE_TO_POINTER (f))
#define HOOX_POINTER_TO_FUNCPTR(t, p) ((t) HX_POINTER_TO_SIZE (p))

#define HOOX_INT2_MASK  0x00000003U
#define HOOX_INT3_MASK  0x00000007U
#define HOOX_INT4_MASK  0x0000000fU
#define HOOX_INT5_MASK  0x0000001fU
#define HOOX_INT6_MASK  0x0000003fU
#define HOOX_INT8_MASK  0x000000ffU
#define HOOX_INT10_MASK 0x000003ffU
#define HOOX_INT11_MASK 0x000007ffU
#define HOOX_INT12_MASK 0x00000fffU
#define HOOX_INT14_MASK 0x00003fffU
#define HOOX_INT16_MASK 0x0000ffffU
#define HOOX_INT18_MASK 0x0003ffffU
#define HOOX_INT19_MASK 0x0007ffffU
#define HOOX_INT24_MASK 0x00ffffffU
#define HOOX_INT26_MASK 0x03ffffffU
#define HOOX_INT28_MASK 0x0fffffffU
#define HOOX_INT32_MASK 0xffffffffU

#define HOOX_IS_WITHIN_UINT7_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (0) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (127))
#define HOOX_IS_WITHIN_UINT8_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (0) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (255))
#define HOOX_IS_WITHIN_INT8_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (-128) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (127))
#define HOOX_IS_WITHIN_INT11_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (-1024) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (1023))
#define HOOX_IS_WITHIN_INT14_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (-8192) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (8191))
#define HOOX_IS_WITHIN_INT16_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (-32768) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (32767))
#define HOOX_IS_WITHIN_INT18_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (-131072) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (131071))
#define HOOX_IS_WITHIN_INT19_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (-262144) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (262143))
#define HOOX_IS_WITHIN_INT20_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (-524288) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (524287))
#define HOOX_IS_WITHIN_INT21_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (-1048576) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (1048575))
#define HOOX_IS_WITHIN_INT24_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (-8388608) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (8388607))
#define HOOX_IS_WITHIN_INT26_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (-33554432) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (33554431))
#define HOOX_IS_WITHIN_INT28_RANGE(i) \
    (((hx_int64) (i)) >= HX_INT64_CONSTANT (-134217728) && \
     ((hx_int64) (i)) <= HX_INT64_CONSTANT (134217727))
#define HOOX_IS_WITHIN_INT32_RANGE(i) \
    (((hx_int64) (i)) >= (hx_int64) HX_MININT32 && \
     ((hx_int64) (i)) <= (hx_int64) HX_MAXINT32)

#ifdef HX_NORETURN
# define HOOX_NORETURN HX_NORETURN
#else
# define HOOX_NORETURN
#endif

HOOX_API HxQuark hoox_error_quark (void);

HOOX_API HOOX_NORETURN void hoox_panic (const hx_char * format, ...)
    HX_ANALYZER_NORETURN;

HOOX_API HooxCpuFeatures hoox_query_cpu_features (void);

HOOX_API hx_pointer hoox_cpu_context_get_nth_argument (HooxCpuContext * self,
    hx_uint n);
HOOX_API void hoox_cpu_context_replace_nth_argument (HooxCpuContext * self,
    hx_uint n, hx_pointer value);
HOOX_API hx_pointer hoox_cpu_context_get_return_value (HooxCpuContext * self);
HOOX_API void hoox_cpu_context_replace_return_value (HooxCpuContext * self,
    hx_pointer value);

HOOX_API HxType hoox_address_get_type (void) HX_GNUC_CONST;

HX_END_DECLS

#endif
