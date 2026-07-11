/*
 * M3 smoke test: exercise the memory primitives end-to-end on the target
 * platform — allocate an executable page, write machine code, run it, patch
 * it via hoox_memory_patch_code, and run the patched version.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxmemory.h"

#include <stdio.h>
#include <string.h>

HOOX_API void hoox_internal_heap_ref (void);
HOOX_API void hoox_internal_heap_unref (void);

static int hx_failures = 0;

#define CHECK(expr) \
    HX_STMT_START { \
      if (!(expr)) { \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        hx_failures++; \
      } \
    } HX_STMT_END

/*
 * Minimal "return a constant" function in native machine code:
 *   x86/x86-64: mov eax, imm32 ; ret   (B8 <imm32> C3)
 *   arm64:      mov w0, #imm   ; ret    (MOVZ 0x52800000|(imm<<5); RET D65F03C0)
 *   arm (A32):  mov r0, #imm   ; bx lr  (E3A000xx ; E12FFF1E)
 */
typedef int (* IntFunc) (void);

typedef struct
{
  const hx_uint8 * code;
  hx_size size;
} PatchData;

static void
apply_patch (hx_pointer mem, hx_pointer user_data)
{
  PatchData * d = user_data;
  memcpy (mem, d->code, d->size);
}

int
main (void)
{
  hx_uint page_size;
  hx_uint8 * page;
  IntFunc fn;
#if defined (__aarch64__) || defined (_M_ARM64)
  /* mov w0, #42 ; ret   and   mov w0, #99 ; ret  (little-endian) */
  const hx_uint8 code_42[8] =
      { 0x40, 0x05, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6 };
  const hx_uint8 code_99[8] =
      { 0x60, 0x0c, 0x80, 0x52, 0xc0, 0x03, 0x5f, 0xd6 };
#elif defined (__arm__) || defined (_M_ARM)
  /* A32: mov r0, #imm ; bx lr  (little-endian). Entered in ARM state because
   * the page pointer is word-aligned (bit0 clear), so bx lr interworks back. */
  const hx_uint8 code_42[8] =
      { 0x2a, 0x00, 0xa0, 0xe3, 0x1e, 0xff, 0x2f, 0xe1 };
  const hx_uint8 code_99[8] =
      { 0x63, 0x00, 0xa0, 0xe3, 0x1e, 0xff, 0x2f, 0xe1 };
#else
  const hx_uint8 code_42[6] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };
  const hx_uint8 code_99[6] = { 0xB8, 0x63, 0x00, 0x00, 0x00, 0xC3 };
#endif
  PatchData pd;

  hoox_internal_heap_ref ();

  /*
   * This smoke test allocates a single RWX page and executes code from it.
   * Platforms that enforce W^X (e.g. Apple Silicon) do not offer RWX at all —
   * there the hook engine uses the code-segment / writable-remap path instead,
   * which the interceptor suite exercises. Skip here when RWX is unavailable.
   */
  if (hoox_query_rwx_support () != HOOX_RWX_FULL)
  {
    printf ("memory: skipped (RWX unavailable on this platform)\n");
    hoox_internal_heap_unref ();
    return 0;
  }

  page_size = hoox_query_page_size ();
  CHECK (page_size >= 4096);

  page = hoox_alloc_n_pages (1, HOOX_PAGE_RWX);
  CHECK (page != NULL);

  memcpy (page, code_42, sizeof (code_42));
  hoox_memory_mark_code (page, sizeof (code_42));

  fn = (IntFunc) (void *) page;
  CHECK (fn () == 42);

  /* Patch it to return 99 via the W^X-safe patch path. */
  pd.code = code_99;
  pd.size = sizeof (code_99);
  CHECK (hoox_memory_patch_code (page, sizeof (code_99), apply_patch, &pd));
  CHECK (fn () == 99);

  hoox_free_pages (page);

  hoox_internal_heap_unref ();

  if (hx_failures == 0)
  {
    printf ("memory: all tests passed\n");
    return 0;
  }
  printf ("memory: %d failure(s)\n", hx_failures);
  return 1;
}
