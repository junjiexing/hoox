/*
 * Copyright (C) 2009 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_HX_READER_H__
#define __HOOX_HX_READER_H__

#include "hooxdefs.h"

#include <hx_disasm.h>

HX_BEGIN_DECLS

HOOX_API hx_uint hoox_x86_reader_insn_length (hx_uint8 * code);
HOOX_API hx_boolean hoox_x86_reader_insn_is_jcc (const hx_insn * insn);

HOOX_API hx_pointer hoox_x86_reader_find_next_call_target (hx_constpointer address);
HOOX_API hx_pointer hoox_x86_reader_try_get_relative_call_target (
    hx_constpointer address);
HOOX_API hx_pointer hoox_x86_reader_try_get_relative_jump_target (
    hx_constpointer address);
HOOX_API hx_pointer hoox_x86_reader_try_get_indirect_jump_target (
    hx_constpointer address);
HOOX_API hx_insn * hoox_x86_reader_disassemble_instruction_at (
    hx_constpointer address);

HX_END_DECLS

#endif
