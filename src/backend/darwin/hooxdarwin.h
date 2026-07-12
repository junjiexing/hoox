/*
 * Copyright (C) 2010-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_DARWIN_H__
#define __HOOX_DARWIN_H__

#include <TargetConditionals.h>
#include <mach/mach.h>
#include <mach/vm_region.h>

/*
 * The public iOS / tvOS SDK #error-gates <mach/mach_vm.h>, yet the mach_vm_*
 * symbols are present in libsystem at runtime. So include the SDK header on
 * macOS, and declare the subset hoox actually uses everywhere else (device
 * and simulator). This mirrors frida-gum's approach.
 */
#if TARGET_OS_OSX
# include <mach/mach_vm.h>
#else
kern_return_t mach_vm_protect (vm_map_t target_task, mach_vm_address_t address,
    mach_vm_size_t size, boolean_t set_maximum, vm_prot_t new_protection);
kern_return_t mach_vm_region (vm_map_t target_task, mach_vm_address_t * address,
    mach_vm_size_t * size, vm_region_flavor_t flavor, vm_region_info_t info,
    mach_msg_type_number_t * info_cnt, mach_port_t * object_name);
kern_return_t mach_vm_remap (vm_map_t target_task,
    mach_vm_address_t * target_address, mach_vm_size_t size,
    mach_vm_offset_t mask, int flags, vm_map_t src_task,
    mach_vm_address_t src_address, boolean_t copy, vm_prot_t * cur_protection,
    vm_prot_t * max_protection, vm_inherit_t inheritance);
#endif

#endif
