/*
 * Copyright (C) 2008-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2020-2024 Francesco Tamagni <mrmacete@protonmail.ch>
 * Copyright (C) 2023 Grant Douglas <me@hexplo.it>
 * Copyright (C) 2024 Håvard Sørbø <havard@hsorbo.no>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_PROCESS_H__
#define __HOOX_PROCESS_H__

#include "hooxmemory.h"
#include "hooxmodule.h"

#define HOOX_THREAD_ID_INVALID ((HooxThreadId) -1)
#define HOOX_TYPE_THREAD_DETAILS (hoox_thread_details_get_type ())

HX_BEGIN_DECLS

typedef hx_uint HooxProcessId;
typedef hx_size HooxThreadId;
typedef struct _HooxThreadDetails HooxThreadDetails;
typedef struct _HooxThreadEntrypoint HooxThreadEntrypoint;
typedef struct _HooxMallocRangeDetails HooxMallocRangeDetails;

typedef enum {
  HOOX_TEARDOWN_REQUIREMENT_FULL,
  HOOX_TEARDOWN_REQUIREMENT_MINIMAL
} HooxTeardownRequirement;

typedef enum {
  HOOX_CODE_SIGNING_OPTIONAL,
  HOOX_CODE_SIGNING_REQUIRED
} HooxCodeSigningPolicy;

typedef enum {
  HOOX_MODIFY_THREAD_FLAGS_NONE         = 0,
  HOOX_MODIFY_THREAD_FLAGS_ABORT_SAFELY = (1 << 0),
} HooxModifyThreadFlags;

typedef enum {
  HOOX_THREAD_FLAGS_NAME                 = (1 << 0),
  HOOX_THREAD_FLAGS_STATE                = (1 << 1),
  HOOX_THREAD_FLAGS_CPU_CONTEXT          = (1 << 2),
  HOOX_THREAD_FLAGS_ENTRYPOINT_ROUTINE   = (1 << 3),
  HOOX_THREAD_FLAGS_ENTRYPOINT_PARAMETER = (1 << 4),

  HOOX_THREAD_FLAGS_NONE                 = 0,
  HOOX_THREAD_FLAGS_ALL                  = HOOX_THREAD_FLAGS_NAME |
                                          HOOX_THREAD_FLAGS_STATE |
                                          HOOX_THREAD_FLAGS_CPU_CONTEXT |
                                          HOOX_THREAD_FLAGS_ENTRYPOINT_ROUTINE |
                                          HOOX_THREAD_FLAGS_ENTRYPOINT_PARAMETER,
} HooxThreadFlags;

typedef enum {
  HOOX_THREAD_RUNNING = 1,
  HOOX_THREAD_STOPPED,
  HOOX_THREAD_WAITING,
  HOOX_THREAD_UNINTERRUPTIBLE,
  HOOX_THREAD_HALTED
} HooxThreadState;

struct _HooxThreadEntrypoint
{
  HooxAddress routine;
  HooxAddress parameter;
};

struct _HooxThreadDetails
{
  HooxThreadFlags flags;
  HooxThreadId id;
  const hx_char * name;
  HooxThreadState state;
  HooxCpuContext cpu_context;
  HooxThreadEntrypoint entrypoint;
};

typedef enum {
  HOOX_WATCH_READ  = (1 << 0),
  HOOX_WATCH_WRITE = (1 << 1),
} HooxWatchConditions;

struct _HooxMallocRangeDetails
{
  const HooxMemoryRange * range;
};

typedef void (* HooxModifyThreadFunc) (HooxThreadId thread_id,
    HooxCpuContext * cpu_context, hx_pointer user_data);
typedef hx_boolean (* HooxFoundThreadFunc) (const HooxThreadDetails * details,
    hx_pointer user_data);
typedef hx_boolean (* HooxFoundModuleFunc) (HooxModule * module,
    hx_pointer user_data);
typedef hx_boolean (* HooxFoundMallocRangeFunc) (
    const HooxMallocRangeDetails * details, hx_pointer user_data);

HOOX_API HooxOS hoox_process_get_native_os (void);
HOOX_API HooxTeardownRequirement hoox_process_get_teardown_requirement (void);
HOOX_API void hoox_process_set_teardown_requirement (
    HooxTeardownRequirement requirement);
HOOX_API HooxCodeSigningPolicy hoox_process_get_code_signing_policy (void);
HOOX_API void hoox_process_set_code_signing_policy (HooxCodeSigningPolicy policy);
HOOX_API hx_boolean hoox_process_is_debugger_attached (void);
HOOX_API HooxProcessId hoox_process_get_id (void);
HOOX_API HooxThreadId hoox_process_get_current_thread_id (void);
HOOX_API hx_boolean hoox_process_has_thread (HooxThreadId thread_id);
HOOX_API HooxThreadDetails * hoox_process_find_thread_by_id (HooxThreadId thread_id,
    HooxThreadFlags flags);
HOOX_API hx_boolean hoox_process_modify_thread (HooxThreadId thread_id,
    HooxModifyThreadFunc func, hx_pointer user_data, HooxModifyThreadFlags flags);
HOOX_API void hoox_process_enumerate_threads (HooxFoundThreadFunc func,
    hx_pointer user_data, HooxThreadFlags flags);
HOOX_API HooxModule * hoox_process_get_main_module (void);
HOOX_API HooxModule * hoox_process_get_libc_module (void);
HOOX_API HooxModule * hoox_process_find_module_by_name (const hx_char * name);
HOOX_API HooxModule * hoox_process_find_module_by_address (HooxAddress address);
HOOX_API hx_boolean hoox_process_find_function_range (hx_constpointer address,
    HooxMemoryRange * range);
HOOX_API void hoox_process_enumerate_modules (HooxFoundModuleFunc func,
    hx_pointer user_data);
HOOX_API void hoox_process_enumerate_ranges (HooxPageProtection prot,
    HooxFoundRangeFunc func, hx_pointer user_data);
HOOX_API void hoox_process_enumerate_malloc_ranges (
    HooxFoundMallocRangeFunc func, hx_pointer user_data);
HOOX_API hx_uint hoox_thread_try_get_ranges (HooxMemoryRange * ranges,
    hx_uint max_length);
HOOX_API hx_int hoox_thread_get_system_error (void);
HOOX_API void hoox_thread_set_system_error (hx_int value);
HOOX_API hx_boolean hoox_thread_suspend (HooxThreadId thread_id, HxError ** error);
HOOX_API hx_boolean hoox_thread_resume (HooxThreadId thread_id, HxError ** error);
HOOX_API hx_boolean hoox_thread_set_hardware_breakpoint (HooxThreadId thread_id,
    hx_uint breakpoint_id, HooxAddress address, HxError ** error);
HOOX_API hx_boolean hoox_thread_unset_hardware_breakpoint (HooxThreadId thread_id,
    hx_uint breakpoint_id, HxError ** error);
HOOX_API hx_boolean hoox_thread_set_hardware_watchpoint (HooxThreadId thread_id,
    hx_uint watchpoint_id, HooxAddress address, hx_size size, HooxWatchConditions wc,
    HxError ** error);
HOOX_API hx_boolean hoox_thread_unset_hardware_watchpoint (HooxThreadId thread_id,
    hx_uint watchpoint_id, HxError ** error);

HOOX_API const hx_char * hoox_code_signing_policy_to_string (
    HooxCodeSigningPolicy policy);

HOOX_API HxType hoox_thread_details_get_type (void) HX_GNUC_CONST;
HOOX_API HooxThreadDetails * hoox_thread_details_copy (
    const HooxThreadDetails * details);
HOOX_API void hoox_thread_details_free (HooxThreadDetails * details);

HX_END_DECLS

#endif
