/*
 * M3 smoke test: exercise the memory primitives end-to-end on the target
 * platform — allocate an executable page, write machine code, run it, patch
 * it via gum_memory_patch_code, and run the patched version.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gummemory.h"

#include <stdio.h>
#include <string.h>

GUM_API void gum_internal_heap_ref (void);
GUM_API void gum_internal_heap_unref (void);

static int g_failures = 0;

#define CHECK(expr) \
    G_STMT_START { \
      if (!(expr)) { \
        fprintf (stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
      } \
    } G_STMT_END

/* x86-64: mov eax, imm32 ; ret  (B8 <imm32> C3) */
typedef int (* IntFunc) (void);

typedef struct
{
  const guint8 * code;
  gsize size;
} PatchData;

static void
apply_patch (gpointer mem, gpointer user_data)
{
  PatchData * d = user_data;
  memcpy (mem, d->code, d->size);
}

int
main (void)
{
  guint page_size;
  guint8 * page;
  IntFunc fn;
  const guint8 code_42[6] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };
  const guint8 code_99[6] = { 0xB8, 0x63, 0x00, 0x00, 0x00, 0xC3 };
  PatchData pd;

  gum_internal_heap_ref ();

  page_size = gum_query_page_size ();
  CHECK (page_size >= 4096);

  page = gum_alloc_n_pages (1, GUM_PAGE_RWX);
  CHECK (page != NULL);

  memcpy (page, code_42, sizeof (code_42));
  gum_memory_mark_code (page, sizeof (code_42));

  fn = (IntFunc) (void *) page;
  CHECK (fn () == 42);

  /* Patch it to return 99 via the W^X-safe patch path. */
  pd.code = code_99;
  pd.size = sizeof (code_99);
  CHECK (gum_memory_patch_code (page, sizeof (code_99), apply_patch, &pd));
  CHECK (fn () == 99);

  gum_free_pages (page);

  gum_internal_heap_unref ();

  if (g_failures == 0)
  {
    printf ("memory: all tests passed\n");
    return 0;
  }
  printf ("memory: %d failure(s)\n", g_failures);
  return 1;
}
