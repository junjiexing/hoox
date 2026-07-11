/*
 * Copyright (C) 2015-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * hoox — Darwin (macOS) process/thread/range/module shim.
 *
 * A thin replacement for the slice of hooxprocess the hook engine needs,
 * avoiding the full frida hooxprocess.c tree. On macOS x86_64 the RWX path is
 * taken at runtime (hoox_query_rwx_support == HOOX_RWX_FULL), so thread
 * enumeration / suspend / resume are *link* requirements rather than hook
 * hot-path. Range enumeration (via the mach VM map) backs the near-allocator
 * used to place trampolines within reach of the target; module enumeration
 * (via dyld) backs the Darwin code-cave deflector path.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxprocess.h"
#include "hooxprocess-priv.h"
#include "hooxmodule.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

#if HX_SIZEOF_VOID_P == 8
typedef struct mach_header_64 HooxMachHeader;
typedef struct segment_command_64 HooxSegmentCommand;
# define HOOX_LC_SEGMENT LC_SEGMENT_64
#else
typedef struct mach_header HooxMachHeader;
typedef struct segment_command HooxSegmentCommand;
# define HOOX_LC_SEGMENT LC_SEGMENT
#endif

struct _HooxModule
{
  hx_char name[HOOX_MAX_PATH];
  hx_char path[HOOX_MAX_PATH];
  HooxMemoryRange range;
};

static HooxPageProtection hoox_process_page_protection_from_mach (
    vm_prot_t native_prot);
static hx_boolean hoox_thread_act_for_id (HooxThreadId thread_id,
    thread_act_t * result);

/* ---- thread ------------------------------------------------------------- */

HooxThreadId
hoox_process_get_current_thread_id (void)
{
  uint64_t tid = 0;

  pthread_threadid_np (NULL, &tid);

  return (HooxThreadId) tid;
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
  thread_act_t thread;
  kern_return_t kr;

  (void) error;

  if (!hoox_thread_act_for_id (thread_id, &thread))
    return FALSE;

  kr = thread_suspend (thread);

  mach_port_deallocate (mach_task_self (), thread);

  return kr == KERN_SUCCESS;
}

hx_boolean
hoox_thread_resume (HooxThreadId thread_id,
                   HxError ** error)
{
  thread_act_t thread;
  kern_return_t kr;

  (void) error;

  if (!hoox_thread_act_for_id (thread_id, &thread))
    return FALSE;

  kr = thread_resume (thread);

  mach_port_deallocate (mach_task_self (), thread);

  return kr == KERN_SUCCESS;
}

static hx_boolean
hoox_thread_act_for_id (HooxThreadId thread_id,
                       thread_act_t * result)
{
  mach_port_t task;
  thread_act_array_t threads;
  mach_msg_type_number_t count, i;
  hx_boolean found;

  task = mach_task_self ();

  if (task_threads (task, &threads, &count) != KERN_SUCCESS)
    return FALSE;

  found = FALSE;

  for (i = 0; i != count; i++)
  {
    thread_identifier_info_data_t id_info;
    mach_msg_type_number_t id_count = THREAD_IDENTIFIER_INFO_COUNT;

    if (!found &&
        thread_info (threads[i], THREAD_IDENTIFIER_INFO,
            (thread_info_t) &id_info, &id_count) == KERN_SUCCESS &&
        (HooxThreadId) id_info.thread_id == thread_id)
    {
      *result = threads[i];
      found = TRUE;
    }
    else
    {
      mach_port_deallocate (task, threads[i]);
    }
  }

  vm_deallocate (task, (vm_address_t) threads, count * sizeof (thread_t));

  return found;
}

void
_hoox_process_enumerate_threads (HooxFoundThreadFunc func,
                                hx_pointer user_data,
                                HooxThreadFlags flags)
{
  mach_port_t task;
  thread_act_array_t threads;
  mach_msg_type_number_t count, i;
  hx_boolean carry_on = TRUE;

  (void) flags;

  task = mach_task_self ();

  if (task_threads (task, &threads, &count) != KERN_SUCCESS)
    return;

  for (i = 0; i != count; i++)
  {
    thread_identifier_info_data_t id_info;
    mach_msg_type_number_t id_count = THREAD_IDENTIFIER_INFO_COUNT;

    if (carry_on &&
        thread_info (threads[i], THREAD_IDENTIFIER_INFO,
            (thread_info_t) &id_info, &id_count) == KERN_SUCCESS)
    {
      HooxThreadDetails details;

      memset (&details, 0, sizeof (details));
      details.flags = HOOX_THREAD_FLAGS_NONE;
      details.id = (HooxThreadId) id_info.thread_id;

      carry_on = func (&details, user_data);
    }

    mach_port_deallocate (task, threads[i]);
  }

  vm_deallocate (task, (vm_address_t) threads, count * sizeof (thread_t));
}

/* ---- range enumeration (backs the near-allocator) ----------------------- */

void
_hoox_process_enumerate_ranges (HooxPageProtection prot,
                               HooxFoundRangeFunc func,
                               hx_pointer user_data)
{
  mach_port_t task;
  mach_vm_address_t address;

  task = mach_task_self ();
  address = 0;

  while (TRUE)
  {
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t info_count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object_name = MACH_PORT_NULL;
    kern_return_t kr;
    HooxPageProtection cur_prot;

    kr = mach_vm_region (task, &address, &size, VM_REGION_BASIC_INFO_64,
        (vm_region_info_t) &info, &info_count, &object_name);
    if (object_name != MACH_PORT_NULL)
      mach_port_deallocate (task, object_name);
    if (kr != KERN_SUCCESS)
      break;

    cur_prot = hoox_process_page_protection_from_mach (info.protection);

    if ((cur_prot & prot) == prot)
    {
      HooxMemoryRange range;
      HooxRangeDetails details;

      range.base_address = (HooxAddress) address;
      range.size = (hx_size) size;

      details.range = &range;
      details.protection = cur_prot;
      details.file = NULL;

      if (!func (&details, user_data))
        break;
    }

    address += size;
  }
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
  return HOOX_OS_MACOS;
}

/* ---- module enumeration (via dyld) --------------------------------------
 * Backs hoox_probe_module_for_code_cave (hooxcodeallocator.c), which is
 * compiled on Darwin and reads only the module's __TEXT range. */

void
hoox_process_enumerate_modules (HooxFoundModuleFunc func,
                               hx_pointer user_data)
{
  hx_uint count, i;

  count = (hx_uint) _dyld_image_count ();

  for (i = 0; i != count; i++)
  {
    const HooxMachHeader * header;
    const hx_char * image_name;
    intptr_t slide;
    HooxModule module;
    const hx_char * basename;
    const struct load_command * lc;
    hx_uint32 j;
    hx_boolean found_text;

    header = (const HooxMachHeader *) _dyld_get_image_header (i);
    if (header == NULL)
      continue;

    image_name = _dyld_get_image_name (i);
    slide = _dyld_get_image_vmaddr_slide (i);

    memset (&module, 0, sizeof (module));

    if (image_name != NULL)
    {
      hx_strlcpy (module.path, image_name, sizeof (module.path));

      basename = strrchr (image_name, '/');
      basename = (basename != NULL) ? basename + 1 : image_name;
      hx_strlcpy (module.name, basename, sizeof (module.name));
    }

    found_text = FALSE;
    lc = (const struct load_command *) (header + 1);

    for (j = 0; j != header->ncmds; j++)
    {
      if (lc->cmd == HOOX_LC_SEGMENT)
      {
        const HooxSegmentCommand * seg = (const HooxSegmentCommand *) lc;

        if (strcmp (seg->segname, SEG_TEXT) == 0)
        {
          module.range.base_address =
              (HooxAddress) (seg->vmaddr + (hx_size) slide);
          module.range.size = (hx_size) seg->vmsize;
          found_text = TRUE;
          break;
        }
      }

      lc = (const struct load_command *) ((const hx_uint8 *) lc + lc->cmdsize);
    }

    if (!found_text)
    {
      module.range.base_address = (HooxAddress) (hx_uintptr) header;
      module.range.size = 0;
    }

    if (!func (&module, user_data))
      break;
  }
}

const HooxMemoryRange *
hoox_module_get_range (HooxModule * self)
{
  return &self->range;
}

static HooxPageProtection
hoox_process_page_protection_from_mach (vm_prot_t native_prot)
{
  HooxPageProtection prot = HOOX_PAGE_NO_ACCESS;

  if ((native_prot & VM_PROT_READ) == VM_PROT_READ)
    prot |= HOOX_PAGE_READ;
  if ((native_prot & VM_PROT_WRITE) == VM_PROT_WRITE)
    prot |= HOOX_PAGE_WRITE;
  if ((native_prot & VM_PROT_EXECUTE) == VM_PROT_EXECUTE)
    prot |= HOOX_PAGE_EXECUTE;

  return prot;
}
