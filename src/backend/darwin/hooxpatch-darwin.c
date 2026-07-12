/*
 * Copyright (C) 2025 hoox authors
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

/*
 * Off-page code patcher for Apple arm64 (macOS / iOS / tvOS).
 *
 * On Apple arm64 there is no RWX and no usable code segment on modern kernels,
 * so hoox patches a target's code in place by flipping its page to RW
 * (VM_PROT_COPY, execute dropped), writing, then restoring RX. During that
 * window the page is non-executable — and if hoox's *own* patch code happens to
 * live on the same 16 KiB page (self-hosting: hoox statically linked into the
 * target, target function on the same page as the patcher), the patching thread
 * faults on its next instruction fetch.
 *
 * The fix here runs the permission-flip + write + restore from a *hoox-owned
 * scratch executable page* that is never itself a patch target: we emit a tiny
 * position-independent stub that does
 *
 *     mach_vm_protect(task, page, size, FALSE, RW|COPY)   // if it fails, bail
 *     *(u32 *)(dst + i*4) = word[i]   for each changed word
 *     mach_vm_protect(task, page, size, FALSE, RX)
 *
 * and call it. Because the executing instructions during the no-X window are on
 * the stub's page (not the target's), flipping the target page can't fault the
 * running patcher — regardless of the target's layout. Other threads are still
 * suspended by the caller (they would fault if they ran on the target page).
 *
 * We keep the write minimal: the caller applies the patch into a private copy of
 * the page(s), then we diff to find the small, contiguous changed 4-byte-word
 * range (a redirect is <= 16 bytes) and only that range is written back, so the
 * emitted stub is a short unrolled sequence — no loop, no fancy addressing.
 */

#include "hooxmemory.h"

#include "hooxmemory-priv.h"

#include <string.h>

#if defined (HAVE_DARWIN) && defined (HAVE_ARM64)

#include "hooxarm64writer.h"
#include "hooxdarwin.h"

#include "hxarray.h"
#include "hxmem.h"

/* A redirect is at most 16 bytes; cap generously and fall back otherwise. */
#define HOOX_OFFPAGE_MAX_WORDS 64

static hx_boolean
hoox_darwin_run_offpage_patch (hx_pointer protect_base,
                               hx_size protect_size,
                               hx_pointer dst,
                               const hx_uint32 * words,
                               hx_uint n_words)
{
  hx_uint page_size = hoox_query_page_size ();
  hx_pointer stub;
  HooxArm64Writer aw;
  hx_uint i;
  HooxAddress task = (HooxAddress) (hx_uintptr) mach_task_self ();
  HooxAddress mvp = (HooxAddress) (hx_uintptr) &mach_vm_protect;
  HooxAddress rw = (HooxAddress)
      (VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
  HooxAddress rx = (HooxAddress) (VM_PROT_READ | VM_PROT_EXECUTE);
  int (* fn) (void);
  int kr;

  stub = hoox_alloc_n_pages (1, HOOX_PAGE_RW);
  if (stub == NULL)
    return FALSE;

  hoox_arm64_writer_init (&aw, stub);
  aw.pc = (HooxAddress) (hx_uintptr) stub;

  hoox_arm64_writer_put_push_reg_reg (&aw, HX_ARM64_REG_FP, HX_ARM64_REG_LR);

  /* mach_vm_protect(task, protect_base, protect_size, FALSE, RW|COPY) */
  hoox_arm64_writer_put_call_address_with_arguments (&aw, mvp, 5,
      HOOX_ARG_ADDRESS, task,
      HOOX_ARG_ADDRESS, (HooxAddress) (hx_uintptr) protect_base,
      HOOX_ARG_ADDRESS, (HooxAddress) protect_size,
      HOOX_ARG_ADDRESS, (HooxAddress) 0,
      HOOX_ARG_ADDRESS, rw);

  /* If it failed (e.g. hardened codesigning), the page is still RX — return the
   * error in x0 (w0) without writing (which would fault) or restoring. */
  hoox_arm64_writer_put_cbnz_reg_label (&aw, HX_ARM64_REG_X0, "done");

  hoox_arm64_writer_put_ldr_reg_u64 (&aw, HX_ARM64_REG_X9,
      (hx_uint64) (hx_uintptr) dst);
  for (i = 0; i != n_words; i++)
  {
    hoox_arm64_writer_put_ldr_reg_u32 (&aw, HX_ARM64_REG_W10, words[i]);
    hoox_arm64_writer_put_str_reg_reg_offset (&aw, HX_ARM64_REG_W10,
        HX_ARM64_REG_X9, (hx_size) i * 4);
  }

  /* mach_vm_protect(task, protect_base, protect_size, FALSE, RX) */
  hoox_arm64_writer_put_call_address_with_arguments (&aw, mvp, 5,
      HOOX_ARG_ADDRESS, task,
      HOOX_ARG_ADDRESS, (HooxAddress) (hx_uintptr) protect_base,
      HOOX_ARG_ADDRESS, (HooxAddress) protect_size,
      HOOX_ARG_ADDRESS, (HooxAddress) 0,
      HOOX_ARG_ADDRESS, rx);

  hoox_arm64_writer_put_ldr_reg_u32 (&aw, HX_ARM64_REG_W0, 0);  /* success */

  hoox_arm64_writer_put_label (&aw, "done");
  hoox_arm64_writer_put_pop_reg_reg (&aw, HX_ARM64_REG_FP, HX_ARM64_REG_LR);
  hoox_arm64_writer_put_ret (&aw);
  hoox_arm64_writer_flush (&aw);
  hoox_arm64_writer_clear (&aw);

  if (!hoox_try_mprotect (stub, page_size, HOOX_PAGE_RX))
  {
    hoox_memory_free (stub, page_size);
    return FALSE;
  }
  hoox_clear_cache (stub, page_size);

  fn = (int (*) (void)) stub;
  kr = fn ();

  hoox_memory_free (stub, page_size);
  return kr == 0;
}

hx_boolean
_hoox_darwin_arm64_patch_pages (HxPtrArray * sorted_addresses,
                               hx_boolean coalesce,
                               HooxMemoryPatchPagesApplyFunc apply,
                               hx_pointer apply_data,
                               hx_size page_size)
{
  hx_uint i = 0;
  hx_boolean ok = TRUE;

  while (i < sorted_addresses->len)
  {
    hx_pointer lump_start = hx_ptr_array_index (sorted_addresses, i);
    hx_uint n_pages = 1;
    hx_size lump_size, first, last, b;
    hx_pointer temp;
    const hx_uint8 * tp;
    const hx_uint8 * op;
    hx_uint32 * words;
    hx_uint n_words, w;

    if (coalesce)
    {
      while (i + n_pages < sorted_addresses->len &&
          hx_ptr_array_index (sorted_addresses, i + n_pages) ==
          (hx_pointer) ((hx_uint8 *) lump_start +
              (hx_size) n_pages * page_size))
        n_pages++;
    }

    lump_size = (hx_size) n_pages * page_size;

    temp = hx_malloc (lump_size);
    memcpy (temp, lump_start, lump_size);           /* read the live (RX) code */
    apply (temp, lump_start, n_pages, apply_data);  /* write redirect into copy */

    /* Diff against the still-original live page to find the changed range. */
    tp = (const hx_uint8 *) temp;
    op = (const hx_uint8 *) lump_start;
    first = lump_size;
    last = 0;
    for (b = 0; b != lump_size; b++)
    {
      if (tp[b] != op[b])
      {
        if (first == lump_size)
          first = b;
        last = b;
      }
    }

    if (first == lump_size)  /* nothing changed for this lump */
    {
      hx_free (temp);
      i += n_pages;
      continue;
    }

    first &= ~(hx_size) 3;
    last |= (hx_size) 3;
    n_words = (hx_uint) ((last - first + 1) / 4);

    if (n_words > HOOX_OFFPAGE_MAX_WORDS)
    {
      hx_free (temp);
      ok = FALSE;
      break;
    }

    words = hx_new (hx_uint32, n_words);
    for (w = 0; w != n_words; w++)
      memcpy (&words[w], tp + first + (hx_size) w * 4, sizeof (hx_uint32));

    ok = hoox_darwin_run_offpage_patch (lump_start, lump_size,
        (hx_pointer) (op + first), words, n_words);

    hx_free (words);
    hx_free (temp);

    if (!ok)
      break;

    hoox_clear_cache (lump_start, lump_size);
    i += n_pages;
  }

  return ok;
}

#endif /* HAVE_DARWIN && HAVE_ARM64 */
