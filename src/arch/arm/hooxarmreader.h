/*
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_ARM_READER_H__
#define __HOOX_ARM_READER_H__

#include "hooxdefs.h"

#include <hx_disasm.h>

HX_BEGIN_DECLS

hx_pointer hoox_arm_reader_try_get_relative_jump_target (hx_constpointer address);
hx_pointer hoox_arm_reader_try_get_indirect_jump_target (hx_constpointer address);
hx_insn * hoox_arm_reader_disassemble_instruction_at (hx_constpointer address);

HX_END_DECLS

#endif
