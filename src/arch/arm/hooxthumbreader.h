/*
 * Copyright (C) 2015 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_THUMB_READER_H__
#define __HOOX_THUMB_READER_H__

#include "hooxdefs.h"

#include <hx_disasm.h>

HX_BEGIN_DECLS

hx_pointer hoox_thumb_reader_try_get_relative_jump_target (hx_constpointer address);
hx_insn * hoox_thumb_reader_disassemble_instruction_at (hx_constpointer address);

HX_END_DECLS

#endif
