/*
 * hoox — FreeBSD process/thread/range shim.
 *
 * A thin replacement for the slice of hooxprocess the hook engine needs,
 * avoiding the full frida hooxprocess.c tree. See PLAN.md 2.1 / TASKS.md T8.1:
 * on FreeBSD the RWX path is taken at runtime (hoox_query_rwx_support ==
 * HOOX_RWX_FULL), so thread enumeration / suspend / resume are *link*
 * requirements rather than hook hot-path. Range enumeration (via
 * sysctl KERN_PROC_VMMAP) backs the near-allocator used to place trampolines
 * within reach of the target.
 *
 * Derived from frida-gum's backend-freebsd.
 *
 * Copyright (C) 2022-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2023 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxprocess.h"
#include "hooxprocess-priv.h"
#include "hooxmodule.h"

#include "hxstrfuncs.h"

#include <dlfcn.h>
#include <errno.h>
#include <link.h>
#include <pthread.h>
#include <pthread_np.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysctl.h>
#include <sys/thr.h>
#include <sys/types.h>
#include <sys/user.h>

typedef struct _HooxEnumerateModulesContext HooxEnumerateModulesContext;

struct _HooxEnumerateModulesContext
{
  HooxFoundModuleFunc func;
  hx_pointer user_data;
  hx_boolean carry_on;
};

static struct kinfo_proc * hoox_query_threads (hx_uint * count);
static int hoox_emit_module_from_phdr (struct dl_phdr_info * info, size_t size,
    void * user_data);
static HooxPageProtection hoox_page_protection_from_vmentry (hx_int native_prot);

/* ---- thread ------------------------------------------------------------- */

HooxThreadId
hoox_process_get_current_thread_id (void)
{
  return (HooxThreadId) pthread_getthreadid_np ();
}

hx_int
hoox_thread_get_system_error (void)
{
  return errno;
}

void
hoox_thread_set_system_error (hx_int value)
{
  errno = value;
}

hx_boolean
hoox_thread_suspend (HooxThreadId thread_id,
                    HxError ** error)
{
  (void) error;

  return thr_kill ((long) thread_id, SIGSTOP) == 0;
}

hx_boolean
hoox_thread_resume (HooxThreadId thread_id,
                   HxError ** error)
{
  (void) error;

  return thr_kill ((long) thread_id, SIGCONT) == 0;
}

void
_hoox_process_enumerate_threads (HooxFoundThreadFunc func,
                                hx_pointer user_data,
                                HooxThreadFlags flags)
{
  struct kinfo_proc * threads;
  hx_uint n, i;

  (void) flags;

  threads = hoox_query_threads (&n);
  if (threads == NULL)
    return;

  for (i = 0; i != n; i++)
  {
    HooxThreadDetails details;

    memset (&details, 0, sizeof (details));
    details.flags = HOOX_THREAD_FLAGS_NONE;
    details.id = (HooxThreadId) threads[i].ki_tid;

    if (!func (&details, user_data))
      break;
  }

  free (threads);
}

static struct kinfo_proc *
hoox_query_threads (hx_uint * count)
{
  struct kinfo_proc * threads = NULL;
  hx_int mib[4];
  size_t size;

  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_PID | KERN_PROC_INC_THREAD;
  mib[3] = getpid ();

  size = 0;
  if (sysctl (mib, HX_N_ELEMENTS (mib), NULL, &size, NULL, 0) != 0)
    return NULL;

  while (TRUE)
  {
    size_t previous_size;
    hx_boolean still_too_small;

    threads = realloc (threads, size);

    previous_size = size;
    if (sysctl (mib, HX_N_ELEMENTS (mib), threads, &size, NULL, 0) == 0)
      break;

    still_too_small = errno == ENOMEM && size == previous_size;
    if (!still_too_small)
    {
      free (threads);
      return NULL;
    }

    size += size / 10;
  }

  *count = (hx_uint) (size / sizeof (struct kinfo_proc));

  return threads;
}

/* ---- range enumeration (backs the near-allocator) ----------------------- */

void
_hoox_process_enumerate_ranges (HooxPageProtection prot,
                               HooxFoundRangeFunc func,
                               hx_pointer user_data)
{
  hx_int mib[4];
  hx_uint8 * entries = NULL;
  hx_uint8 * cursor, * end;
  size_t size;

  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_VMMAP;
  mib[3] = getpid ();

  size = 0;
  if (sysctl (mib, HX_N_ELEMENTS (mib), NULL, &size, NULL, 0) != 0)
    goto beach;

  while (TRUE)
  {
    size_t previous_size;
    hx_boolean still_too_small;

    entries = realloc (entries, size);

    previous_size = size;
    if (sysctl (mib, HX_N_ELEMENTS (mib), entries, &size, NULL, 0) == 0)
      break;

    still_too_small = errno == ENOMEM && size == previous_size;
    if (!still_too_small)
      goto beach;

    size = size * 4 / 3;
  }

  cursor = entries;
  end = entries + size;

  while (cursor != end)
  {
    struct kinfo_vmentry * e = (struct kinfo_vmentry *) cursor;
    HooxRangeDetails details;
    HooxMemoryRange range;
    HooxFileMapping file;

    if (e->kve_structsize == 0)
      break;

    range.base_address = (HooxAddress) e->kve_start;
    range.size = (hx_size) (e->kve_end - e->kve_start);

    details.range = &range;
    details.protection = hoox_page_protection_from_vmentry (e->kve_protection);
    if (e->kve_type == KVME_TYPE_VNODE)
    {
      file.path = e->kve_path;
      file.offset = e->kve_offset;
      file.size = (hx_size) e->kve_vn_size;

      details.file = &file;
    }
    else
    {
      details.file = NULL;
    }

    if ((details.protection & prot) == prot)
    {
      if (!func (&details, user_data))
        goto beach;
    }

    cursor += e->kve_structsize;
  }

beach:
  free (entries);
}

static HooxPageProtection
hoox_page_protection_from_vmentry (hx_int native_prot)
{
  HooxPageProtection prot = HOOX_PAGE_NO_ACCESS;

  if ((native_prot & KVME_PROT_READ) != 0)
    prot |= HOOX_PAGE_READ;
  if ((native_prot & KVME_PROT_WRITE) != 0)
    prot |= HOOX_PAGE_WRITE;
  if ((native_prot & KVME_PROT_EXEC) != 0)
    prot |= HOOX_PAGE_EXECUTE;

  return prot;
}

/* ---- code signing ------------------------------------------------------- */

HooxCodeSigningPolicy
hoox_process_get_code_signing_policy (void)
{
  return HOOX_CODE_SIGNING_OPTIONAL;
}

HooxOS
hoox_process_get_native_os (void)
{
  return HOOX_OS_FREEBSD;
}

/* ---- module (range shim) ------------------------------------------------
 * hoox_probe_module_for_code_cave (hooxcodeallocator.c) is compiled on the
 * Darwin/ELF32 code-cave path and links against hoox_process_enumerate_modules
 * / hoox_module_get_range. Provide a dl_iterate_phdr-based enumeration so the
 * symbols resolve and the ELF32 near path works. */

struct _HooxModule
{
  hx_char name[HOOX_MAX_PATH];
  hx_char path[HOOX_MAX_PATH];
  HooxMemoryRange range;
};

void
hoox_process_enumerate_modules (HooxFoundModuleFunc func,
                               hx_pointer user_data)
{
  HooxEnumerateModulesContext ctx;

  ctx.func = func;
  ctx.user_data = user_data;
  ctx.carry_on = TRUE;

  dl_iterate_phdr (hoox_emit_module_from_phdr, &ctx);
}

static int
hoox_emit_module_from_phdr (struct dl_phdr_info * info,
                            size_t size,
                            void * user_data)
{
  HooxEnumerateModulesContext * ctx = user_data;
  HooxModule module;
  const hx_char * name;
  const hx_char * basename;
  Elf_Half i;
  hx_boolean found_load;
  Elf_Addr min_vaddr, max_vaddr;

  (void) size;

  memset (&module, 0, sizeof (module));

  name = (info->dlpi_name != NULL) ? info->dlpi_name : "";
  hx_strlcpy (module.path, name, sizeof (module.path));

  basename = strrchr (name, '/');
  basename = (basename != NULL) ? basename + 1 : name;
  hx_strlcpy (module.name, basename, sizeof (module.name));

  found_load = FALSE;
  min_vaddr = 0;
  max_vaddr = 0;

  for (i = 0; i != info->dlpi_phnum; i++)
  {
    const Elf_Phdr * phdr = &info->dlpi_phdr[i];
    Elf_Addr segment_end;

    if (phdr->p_type != PT_LOAD)
      continue;

    segment_end = phdr->p_vaddr + phdr->p_memsz;

    if (!found_load)
    {
      min_vaddr = phdr->p_vaddr;
      max_vaddr = segment_end;
      found_load = TRUE;
    }
    else
    {
      if (phdr->p_vaddr < min_vaddr)
        min_vaddr = phdr->p_vaddr;
      if (segment_end > max_vaddr)
        max_vaddr = segment_end;
    }
  }

  if (found_load)
  {
    module.range.base_address =
        (HooxAddress) (info->dlpi_addr + min_vaddr);
    module.range.size = (hx_size) (max_vaddr - min_vaddr);
  }
  else
  {
    module.range.base_address = (HooxAddress) info->dlpi_addr;
    module.range.size = 0;
  }

  ctx->carry_on = ctx->func (&module, ctx->user_data);

  return ctx->carry_on ? 0 : 1;
}

const HooxMemoryRange *
hoox_module_get_range (HooxModule * self)
{
  return &self->range;
}
