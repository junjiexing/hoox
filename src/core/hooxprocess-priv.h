/*
 * Copyright (C) 2017-2025 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2023 Francesco Tamagni <mrmacete@protonmail.ch>
 * Copyright (C) 2024 Håvard Sørbø <havard@hsorbo.no>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_PROCESS_PRIV_H__
#define __HOOX_PROCESS_PRIV_H__

#include "hooxprocess.h"

HX_BEGIN_DECLS

HX_GNUC_INTERNAL void _hoox_process_enumerate_threads (HooxFoundThreadFunc func,
    hx_pointer user_data, HooxThreadFlags flags);
HX_GNUC_INTERNAL hx_boolean _hoox_process_collect_main_module (HooxModule * module,
    hx_pointer user_data);
HX_GNUC_INTERNAL void _hoox_process_enumerate_ranges (HooxPageProtection prot,
    HooxFoundRangeFunc func, hx_pointer user_data);

#ifdef HAVE_WINDOWS
HX_GNUC_INTERNAL hx_boolean _hoox_windows_is_win7_wow64 (void);
#else
# define _hoox_windows_is_win7_wow64() FALSE
#endif

#if defined (HAVE_I386)
HX_GNUC_INTERNAL void _hoox_x86_set_breakpoint (hx_size * dr7, hx_size * dr0,
    hx_uint breakpoint_id, HooxAddress address);
HX_GNUC_INTERNAL void _hoox_x86_unset_breakpoint (hx_size * dr7, hx_size * dr0,
    hx_uint breakpoint_id);
HX_GNUC_INTERNAL void _hoox_x86_set_watchpoint (hx_size * dr7, hx_size * dr0,
    hx_uint watchpoint_id, HooxAddress address, hx_size size,
    HooxWatchConditions conditions);
HX_GNUC_INTERNAL void _hoox_x86_unset_watchpoint (hx_size * dr7, hx_size * dr0,
    hx_uint watchpoint_id);
#elif defined (HAVE_ARM)
HX_GNUC_INTERNAL void _hoox_arm_set_breakpoint (hx_uint32 * bcr, hx_uint32 * bvr,
    hx_uint breakpoint_id, HooxAddress address);
HX_GNUC_INTERNAL void _hoox_arm_unset_breakpoint (hx_uint32 * bcr, hx_uint32 * bvr,
    hx_uint breakpoint_id);
HX_GNUC_INTERNAL void _hoox_arm_set_watchpoint (hx_uint32 * wcr, hx_uint32 * wvr,
    hx_uint watchpoint_id, HooxAddress address, hx_size size,
    HooxWatchConditions conditions);
HX_GNUC_INTERNAL void _hoox_arm_unset_watchpoint (hx_uint32 * wcr, hx_uint32 * wvr,
    hx_uint watchpoint_id);
#elif defined (HAVE_ARM64)
# if defined (HAVE_WINDOWS)
typedef hx_uint32 HooxArm64CtrlReg;
# else
typedef hx_uint64 HooxArm64CtrlReg;
# endif
HX_GNUC_INTERNAL void _hoox_arm64_set_breakpoint (HooxArm64CtrlReg * bcr,
    hx_uint64 * bvr, hx_uint breakpoint_id, HooxAddress address);
HX_GNUC_INTERNAL void _hoox_arm64_unset_breakpoint (HooxArm64CtrlReg * bcr,
    hx_uint64 * bvr, hx_uint breakpoint_id);
HX_GNUC_INTERNAL void _hoox_arm64_set_watchpoint (HooxArm64CtrlReg * wcr,
    hx_uint64 * wvr, hx_uint watchpoint_id, HooxAddress address, hx_size size,
    HooxWatchConditions conditions);
HX_GNUC_INTERNAL void _hoox_arm64_unset_watchpoint (HooxArm64CtrlReg * wcr,
    hx_uint64 * wvr, hx_uint watchpoint_id);
#endif

HX_END_DECLS

#endif
