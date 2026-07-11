/*
 * Copyright (C) 2015-2025 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_ARM64_READER_H__
#define __HOOX_ARM64_READER_H__

#include "hooxdefs.h"

#include <hx_disasm.h>

HX_BEGIN_DECLS

HOOX_API hx_pointer hoox_arm64_reader_find_next_bl_target (hx_constpointer address);
HOOX_API hx_pointer hoox_arm64_reader_try_get_relative_jump_target (
    hx_constpointer address);
HOOX_API hx_insn * hoox_arm64_reader_disassemble_instruction_at (
    hx_constpointer address);

HX_END_DECLS

#endif
