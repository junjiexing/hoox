/*
 * hoox — Linux process/thread/range shim.
 *
 * A thin replacement for the slice of hooxprocess the hook engine needs,
 * avoiding the full frida hooxprocess.c tree. See PLAN.md 2.1 / TASKS.md T8.1:
 * on Linux the RWX path is taken at runtime (hoox_query_rwx_support ==
 * HOOX_RWX_FULL), so thread enumeration / suspend / resume are *link*
 * requirements rather than hook hot-path. Range enumeration (via
 * /proc/self/maps) backs the near-allocator used to place trampolines within
 * reach of the target.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxprocess.h"
#include "hooxprocess-priv.h"
#include "hooxmodule.h"

#include "hooxlinux-priv.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#ifndef O_CLOEXEC
# define O_CLOEXEC 02000000
#endif

static HooxPageProtection hoox_page_protection_from_proc_perms_string (
    const hx_char * perms);

/* ---- /proc/self/maps iterator ------------------------------------------- */

static void
hoox_proc_maps_iter_init_for_path (HooxProcMapsIter * iter,
                                  const hx_char * path)
{
  iter->fd = open (path, O_RDONLY | O_CLOEXEC);
  iter->read_cursor = iter->buffer;
  iter->write_cursor = iter->buffer;
}

void
_hoox_proc_maps_iter_init_for_self (HooxProcMapsIter * iter)
{
  hoox_proc_maps_iter_init_for_path (iter, "/proc/self/maps");
}

void
_hoox_proc_maps_iter_destroy (HooxProcMapsIter * iter)
{
  if (iter->fd != -1)
    close (iter->fd);
}

hx_boolean
_hoox_proc_maps_iter_next (HooxProcMapsIter * iter,
                          const hx_char ** line)
{
  hx_char * next_newline;
  hx_uint available;
  hx_boolean need_refill;

  if (iter->fd == -1)
    return FALSE;

  next_newline = NULL;

  available = (hx_uint) (iter->write_cursor - iter->read_cursor);
  if (available == 0)
  {
    need_refill = TRUE;
  }
  else
  {
    next_newline = strchr (iter->read_cursor, '\n');
    need_refill = (next_newline == NULL);
  }

  if (need_refill)
  {
    hx_uint offset;
    ssize_t res;

    offset = (hx_uint) (iter->read_cursor - iter->buffer);
    if (offset > 0)
    {
      memmove (iter->buffer, iter->read_cursor, available);
      iter->read_cursor -= offset;
      iter->write_cursor -= offset;
    }

    do
    {
      res = read (iter->fd, iter->write_cursor,
          iter->buffer + sizeof (iter->buffer) - 1 - iter->write_cursor);
    }
    while (res == -1 && errno == EINTR);

    if (res <= 0)
      return FALSE;

    iter->write_cursor += res;
    iter->write_cursor[0] = '\0';

    next_newline = strchr (iter->read_cursor, '\n');
    if (next_newline == NULL)
      return FALSE;
  }

  *line = iter->read_cursor;
  *next_newline = '\0';

  iter->read_cursor = next_newline + 1;

  return TRUE;
}

/* ---- thread ------------------------------------------------------------- */

HooxThreadId
hoox_process_get_current_thread_id (void)
{
  return (HooxThreadId) syscall (__NR_gettid);
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

  return syscall (__NR_tgkill, getpid (), (int) thread_id, SIGSTOP) == 0;
}

hx_boolean
hoox_thread_resume (HooxThreadId thread_id,
                   HxError ** error)
{
  (void) error;

  return syscall (__NR_tgkill, getpid (), (int) thread_id, SIGCONT) == 0;
}

void
_hoox_process_enumerate_threads (HooxFoundThreadFunc func,
                                hx_pointer user_data,
                                HooxThreadFlags flags)
{
  DIR * dir;
  struct dirent * entry;

  (void) flags;

  dir = opendir ("/proc/self/task");
  if (dir == NULL)
    return;

  while ((entry = readdir (dir)) != NULL)
  {
    HooxThreadDetails details;
    long tid;
    hx_char * end;

    if (entry->d_name[0] < '0' || entry->d_name[0] > '9')
      continue;

    tid = strtol (entry->d_name, &end, 10);
    if (*end != '\0')
      continue;

    memset (&details, 0, sizeof (details));
    details.flags = HOOX_THREAD_FLAGS_NONE;
    details.id = (HooxThreadId) tid;

    if (!func (&details, user_data))
      break;
  }

  closedir (dir);
}

/* ---- range enumeration (backs the near-allocator) ----------------------- */

void
_hoox_process_enumerate_ranges (HooxPageProtection prot,
                               HooxFoundRangeFunc func,
                               hx_pointer user_data)
{
  HooxProcMapsIter iter;
  hx_boolean carry_on = TRUE;
  const hx_char * line;

  _hoox_proc_maps_iter_init_for_self (&iter);

  while (carry_on && _hoox_proc_maps_iter_next (&iter, &line))
  {
    HooxRangeDetails details;
    HooxMemoryRange range;
    unsigned long long start = 0, end = 0;
    hx_char perms[5] = { 0, };

    sscanf (line, "%llx-%llx %4c ", &start, &end, perms);

    range.base_address = (HooxAddress) start;
    range.size = (hx_size) (end - start);

    details.range = &range;
    details.protection = hoox_page_protection_from_proc_perms_string (perms);
    details.file = NULL;

    if ((details.protection & prot) == prot)
      carry_on = func (&details, user_data);
  }

  _hoox_proc_maps_iter_destroy (&iter);
}

static HooxPageProtection
hoox_page_protection_from_proc_perms_string (const hx_char * perms)
{
  HooxPageProtection prot = HOOX_PAGE_NO_ACCESS;

  if (perms[0] == 'r')
    prot |= HOOX_PAGE_READ;
  if (perms[1] == 'w')
    prot |= HOOX_PAGE_WRITE;
  if (perms[2] == 'x')
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
  return HOOX_OS_LINUX;
}

/* ---- module (range shim) ------------------------------------------------
 * hoox_probe_module_for_code_cave (hooxcodeallocator.c) is compiled on every
 * platform and links against hoox_module_get_range, even though the code-cave
 * path itself is Darwin/ELF32-only and never runs on Linux x86/x64. Provide the
 * minimal definition so the symbol resolves. */

struct _HooxModule
{
  hx_char name[HOOX_MAX_PATH];
  hx_char path[HOOX_MAX_PATH];
  HooxMemoryRange range;
};

const HooxMemoryRange *
hoox_module_get_range (HooxModule * self)
{
  return &self->range;
}
