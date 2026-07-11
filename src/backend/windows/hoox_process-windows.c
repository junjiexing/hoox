/*
 * hoox — Windows process/thread/module shim.
 *
 * A thin replacement for the slice of hooxprocess/hooxmodule the hook engine
 * needs, avoiding the full frida hooxprocess.c tree. See PLAN.md 2.1: on
 * Windows x64 the thread-enumeration / suspend path is a *link* requirement
 * (patch_code takes the RWX path at runtime), while the module range is used
 * by the tests.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxprocess.h"
#include "hooxprocess-priv.h"
#include "hooxmodule.h"

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

/* ---- thread ------------------------------------------------------------- */

HooxThreadId
hoox_process_get_current_thread_id (void)
{
  return (HooxThreadId) GetCurrentThreadId ();
}

hx_int
hoox_thread_get_system_error (void)
{
  return (hx_int) GetLastError ();
}

void
hoox_thread_set_system_error (hx_int value)
{
  SetLastError ((DWORD) value);
}

hx_boolean
hoox_thread_suspend (HooxThreadId thread_id,
                    HxError ** error)
{
  HANDLE thread;

  (void) error;

  thread = OpenThread (THREAD_SUSPEND_RESUME, FALSE, (DWORD) thread_id);
  if (thread == NULL)
    return FALSE;

  SuspendThread (thread);
  CloseHandle (thread);

  return TRUE;
}

hx_boolean
hoox_thread_resume (HooxThreadId thread_id,
                   HxError ** error)
{
  HANDLE thread;

  (void) error;

  thread = OpenThread (THREAD_SUSPEND_RESUME, FALSE, (DWORD) thread_id);
  if (thread == NULL)
    return FALSE;

  ResumeThread (thread);
  CloseHandle (thread);

  return TRUE;
}

void
_hoox_process_enumerate_threads (HooxFoundThreadFunc func,
                                hx_pointer user_data,
                                HooxThreadFlags flags)
{
  DWORD pid = GetCurrentProcessId ();
  HANDLE snapshot;
  THREADENTRY32 entry;

  snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return;

  entry.dwSize = sizeof (entry);
  if (Thread32First (snapshot, &entry))
  {
    do
    {
      HooxThreadDetails details;

      if (entry.th32OwnerProcessID != pid)
        continue;

      memset (&details, 0, sizeof (details));
      details.flags = HOOX_THREAD_FLAGS_NONE;
      details.id = (HooxThreadId) entry.th32ThreadID;

      if (!func (&details, user_data))
        break;
    }
    while (Thread32Next (snapshot, &entry));
  }

  CloseHandle (snapshot);
  (void) flags;
}

/* ---- code signing ------------------------------------------------------- */

static HooxCodeSigningPolicy hoox_code_signing_policy = HOOX_CODE_SIGNING_OPTIONAL;

HooxCodeSigningPolicy
hoox_process_get_code_signing_policy (void)
{
  return hoox_code_signing_policy;
}

/* ---- module (main-module range for tests) ------------------------------- */

struct _HooxModule
{
  hx_char name[MAX_PATH];
  hx_char path[MAX_PATH];
  HooxMemoryRange range;
  hx_boolean initialized;
};

static HooxModule hoox_main_module;

const HooxMemoryRange *
hoox_module_get_range (HooxModule * self)
{
  return &self->range;
}
