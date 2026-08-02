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
  DWORD previous_count;

  (void) error;

  thread = OpenThread (THREAD_SUSPEND_RESUME, FALSE, (DWORD) thread_id);
  if (thread == NULL)
    return FALSE;

  previous_count = SuspendThread (thread);
  CloseHandle (thread);

  return previous_count != (DWORD) -1;
}

hx_boolean
hoox_thread_resume (HooxThreadId thread_id,
                   HxError ** error)
{
  HANDLE thread;
  DWORD previous_count;

  (void) error;

  thread = OpenThread (THREAD_SUSPEND_RESUME, FALSE, (DWORD) thread_id);
  if (thread == NULL)
    return FALSE;

  previous_count = ResumeThread (thread);
  CloseHandle (thread);

  return previous_count != (DWORD) -1;
}

hx_boolean
_hoox_windows_suspend_thread (HooxThreadId thread_id,
                              hx_pointer * thread_handle)
{
  DWORD desired_access;
  HANDLE thread;
  DWORD previous_count;

  desired_access = THREAD_SUSPEND_RESUME;
#ifdef HOOX_WINDOWS_PATCH_PC_GUARD
  desired_access |= THREAD_GET_CONTEXT;
#endif

  thread = OpenThread (desired_access, FALSE, (DWORD) thread_id);
  if (thread == NULL)
    return FALSE;

  previous_count = SuspendThread (thread);
  if (previous_count == (DWORD) -1)
  {
    CloseHandle (thread);
    return FALSE;
  }

  *thread_handle = thread;

  return TRUE;
}

hx_boolean
_hoox_windows_resume_thread (hx_pointer thread_handle)
{
  return ResumeThread ((HANDLE) thread_handle) != (DWORD) -1;
}

void
_hoox_windows_close_thread (hx_pointer thread_handle)
{
  CloseHandle ((HANDLE) thread_handle);
}

#ifdef HOOX_WINDOWS_PATCH_PC_GUARD

hx_boolean
_hoox_windows_query_thread_ip (hx_pointer thread_handle,
                              hx_pointer * instruction_pointer)
{
  HANDLE thread = (HANDLE) thread_handle;
  CONTEXT context;

  memset (&context, 0, sizeof (context));
  context.ContextFlags = CONTEXT_CONTROL;
  if (!GetThreadContext (thread, &context))
    return FALSE;

#if defined (_M_X64) || defined (_M_AMD64) || defined (__x86_64__)
  *instruction_pointer = (hx_pointer) context.Rip;
#elif defined (_M_ARM64) || defined (__aarch64__)
  *instruction_pointer = (hx_pointer) context.Pc;
#elif defined (_M_ARM) || defined (__arm__)
  *instruction_pointer = (hx_pointer) (hx_size) context.Pc;
#elif defined (_M_IX86) || defined (__i386__)
  *instruction_pointer = (hx_pointer) (hx_size) context.Eip;
#else
# error Unsupported Windows architecture
#endif

  return TRUE;
}

#endif

void
_hoox_windows_sleep_ms (hx_uint milliseconds)
{
  Sleep ((DWORD) milliseconds);
}

hx_boolean
_hoox_windows_enumerate_threads (HooxFoundThreadFunc func,
                                hx_pointer user_data,
                                HooxThreadFlags flags)
{
  DWORD pid = GetCurrentProcessId ();
  HANDLE snapshot;
  THREADENTRY32 entry;
  hx_boolean success = TRUE;

  snapshot = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE)
    return FALSE;

  entry.dwSize = sizeof (entry);
  if (!Thread32First (snapshot, &entry))
  {
    CloseHandle (snapshot);
    return FALSE;
  }

  while (TRUE)
  {
    if (entry.th32OwnerProcessID == pid)
    {
      HooxThreadDetails details;

      memset (&details, 0, sizeof (details));
      details.flags = HOOX_THREAD_FLAGS_NONE;
      details.id = (HooxThreadId) entry.th32ThreadID;

      if (!func (&details, user_data))
      {
        success = FALSE;
        break;
      }
    }

    SetLastError (ERROR_SUCCESS);
    if (!Thread32Next (snapshot, &entry))
    {
      if (GetLastError () != ERROR_NO_MORE_FILES)
        success = FALSE;
      break;
    }
  }

  CloseHandle (snapshot);
  (void) flags;

  return success;
}

void
_hoox_process_enumerate_threads (HooxFoundThreadFunc func,
                                hx_pointer user_data,
                                HooxThreadFlags flags)
{
  (void) _hoox_windows_enumerate_threads (func, user_data, flags);
}

HooxOS
hoox_process_get_native_os (void)
{
  return HOOX_OS_WINDOWS;
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
