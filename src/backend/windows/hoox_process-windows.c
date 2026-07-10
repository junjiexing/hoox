/*
 * hoox — Windows process/thread/module shim.
 *
 * A thin replacement for the slice of gumprocess/gummodule the hook engine
 * needs, avoiding the full frida gumprocess.c tree. See PLAN.md 2.1: on
 * Windows x64 the thread-enumeration / suspend path is a *link* requirement
 * (patch_code takes the RWX path at runtime), while the module range is used
 * by the tests.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "gumprocess.h"
#include "gumprocess-priv.h"
#include "gummodule.h"

#ifndef WIN32_LEAN_AND_MEAN
# define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>

/* ---- thread ------------------------------------------------------------- */

GumThreadId
gum_process_get_current_thread_id (void)
{
  return (GumThreadId) GetCurrentThreadId ();
}

gint
gum_thread_get_system_error (void)
{
  return (gint) GetLastError ();
}

void
gum_thread_set_system_error (gint value)
{
  SetLastError ((DWORD) value);
}

gboolean
gum_thread_suspend (GumThreadId thread_id,
                    GError ** error)
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

gboolean
gum_thread_resume (GumThreadId thread_id,
                   GError ** error)
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
_gum_process_enumerate_threads (GumFoundThreadFunc func,
                                gpointer user_data,
                                GumThreadFlags flags)
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
      GumThreadDetails details;

      if (entry.th32OwnerProcessID != pid)
        continue;

      memset (&details, 0, sizeof (details));
      details.flags = GUM_THREAD_FLAGS_NONE;
      details.id = (GumThreadId) entry.th32ThreadID;

      if (!func (&details, user_data))
        break;
    }
    while (Thread32Next (snapshot, &entry));
  }

  CloseHandle (snapshot);
  (void) flags;
}

/* ---- code signing ------------------------------------------------------- */

static GumCodeSigningPolicy gum_code_signing_policy = GUM_CODE_SIGNING_OPTIONAL;

GumCodeSigningPolicy
gum_process_get_code_signing_policy (void)
{
  return gum_code_signing_policy;
}

void
gum_process_set_code_signing_policy (GumCodeSigningPolicy policy)
{
  gum_code_signing_policy = policy;
}

/* ---- module (main-module range for tests) ------------------------------- */

struct _GumModule
{
  gchar name[MAX_PATH];
  gchar path[MAX_PATH];
  GumMemoryRange range;
  gboolean initialized;
};

static GumModule gum_main_module;

static GumModule *
gum_main_module_get (void)
{
  GumModule * m = &gum_main_module;
  HMODULE handle;
  MODULEINFO info;
  DWORD len;

  if (m->initialized)
    return m;

  handle = GetModuleHandleW (NULL);

  len = GetModuleFileNameA (handle, m->path, sizeof (m->path));
  if (len != 0)
  {
    const char * slash = m->path;
    const char * p;
    for (p = m->path; *p != '\0'; p++)
    {
      if (*p == '\\' || *p == '/')
        slash = p + 1;
    }
    strncpy (m->name, slash, sizeof (m->name) - 1);
  }

  if (GetModuleInformation (GetCurrentProcess (), handle, &info,
      sizeof (info)))
  {
    m->range.base_address = GUM_ADDRESS (info.lpBaseOfDll);
    m->range.size = info.SizeOfImage;
  }

  m->initialized = TRUE;
  return m;
}

GumModule *
gum_process_get_main_module (void)
{
  return gum_main_module_get ();
}

const gchar *
gum_module_get_name (GumModule * self)
{
  return self->name;
}

const gchar *
gum_module_get_path (GumModule * self)
{
  return self->path;
}

const GumMemoryRange *
gum_module_get_range (GumModule * self)
{
  return &self->range;
}

GumAddress
gum_module_find_export_by_name (GumModule * self,
                                const gchar * symbol_name)
{
  (void) self;
  return GUM_ADDRESS (GetProcAddress (GetModuleHandleW (NULL), symbol_name));
}
