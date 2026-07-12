/*
 * Copyright (C) 2025 hoox authors
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

/*
 * Off-page code patcher for Apple arm64 (macOS / iOS / tvOS).
 *
 * On Apple arm64 there is no RWX and no usable code segment on modern kernels,
 * so hoox patches a target's code by flipping its page to RW (VM_PROT_COPY,
 * execute dropped), writing, then restoring RX. During that window the page is
 * non-executable — and if any code hoox needs to run while the page is writable
 * lives on that same 16 KiB page (self-hosting: hoox statically linked into the
 * target, a target function sharing a page with hoox's patch/interceptor code),
 * the patching thread faults on its next instruction fetch.
 *
 * The in-place path in hoox_memory_patch_code_pages() runs a non-trivial call
 * graph inside that window (the apply callback reaches the interceptor backend's
 * activate/deactivate trampoline writers and function-address helper), so there
 * is no small, fixed set of "patch anchors" whose pages fully describe what
 * executes while the target page is non-executable — a target sharing a page
 * with any of those helpers would self-fault. This backend sidesteps the whole
 * question: it performs the permission-flip + write + restore from a *hoox-owned
 * scratch executable page* that is never itself a patch target. We emit a tiny
 * position-independent stub that does
 *
 *     mach_vm_protect(task, base, size, FALSE, RW|COPY)   // if it fails, bail
 *     memcpy(base, source, size)                          // write patched copy
 *     mach_vm_protect(task, base, size, FALSE, RX)
 *
 * and call it. Because the only instructions executing during the no-X window
 * are on the stub's page (plus libsystem's mach_vm_protect / memcpy, in the
 * shared cache), flipping the target page can't fault the running patcher —
 * regardless of the target's layout. Other threads are suspended by the caller
 * (they would fault if they ran on the target page).
 *
 * The caller applies the patch into a private, page-granular copy of the target
 * lump; the stub copies that whole copy back via memcpy. Using memcpy (rather
 * than an unrolled per-word store sequence) keeps the stub a fixed handful of
 * instructions no matter how much changed, so it handles everything uniformly:
 * a small redirect, several redirects installed on one page in a single
 * transaction, an edit straddling a page boundary, and a full page written for
 * the first time (e.g. materialising a freshly-allocated RW thunk page).
 */

#include "hooxmemory.h"

#include "hooxmemory-priv.h"

#include <string.h>

#if defined (HAVE_DARWIN) && defined (HAVE_ARM64)

#include "hooxarm64writer.h"
#include "hooxdarwin.h"

#include "hxarray.h"
#include "hxmem.h"

static hx_boolean
hoox_darwin_run_offpage_patch (hx_pointer base,
                               hx_size size,
                               hx_pointer source)
{
  hx_uint page_size = hoox_query_page_size ();
  hx_pointer stub;
  HooxArm64Writer aw;
  HooxAddress task = (HooxAddress) (hx_uintptr) mach_task_self ();
  HooxAddress mvp = (HooxAddress) (hx_uintptr) &mach_vm_protect;
  HooxAddress mcp = (HooxAddress) (hx_uintptr) &memcpy;
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

  /* mach_vm_protect(task, base, size, FALSE, RW|COPY) */
  hoox_arm64_writer_put_call_address_with_arguments (&aw, mvp, 5,
      HOOX_ARG_ADDRESS, task,
      HOOX_ARG_ADDRESS, (HooxAddress) (hx_uintptr) base,
      HOOX_ARG_ADDRESS, (HooxAddress) size,
      HOOX_ARG_ADDRESS, (HooxAddress) 0,
      HOOX_ARG_ADDRESS, rw);

  /* If it failed (e.g. hardened codesigning), the page is still RX — return the
   * error without writing (which would fault) or restoring. mach_vm_protect
   * returns a 32-bit kern_return_t in w0; test w0 (not x0, whose high half is
   * left unspecified by the calling convention). */
  hoox_arm64_writer_put_cbnz_reg_label (&aw, HX_ARM64_REG_W0, "done");

  /* memcpy(base, source, size) — write the whole patched copy back. */
  hoox_arm64_writer_put_call_address_with_arguments (&aw, mcp, 3,
      HOOX_ARG_ADDRESS, (HooxAddress) (hx_uintptr) base,
      HOOX_ARG_ADDRESS, (HooxAddress) (hx_uintptr) source,
      HOOX_ARG_ADDRESS, (HooxAddress) size);

  /* mach_vm_protect(task, base, size, FALSE, RX) */
  hoox_arm64_writer_put_call_address_with_arguments (&aw, mvp, 5,
      HOOX_ARG_ADDRESS, task,
      HOOX_ARG_ADDRESS, (HooxAddress) (hx_uintptr) base,
      HOOX_ARG_ADDRESS, (HooxAddress) size,
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
    hoox_free_pages (stub);
    return FALSE;
  }
  hoox_clear_cache (stub, page_size);

  fn = (int (*) (void)) stub;
  kr = fn ();

  hoox_free_pages (stub);
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

  /*
   * `coalesce` is the caller's hint, but off-page correctness *requires*
   * coalescing physically-contiguous pages regardless of it: a redirect whose
   * prologue straddles a page boundary registers both its start and end page,
   * and the write into the private copy must land in one contiguous buffer, or
   * it would overrun a per-page allocation and leave the spilled-over tail
   * unwritten. We therefore always coalesce below.
   */
  (void) coalesce;

  while (i < sorted_addresses->len)
  {
    hx_pointer lump_start = hx_ptr_array_index (sorted_addresses, i);
    hx_uint n_pages = 1;
    hx_size lump_size;
    hx_pointer temp;
    hx_uint k;

    while (i + n_pages < sorted_addresses->len &&
        hx_ptr_array_index (sorted_addresses, i + n_pages) ==
        (hx_pointer) ((hx_uint8 *) lump_start +
            (hx_size) n_pages * page_size))
      n_pages++;

    lump_size = (hx_size) n_pages * page_size;

    temp = hx_malloc (lump_size);
    memcpy (temp, lump_start, lump_size);           /* read the live (RX) code */

    /*
     * apply() is keyed by target page and writes at (source + (func - page)),
     * so it must be called once per page — never once for the whole run, which
     * would honour only the first page's updates. Because `temp` spans the
     * entire contiguous run, a redirect that straddles a page boundary spills
     * safely into the next page of the same buffer instead of overrunning it.
     */
    for (k = 0; k != n_pages; k++)
    {
      hx_pointer page = (hx_pointer) ((hx_uint8 *) lump_start +
          (hx_size) k * page_size);
      hx_pointer src = (hx_pointer) ((hx_uint8 *) temp +
          (hx_size) k * page_size);
      apply (src, page, 1, apply_data);
    }

    if (memcmp (temp, lump_start, lump_size) == 0)  /* nothing changed */
    {
      hx_free (temp);
      i += n_pages;
      continue;
    }

    /* Write the whole patched lump back from the hoox-owned scratch page. */
    ok = hoox_darwin_run_offpage_patch (lump_start, lump_size, temp);

    hx_free (temp);

    if (!ok)
      break;

    hoox_clear_cache (lump_start, lump_size);
    i += n_pages;
  }

  return ok;
}

#endif /* HAVE_DARWIN && HAVE_ARM64 */
