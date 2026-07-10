/*
 * hoox — definitions for MSVC segment intrinsics that clang (in GNU mode)
 * references from the Windows TLS backend but does not provide as builtins.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

/* clang.exe defines both __clang__ and _MSC_VER; it references __writegsqword
 * from the TLS backend but does not always provide it as a builtin, so define
 * it here for the clang/gcc toolchains. */
#if defined (_WIN32) && (defined (__clang__) || defined (__GNUC__))

#include <stdint.h>

/* GS-relative 64-bit store: [gs_base + offset] = data. */
void
__writegsqword (unsigned long offset,
                unsigned long long data)
{
  __asm__ __volatile__ ("movq %1, %%gs:(%0)"
      :
      : "r" ((uint64_t) offset), "r" ((uint64_t) data)
      : "memory");
}

#endif
