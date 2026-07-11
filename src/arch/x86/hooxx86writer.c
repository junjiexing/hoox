/*
 * Copyright (C) 2009-2022 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2023 Fabian Freyer <fabian.freyer@physik.tu-berlin.de>
 * Copyright (C) 2024 Yannis Juglaret <yjuglaret@mozilla.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxx86writer.h"

#include <string.h>
#include "hooxmemory.h"

typedef hx_uint HooxX86MetaReg;
typedef struct _HooxX86RegInfo HooxX86RegInfo;
typedef hx_uint HooxX86LabelRefSize;
typedef struct _HooxX86LabelRef HooxX86LabelRef;

enum _HooxX86MetaReg
{
  HOOX_HX_META_XAX = 0,
  HOOX_HX_META_XCX,
  HOOX_HX_META_XDX,
  HOOX_HX_META_XBX,
  HOOX_HX_META_XSP,
  HOOX_HX_META_XBP,
  HOOX_HX_META_XSI,
  HOOX_HX_META_XDI,
  HOOX_HX_META_R8,
  HOOX_HX_META_R9,
  HOOX_HX_META_R10,
  HOOX_HX_META_R11,
  HOOX_HX_META_R12,
  HOOX_HX_META_R13,
  HOOX_HX_META_R14,
  HOOX_HX_META_R15
};

struct _HooxX86RegInfo
{
  HooxX86MetaReg meta;
  hx_uint width;
  hx_uint index;
  hx_boolean index_is_extended;
};

enum _HooxX86LabelRefSize
{
  HOOX_LREF_SHORT,
  HOOX_LREF_NEAR,
  HOOX_LREF_ABS
};

struct _HooxX86LabelRef
{
  hx_constpointer id;
  hx_uint8 * address;
  HooxX86LabelRefSize size;
};

static void hoox_x86_writer_put_argument_list_setup (HooxX86Writer * self,
    HooxCallingConvention conv, hx_uint n_args, const HooxArgument * args);
static void hoox_x86_writer_put_argument_list_setup_va (HooxX86Writer * self,
    HooxCallingConvention conv, hx_uint n_args, va_list args);
static void hoox_x86_writer_put_argument_list_teardown (HooxX86Writer * self,
    HooxCallingConvention conv, hx_uint n_args);
static void hoox_x86_writer_put_aligned_argument_list_setup (HooxX86Writer * self,
    HooxCallingConvention conv, hx_uint n_args, const HooxArgument * args);
static void hoox_x86_writer_put_aligned_argument_list_setup_va (
    HooxX86Writer * self, HooxCallingConvention conv, hx_uint n_args, va_list args);
static void hoox_x86_writer_put_aligned_argument_list_teardown (
    HooxX86Writer * self, HooxCallingConvention conv, hx_uint n_args);
static hx_uint hoox_x86_writer_get_needed_alignment_correction (
    HooxX86Writer * self, hx_uint n_args);
static hx_boolean hoox_x86_writer_put_short_jmp (HooxX86Writer * self,
    hx_constpointer target);
static hx_boolean hoox_x86_writer_put_near_jmp (HooxX86Writer * self,
    hx_constpointer target);
static void hoox_x86_writer_put_ud2 (HooxX86Writer * self);
static hx_boolean hoox_x86_writer_put_fx_save_or_restore_reg_ptr (
    HooxX86Writer * self, hx_uint8 operation, HooxX86Reg reg);
static void hoox_x86_writer_describe_cpu_reg (HooxX86Writer * self,
    HooxX86Reg reg, HooxX86RegInfo * ri);

static HooxX86MetaReg hoox_meta_reg_from_cpu_reg (HooxX86Reg reg);

static hx_boolean hoox_x86_writer_put_prefix_for_reg_info (HooxX86Writer * self,
    const HooxX86RegInfo * ri, hx_uint operand_index);
static hx_boolean hoox_x86_writer_put_prefix_for_registers (HooxX86Writer * self,
    const HooxX86RegInfo * width_reg, hx_uint default_width, ...);

static hx_uint8 hoox_get_jcc_opcode (hx_x86_insn instruction_id);

HooxX86Writer *
hoox_x86_writer_ref (HooxX86Writer * writer)
{
  hx_atomic_int_inc (&writer->ref_count);

  return writer;
}

void
hoox_x86_writer_unref (HooxX86Writer * writer)
{
  if (hx_atomic_int_dec_and_test (&writer->ref_count))
  {
    hoox_x86_writer_clear (writer);

    hx_slice_free (HooxX86Writer, writer);
  }
}

void
hoox_x86_writer_init (HooxX86Writer * writer,
                     hx_pointer code_address)
{
  writer->ref_count = 1;
  writer->flush_on_destroy = TRUE;

  writer->label_defs = NULL;
  writer->label_refs.data = NULL;

  hoox_x86_writer_reset (writer, code_address);
}

static hx_boolean
hoox_x86_writer_has_label_defs (HooxX86Writer * self)
{
  return self->label_defs != NULL;
}

static hx_boolean
hoox_x86_writer_has_label_refs (HooxX86Writer * self)
{
  return self->label_refs.data != NULL;
}

void
hoox_x86_writer_clear (HooxX86Writer * writer)
{
  if (writer->flush_on_destroy)
    hoox_x86_writer_flush (writer);

  if (hoox_x86_writer_has_label_defs (writer))
    hoox_metal_hash_table_unref (writer->label_defs);

  if (hoox_x86_writer_has_label_refs (writer))
    hoox_metal_array_free (&writer->label_refs);
}

void
hoox_x86_writer_reset (HooxX86Writer * writer,
                      hx_pointer code_address)
{
#if HX_SIZEOF_VOID_P == 4
  writer->target_cpu = HOOX_CPU_IA32;
#else
  writer->target_cpu = HOOX_CPU_AMD64;
#endif
  writer->target_abi = HOOX_NATIVE_ABI;
  writer->cpu_features = hoox_query_cpu_features ();

  writer->base = (hx_uint8 *) code_address;
  writer->code = (hx_uint8 *) code_address;
  writer->pc = HOOX_ADDRESS (code_address);

  if (hoox_x86_writer_has_label_defs (writer))
    hoox_metal_hash_table_remove_all (writer->label_defs);

  if (hoox_x86_writer_has_label_refs (writer))
    hoox_metal_array_remove_all (&writer->label_refs);
}

void
hoox_x86_writer_set_target_cpu (HooxX86Writer * self,
                               HooxCpuType cpu_type)
{
  self->target_cpu = cpu_type;
}

void
hoox_x86_writer_set_target_abi (HooxX86Writer * self,
                               HooxAbiType abi_type)
{
  self->target_abi = abi_type;
}

hx_uint
hoox_x86_writer_offset (HooxX86Writer * self)
{
  return (hx_uint) (self->code - self->base);
}

static void
hoox_x86_writer_commit (HooxX86Writer * self,
                       hx_uint n)
{
  self->code += n;
  self->pc += n;
}

hx_boolean
hoox_x86_writer_flush (HooxX86Writer * self)
{
  hx_uint num_refs, ref_index;

  if (!hoox_x86_writer_has_label_refs (self))
    return TRUE;

  if (!hoox_x86_writer_has_label_defs (self))
    return FALSE;

  num_refs = self->label_refs.length;

  for (ref_index = 0; ref_index != num_refs; ref_index++)
  {
    HooxX86LabelRef * r;
    hx_pointer target_address;
    hx_int32 distance;

    r = hoox_metal_array_element_at (&self->label_refs, ref_index);

    target_address = hoox_metal_hash_table_lookup (self->label_defs, r->id);
    if (target_address == NULL)
      goto error;

    distance = (hx_int32) ((hx_ssize) target_address - (hx_ssize) r->address);

    switch (r->size)
    {
      case HOOX_LREF_SHORT:
        if (!HOOX_IS_WITHIN_INT8_RANGE (distance))
          goto error;
        *((hx_int8 *) (r->address - 1)) = distance;
        break;
      case HOOX_LREF_NEAR:
        *((hx_int32 *) (r->address - 4)) = HX_INT32_TO_LE (distance);
        break;
      case HOOX_LREF_ABS:
      {
        hx_offset target_offset;
        HooxAddress base_pc, target_pc;

        target_offset = (hx_uint8 *) target_address - self->base;

        base_pc = self->pc - hoox_x86_writer_offset (self);
        target_pc = base_pc + target_offset;

        if (self->target_cpu == HOOX_CPU_AMD64)
          *((hx_uint64 *) (r->address - 8)) = HX_UINT64_TO_LE (target_pc);
        else
          *((hx_uint32 *) (r->address - 4)) = HX_UINT32_TO_LE (target_pc);

        break;
      }
      default:
        hx_assert_not_reached ();
    }
  }

  hoox_metal_array_remove_all (&self->label_refs);

  return TRUE;

error:
  {
    hoox_metal_array_remove_all (&self->label_refs);

    return FALSE;
  }
}

hx_boolean
hoox_x86_writer_put_label (HooxX86Writer * self,
                          hx_constpointer id)
{
  if (!hoox_x86_writer_has_label_defs (self))
    self->label_defs = hoox_metal_hash_table_new (NULL, NULL);

  if (hoox_metal_hash_table_lookup (self->label_defs, id) != NULL)
    return FALSE;

  hoox_metal_hash_table_insert (self->label_defs, (hx_pointer) id, self->code);

  return TRUE;
}

static void
hoox_x86_writer_add_label_reference_here (HooxX86Writer * self,
                                         hx_constpointer id,
                                         HooxX86LabelRefSize size)
{
  HooxX86LabelRef * r;

  if (!hoox_x86_writer_has_label_refs (self))
    hoox_metal_array_init (&self->label_refs, sizeof (HooxX86LabelRef));

  r = hoox_metal_array_append (&self->label_refs);
  r->id = id;
  r->address = self->code;
  r->size = size;
}

hx_boolean
hoox_x86_writer_put_call_address_with_arguments (HooxX86Writer * self,
                                                HooxCallingConvention conv,
                                                HooxAddress func,
                                                hx_uint n_args,
                                                ...)
{
  va_list args;

  va_start (args, n_args);
  hoox_x86_writer_put_argument_list_setup_va (self, conv, n_args, args);
  va_end (args);

  if (!hoox_x86_writer_put_call_address (self, func))
    return FALSE;

  hoox_x86_writer_put_argument_list_teardown (self, conv, n_args);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_call_address_with_aligned_arguments (
    HooxX86Writer * self,
    HooxCallingConvention conv,
    HooxAddress func,
    hx_uint n_args,
    ...)
{
  va_list args;

  va_start (args, n_args);
  hoox_x86_writer_put_aligned_argument_list_setup_va (self, conv, n_args, args);
  va_end (args);

  if (!hoox_x86_writer_put_call_address (self, func))
    return FALSE;

  hoox_x86_writer_put_aligned_argument_list_teardown (self, conv, n_args);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_call_reg_with_arguments (HooxX86Writer * self,
                                            HooxCallingConvention conv,
                                            HooxX86Reg reg,
                                            hx_uint n_args,
                                            ...)
{
  va_list args;

  va_start (args, n_args);
  hoox_x86_writer_put_argument_list_setup_va (self, conv, n_args, args);
  va_end (args);

  if (!hoox_x86_writer_put_call_reg (self, reg))
    return FALSE;

  hoox_x86_writer_put_argument_list_teardown (self, conv, n_args);

  return TRUE;
}

static void
hoox_x86_writer_put_argument_list_setup (HooxX86Writer * self,
                                        HooxCallingConvention conv,
                                        hx_uint n_args,
                                        const HooxArgument * args)
{
  hx_int arg_index;

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    for (arg_index = (hx_int) n_args - 1; arg_index >= 0; arg_index--)
    {
      const HooxArgument * arg = &args[arg_index];

      if (arg->type == HOOX_ARG_ADDRESS)
      {
        hoox_x86_writer_put_push_u32 (self, (hx_uint32) arg->value.address);
      }
      else
      {
        hoox_x86_writer_put_push_reg (self, arg->value.reg);
      }
    }
  }
  else
  {
    static const HooxX86Reg reg_for_arg_unix_64[6] = {
      HOOX_HX_RDI,
      HOOX_HX_RSI,
      HOOX_HX_RDX,
      HOOX_HX_RCX,
      HOOX_HX_R8,
      HOOX_HX_R9
    };
    static const HooxX86Reg reg_for_arg_unix_32[6] = {
      HOOX_HX_EDI,
      HOOX_HX_ESI,
      HOOX_HX_EDX,
      HOOX_HX_ECX,
      HOOX_HX_R8D,
      HOOX_HX_R9D
    };
    static const HooxX86Reg reg_for_arg_windows_64[4] = {
      HOOX_HX_RCX,
      HOOX_HX_RDX,
      HOOX_HX_R8,
      HOOX_HX_R9
    };
    static const HooxX86Reg reg_for_arg_windows_32[4] = {
      HOOX_HX_ECX,
      HOOX_HX_EDX,
      HOOX_HX_R8D,
      HOOX_HX_R9D
    };
    const HooxX86Reg * reg_for_arg_64, * reg_for_arg_32;
    hx_int reg_for_arg_count;

    if (self->target_abi == HOOX_ABI_UNIX)
    {
      reg_for_arg_64 = reg_for_arg_unix_64;
      reg_for_arg_32 = reg_for_arg_unix_32;
      reg_for_arg_count = HX_N_ELEMENTS (reg_for_arg_unix_64);
    }
    else
    {
      reg_for_arg_64 = reg_for_arg_windows_64;
      reg_for_arg_32 = reg_for_arg_windows_32;
      reg_for_arg_count = HX_N_ELEMENTS (reg_for_arg_windows_64);
    }

    for (arg_index = (hx_int) n_args - 1; arg_index >= 0; arg_index--)
    {
      const HooxArgument * arg = &args[arg_index];

      if (arg_index < reg_for_arg_count)
      {
        if (arg->type == HOOX_ARG_ADDRESS)
        {
          hoox_x86_writer_put_mov_reg_u64 (self, reg_for_arg_64[arg_index],
              arg->value.address);
        }
        else if (hoox_meta_reg_from_cpu_reg (arg->value.reg) !=
            hoox_meta_reg_from_cpu_reg (reg_for_arg_64[arg_index]))
        {
          if (arg->value.reg >= HOOX_HX_EAX && arg->value.reg <= HOOX_HX_EIP)
          {
            hoox_x86_writer_put_mov_reg_reg (self, reg_for_arg_32[arg_index],
                arg->value.reg);
          }
          else
          {
            hoox_x86_writer_put_mov_reg_reg (self, reg_for_arg_64[arg_index],
                arg->value.reg);
          }
        }
      }
      else
      {
        if (arg->type == HOOX_ARG_ADDRESS)
        {
          hoox_x86_writer_put_push_reg (self, HOOX_HX_XAX);
          hoox_x86_writer_put_mov_reg_address (self, HOOX_HX_RAX,
              arg->value.address);
          hoox_x86_writer_put_xchg_reg_reg_ptr (self, HOOX_HX_RAX, HOOX_HX_RSP);
        }
        else
        {
          hoox_x86_writer_put_push_reg (self, arg->value.reg);
        }
      }
    }

    if (self->target_abi == HOOX_ABI_WINDOWS)
      hoox_x86_writer_put_sub_reg_imm (self, HOOX_HX_RSP, 4 * 8);
  }
}

static void
hoox_x86_writer_put_argument_list_setup_va (HooxX86Writer * self,
                                           HooxCallingConvention conv,
                                           hx_uint n_args,
                                           va_list args)
{
  HooxArgument * arg_values;
  hx_uint arg_index;

  arg_values = hx_newa (HooxArgument, n_args);

  for (arg_index = 0; arg_index != n_args; arg_index++)
  {
    HooxArgument * arg = &arg_values[arg_index];

    arg->type = va_arg (args, HooxArgType);
    if (arg->type == HOOX_ARG_ADDRESS)
      arg->value.address = va_arg (args, HooxAddress);
    else if (arg->type == HOOX_ARG_REGISTER)
      arg->value.reg = va_arg (args, HooxX86Reg);
    else
      hx_assert_not_reached ();
  }

  hoox_x86_writer_put_argument_list_setup (self, conv, n_args, arg_values);
}

static void
hoox_x86_writer_put_argument_list_teardown (HooxX86Writer * self,
                                           HooxCallingConvention conv,
                                           hx_uint n_args)
{
  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (conv == HOOX_CALL_CAPI && n_args != 0)
    {
      hoox_x86_writer_put_add_reg_imm (self, HOOX_HX_ESP,
          n_args * sizeof (hx_uint32));
    }
  }
  else
  {
    if (self->target_abi == HOOX_ABI_WINDOWS)
      hoox_x86_writer_put_add_reg_imm (self, HOOX_HX_RSP, MAX (n_args, 4) * 8);
    else if (n_args > 6)
      hoox_x86_writer_put_add_reg_imm (self, HOOX_HX_RSP, (n_args - 6) * 8);
  }
}

static void
hoox_x86_writer_put_aligned_argument_list_setup (HooxX86Writer * self,
                                                HooxCallingConvention conv,
                                                hx_uint n_args,
                                                const HooxArgument * args)
{
  hx_uint align_correction;

  align_correction =
      hoox_x86_writer_get_needed_alignment_correction (self, n_args);
  if (align_correction != 0)
  {
    hoox_x86_writer_put_sub_reg_imm (self, HOOX_HX_XSP, align_correction);
  }

  hoox_x86_writer_put_argument_list_setup (self, conv, n_args, args);
}

static void
hoox_x86_writer_put_aligned_argument_list_setup_va (HooxX86Writer * self,
                                                   HooxCallingConvention conv,
                                                   hx_uint n_args,
                                                   va_list args)
{
  hx_uint align_correction;

  align_correction =
      hoox_x86_writer_get_needed_alignment_correction (self, n_args);
  if (align_correction != 0)
  {
    hoox_x86_writer_put_sub_reg_imm (self, HOOX_HX_XSP, align_correction);
  }

  hoox_x86_writer_put_argument_list_setup_va (self, conv, n_args, args);
}

static void
hoox_x86_writer_put_aligned_argument_list_teardown (HooxX86Writer * self,
                                                   HooxCallingConvention conv,
                                                   hx_uint n_args)
{
  hx_uint align_correction;

  hoox_x86_writer_put_argument_list_teardown (self, conv, n_args);

  align_correction =
      hoox_x86_writer_get_needed_alignment_correction (self, n_args);
  if (align_correction != 0)
  {
    hoox_x86_writer_put_add_reg_imm (self, HOOX_HX_XSP, align_correction);
  }
}

static hx_uint
hoox_x86_writer_get_needed_alignment_correction (HooxX86Writer * self,
                                                hx_uint n_args)
{
  hx_uint n_stack_args, pointer_size, stack_args_size, remainder;

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    n_stack_args = n_args;

    pointer_size = 4;
  }
  else
  {
    if (self->target_abi == HOOX_ABI_UNIX)
      n_stack_args = (n_args > 6) ? n_args - 6 : 0;
    else
      n_stack_args = (n_args > 4) ? n_args - 4 : 0;

    pointer_size = 8;
  }

  stack_args_size = n_stack_args * pointer_size;

  remainder = stack_args_size % 16;

  return (remainder != 0) ? 16 - remainder : 0;
}

hx_boolean
hoox_x86_writer_put_call_reg_offset_ptr_with_arguments (
    HooxX86Writer * self,
    HooxCallingConvention conv,
    HooxX86Reg reg,
    hx_ssize offset,
    hx_uint n_args,
    ...)
{
  va_list args;

  va_start (args, n_args);
  hoox_x86_writer_put_argument_list_setup_va (self, conv, n_args, args);
  va_end (args);

  if (!hoox_x86_writer_put_call_reg_offset_ptr (self, reg, offset))
    return FALSE;

  hoox_x86_writer_put_argument_list_teardown (self, conv, n_args);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_call_address (HooxX86Writer * self,
                                 HooxAddress address)
{
  hx_int64 distance;
  hx_boolean distance_fits_in_i32;

  distance = (hx_ssize) address - (hx_ssize) (self->pc + 5);
  distance_fits_in_i32 = (distance >= HX_MININT32 && distance <= HX_MAXINT32);

  if (distance_fits_in_i32)
  {
    self->code[0] = 0xe8;
    *((hx_int32 *) (self->code + 1)) = HX_INT32_TO_LE (distance);
    hoox_x86_writer_commit (self, 5);
  }
  else
  {
    hx_constpointer call_target_storage = self->code + 1;
    hx_constpointer carry_on = self->code + 2;

    if (self->target_cpu != HOOX_CPU_AMD64)
      return FALSE;

    hoox_x86_writer_put_call_indirect_label (self, call_target_storage);
    hoox_x86_writer_put_jmp_short_label (self, carry_on);

    hoox_x86_writer_put_label (self, call_target_storage);
    *((hx_uint64 *) (self->code)) = HX_UINT64_TO_LE (address);
    hoox_x86_writer_commit (self, 8);

    hoox_x86_writer_put_label (self, carry_on);
  }

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_call_reg (HooxX86Writer * self,
                             HooxX86Reg reg)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (ri.width != 32 || ri.index_is_extended)
      return FALSE;
  }
  else
  {
    if (ri.width != 64)
      return FALSE;
  }

  if (ri.index_is_extended)
    hoox_x86_writer_put_u8 (self, 0x41);
  self->code[0] = 0xff;
  self->code[1] = 0xd0 | ri.index;
  hoox_x86_writer_commit (self, 2);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_call_reg_offset_ptr (HooxX86Writer * self,
                                        HooxX86Reg reg,
                                        hx_ssize offset)
{
  HooxX86RegInfo ri;
  hx_boolean offset_fits_in_i8;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  offset_fits_in_i8 = HOOX_IS_WITHIN_INT8_RANGE (offset);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (ri.width != 32 || ri.index_is_extended)
      return FALSE;
  }
  else
  {
    if (ri.width != 64)
      return FALSE;
  }

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 64, &ri, NULL))
    return FALSE;

  self->code[0] = 0xff;
  self->code[1] = (offset_fits_in_i8 ? 0x50 : 0x90) | ri.index;
  hoox_x86_writer_commit (self, 2);

  if (ri.index == 4)
    hoox_x86_writer_put_u8 (self, 0x24);

  if (offset_fits_in_i8)
  {
    hoox_x86_writer_put_s8 (self, (hx_int8) offset);
  }
  else
  {
    *((hx_int32 *) self->code) = HX_INT32_TO_LE (offset);
    hoox_x86_writer_commit (self, 4);
  }

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_call_indirect (HooxX86Writer * self,
                                  HooxAddress address)
{
  if (self->target_cpu == HOOX_CPU_AMD64)
  {
    hx_int64 distance = (hx_int64) address - (hx_int64) (self->pc + 6);

    if (!HOOX_IS_WITHIN_INT32_RANGE (distance))
      return FALSE;

    self->code[0] = 0xff;
    self->code[1] = 0x15;
    *((hx_uint32 *) (self->code + 2)) = HX_INT32_TO_LE ((hx_int32) distance);
  }
  else
  {
    self->code[0] = 0xff;
    self->code[1] = 0x15;
    *((hx_uint32 *) (self->code + 2)) = HX_UINT32_TO_LE (address);
  }

  hoox_x86_writer_commit (self, 6);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_call_indirect_label (HooxX86Writer * self,
                                        hx_constpointer label_id)
{
  if (!hoox_x86_writer_put_call_indirect (self, self->pc))
    return FALSE;

  hoox_x86_writer_add_label_reference_here (self, label_id,
      (self->target_cpu == HOOX_CPU_AMD64)
          ? HOOX_LREF_NEAR
          : HOOX_LREF_ABS);
  return TRUE;
}

void
hoox_x86_writer_put_call_near_label (HooxX86Writer * self,
                                    hx_constpointer label_id)
{
  hoox_x86_writer_put_call_address (self, self->pc);
  hoox_x86_writer_add_label_reference_here (self, label_id, HOOX_LREF_NEAR);
}

void
hoox_x86_writer_put_ret (HooxX86Writer * self)
{
  hoox_x86_writer_put_u8 (self, 0xc3);
}

hx_boolean
hoox_x86_writer_put_jmp_address (HooxX86Writer * self,
                                HooxAddress address)
{
  hx_int64 distance;

  distance = (hx_ssize) address - (hx_ssize) (self->pc + 2);

  if (HOOX_IS_WITHIN_INT8_RANGE (distance))
  {
    self->code[0] = 0xeb;
    *((hx_int8 *) (self->code + 1)) = (hx_int8) distance;
    hoox_x86_writer_commit (self, 2);
  }
  else
  {
    distance = (hx_ssize) address - (hx_ssize) (self->pc + 5);

    if (HOOX_IS_WITHIN_INT32_RANGE (distance))
    {
      self->code[0] = 0xe9;
      *((hx_int32 *) (self->code + 1)) = HX_INT32_TO_LE ((hx_int32) distance);
      hoox_x86_writer_commit (self, 5);
    }
    else
    {
      if (self->target_cpu != HOOX_CPU_AMD64)
        return FALSE;

      self->code[0] = 0xff;
      self->code[1] = 0x25;
      *((hx_int32 *) (self->code + 2)) = HX_INT32_TO_LE (2); /* RIP + 2 */
      self->code[6] = 0x0f;
      self->code[7] = 0x0b;
      *((hx_uint64 *) (self->code + 8)) = HX_UINT64_TO_LE (address);
      hoox_x86_writer_commit (self, 16);
    }
  }

  return TRUE;
}

static hx_boolean
hoox_x86_writer_put_short_jmp (HooxX86Writer * self,
                              hx_constpointer target)
{
  hx_int64 distance;

  distance = (hx_ssize) target - (hx_ssize) (self->pc + 2);
  if (!HOOX_IS_WITHIN_INT8_RANGE (distance))
    return FALSE;

  self->code[0] = 0xeb;
  *((hx_int8 *) (self->code + 1)) = (hx_int8) distance;
  hoox_x86_writer_commit (self, 2);

  return TRUE;
}

static hx_boolean
hoox_x86_writer_put_near_jmp (HooxX86Writer * self,
                             hx_constpointer target)
{
  hx_int64 distance;

  distance = (hx_ssize) target - (hx_ssize) (self->pc + 5);

  if (HOOX_IS_WITHIN_INT32_RANGE (distance))
  {
    self->code[0] = 0xe9;
    *((hx_int32 *) (self->code + 1)) = HX_INT32_TO_LE (distance);
    hoox_x86_writer_commit (self, 5);
  }
  else
  {
    if (self->target_cpu != HOOX_CPU_AMD64)
      return FALSE;

    self->code[0] = 0xff;                               /* JMP [RIP + 2] */
    self->code[1] = 0x25;
    *((hx_int32 *) (self->code + 2)) = HX_INT32_TO_LE (2);  /* RIP + 2 */

    self->code[6] = 0x0f;                               /* UD2 */
    self->code[7] = 0x0b;

    *((hx_uint64 *) (self->code + 8)) = HX_UINT64_TO_LE (HX_POINTER_TO_SIZE (target));
    hoox_x86_writer_commit (self, 16);
  }

  return TRUE;
}

void
hoox_x86_writer_put_jmp_short_label (HooxX86Writer * self,
                                    hx_constpointer label_id)
{
  hoox_x86_writer_put_short_jmp (self, HX_SIZE_TO_POINTER (self->pc));
  hoox_x86_writer_add_label_reference_here (self, label_id, HOOX_LREF_SHORT);
}

hx_boolean
hoox_x86_writer_put_jmp_reg (HooxX86Writer * self,
                            HooxX86Reg reg)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (ri.width != 32 || ri.index_is_extended)
      return FALSE;
  }
  else
  {
    if (ri.width != 64)
      return FALSE;
  }

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 64, &ri, NULL))
    return FALSE;

  self->code[0] = 0xff;
  self->code[1] = 0xe0 | ri.index;
  hoox_x86_writer_commit (self, 2);

  hoox_x86_writer_put_ud2 (self);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_jmp_reg_ptr (HooxX86Writer * self,
                                HooxX86Reg reg)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (ri.width != 32 || ri.index_is_extended)
      return FALSE;
  }
  else
  {
    if (ri.width != 64)
      return FALSE;
  }

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 64, &ri, NULL))
    return FALSE;

  self->code[0] = 0xff;
  self->code[1] = 0x20 | ri.index;
  hoox_x86_writer_commit (self, 2);

  if (ri.meta == HOOX_HX_META_XSP)
    hoox_x86_writer_put_u8 (self, 0x24);

  hoox_x86_writer_put_ud2 (self);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_jmp_reg_offset_ptr (HooxX86Writer * self,
                                       HooxX86Reg reg,
                                       hx_ssize offset)
{
  HooxX86RegInfo ri;
  hx_boolean offset_fits_in_i8;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  offset_fits_in_i8 = HOOX_IS_WITHIN_INT8_RANGE (offset);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (ri.width != 32 || ri.index_is_extended)
      return FALSE;
  }
  else
  {
    if (ri.width != 64)
      return FALSE;
  }

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 64, &ri, NULL))
    return FALSE;

  self->code[0] = 0xff;
  self->code[1] = (offset_fits_in_i8 ? 0x60 : 0xa0) | ri.index;
  hoox_x86_writer_commit (self, 2);

  if (ri.index == 4)
    hoox_x86_writer_put_u8 (self, 0x24);

  if (offset_fits_in_i8)
  {
    hoox_x86_writer_put_s8 (self, (hx_int8) offset);
  }
  else
  {
    *((hx_int32 *) self->code) = HX_INT32_TO_LE (offset);
    hoox_x86_writer_commit (self, 4);
  }

  hoox_x86_writer_put_ud2 (self);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_jmp_near_ptr (HooxX86Writer * self,
                                 HooxAddress address)
{
  self->code[0] = 0xff;
  self->code[1] = 0x25;

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (address > HX_MAXUINT32)
      return FALSE;
    *((hx_uint32 *) (self->code + 2)) = HX_UINT32_TO_LE ((hx_uint32) address);
  }
  else
  {
    hx_int64 distance = (hx_int64) address - (hx_int64) (self->pc + 6);
    if (distance < HX_MININT32 || distance > HX_MAXINT32)
      return FALSE;
    *((hx_int32 *) (self->code + 2)) = HX_INT32_TO_LE ((hx_int32) distance);
  }

  hoox_x86_writer_commit (self, 6);

  hoox_x86_writer_put_ud2 (self);

  return TRUE;
}

/*
 * This instruction causes a UD exception when executed, which isn't very
 * useful, however its presence also stalls the branch predictor. e.g. if the
 * CPU encounters a `JMP [reg]` instruction, and there is no entry in its branch
 * target buffer (cache of previous branches) it will assume that execution
 * continues with the next instruction (which is where compilers will typically
 * place the most common branch of a switch statement). However, in most cases
 * (e.g. Stalker) such indirect branches will typically be used to divert
 * control flow to an address which can only be determined at runtime. As such
 * by following these branches with `UD2`, we can prevent the speculative
 * execution of subsequent instructions and hence the overhead of unwinding
 * them.
 */
static void
hoox_x86_writer_put_ud2 (HooxX86Writer * self)
{
  hoox_x86_writer_put_u8 (self, 0x0f);
  hoox_x86_writer_put_u8 (self, 0x0b);
}

hx_boolean
hoox_x86_writer_put_jcc_short (HooxX86Writer * self,
                              hx_x86_insn instruction_id,
                              hx_constpointer target,
                              HooxBranchHint hint)
{
  hx_ssize distance;

  if (hint != HOOX_NO_HINT)
    hoox_x86_writer_put_u8 (self, (hint == HOOX_LIKELY) ? 0x3e : 0x2e);
  self->code[0] = hoox_get_jcc_opcode (instruction_id);
  distance = (hx_ssize) target - (hx_ssize) (self->pc + 2);
  if (!HOOX_IS_WITHIN_INT8_RANGE (distance))
    return FALSE;
  *((hx_int8 *) (self->code + 1)) = (hx_int8) distance;
  hoox_x86_writer_commit (self, 2);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_jcc_near (HooxX86Writer * self,
                             hx_x86_insn instruction_id,
                             hx_constpointer target,
                             HooxBranchHint hint)
{
  hx_ssize distance;

  if (hint != HOOX_NO_HINT)
    hoox_x86_writer_put_u8 (self, (hint == HOOX_LIKELY) ? 0x3e : 0x2e);
  self->code[0] = 0x0f;
  self->code[1] = 0x10 + hoox_get_jcc_opcode (instruction_id);
  distance = (hx_ssize) target - (hx_ssize) (self->pc + 6);
  if (!HOOX_IS_WITHIN_INT32_RANGE (distance))
    return FALSE;
  *((hx_int32 *) (self->code + 2)) = HX_INT32_TO_LE (distance);
  hoox_x86_writer_commit (self, 6);

  return TRUE;
}

void
hoox_x86_writer_put_jcc_short_label (HooxX86Writer * self,
                                    hx_x86_insn instruction_id,
                                    hx_constpointer label_id,
                                    HooxBranchHint hint)
{
  hoox_x86_writer_put_jcc_short (self, instruction_id,
      HX_SIZE_TO_POINTER (self->pc), hint);
  hoox_x86_writer_add_label_reference_here (self, label_id, HOOX_LREF_SHORT);
}

void
hoox_x86_writer_put_jcc_near_label (HooxX86Writer * self,
                                   hx_x86_insn instruction_id,
                                   hx_constpointer label_id,
                                   HooxBranchHint hint)
{
  hoox_x86_writer_put_jcc_near (self, instruction_id,
      HX_SIZE_TO_POINTER (self->pc), hint);
  hoox_x86_writer_add_label_reference_here (self, label_id, HOOX_LREF_NEAR);
}

static hx_boolean
hoox_x86_writer_put_add_or_sub_reg_imm (HooxX86Writer * self,
                                       HooxX86Reg reg,
                                       hx_ssize imm_value,
                                       hx_boolean add)
{
  HooxX86RegInfo ri;
  hx_boolean immediate_fits_in_i8;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  immediate_fits_in_i8 = HOOX_IS_WITHIN_INT8_RANGE (imm_value);

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 32, &ri, NULL))
    return FALSE;

  if (ri.meta == HOOX_HX_META_XAX && !immediate_fits_in_i8)
  {
    hoox_x86_writer_put_u8 (self, add ? 0x05 : 0x2d);
  }
  else
  {
    self->code[0] = immediate_fits_in_i8 ? 0x83 : 0x81;
    self->code[1] = (add ? 0xc0 : 0xe8) | ri.index;
    hoox_x86_writer_commit (self, 2);
  }

  if (immediate_fits_in_i8)
  {
    hoox_x86_writer_put_s8 (self, (hx_int8) imm_value);
  }
  else
  {
    *((hx_int32 *) self->code) = HX_INT32_TO_LE (imm_value);
    hoox_x86_writer_commit (self, 4);
  }

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_add_reg_imm (HooxX86Writer * self,
                                HooxX86Reg reg,
                                hx_ssize imm_value)
{
  return hoox_x86_writer_put_add_or_sub_reg_imm (self, reg, imm_value, TRUE);
}

hx_boolean
hoox_x86_writer_put_add_reg_reg (HooxX86Writer * self,
                                HooxX86Reg dst_reg,
                                HooxX86Reg src_reg)
{
  HooxX86RegInfo dst, src;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);
  hoox_x86_writer_describe_cpu_reg (self, src_reg, &src);

  if (src.width != dst.width)
    return FALSE;

  if (!hoox_x86_writer_put_prefix_for_registers (self, &dst, 32, &dst, &src,
      NULL))
    return FALSE;

  self->code[0] = 0x01;
  self->code[1] = 0xc0 | (src.index << 3) | dst.index;
  hoox_x86_writer_commit (self, 2);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_sub_reg_imm (HooxX86Writer * self,
                                HooxX86Reg reg,
                                hx_ssize imm_value)
{
  return hoox_x86_writer_put_add_or_sub_reg_imm (self, reg, imm_value, FALSE);
}

hx_boolean
hoox_x86_writer_put_inc_reg (HooxX86Writer * self,
                            HooxX86Reg reg)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (self->target_cpu != HOOX_CPU_AMD64 &&
      (ri.width != 32 || ri.index_is_extended))
  {
    return FALSE;
  }

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 32, &ri, NULL))
    return FALSE;

  self->code[0] = 0xff;
  self->code[1] = 0xc0 | ri.index;
  hoox_x86_writer_commit (self, 2);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_dec_reg (HooxX86Writer * self,
                            HooxX86Reg reg)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (self->target_cpu != HOOX_CPU_AMD64 &&
      (ri.width != 32 || ri.index_is_extended))
  {
    return FALSE;
  }

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 32, &ri, NULL))
    return FALSE;

  self->code[0] = 0xff;
  self->code[1] = 0xc8 | ri.index;
  hoox_x86_writer_commit (self, 2);

  return TRUE;
}

static hx_boolean
hoox_x86_writer_put_inc_or_dec_reg_ptr (HooxX86Writer * self,
                                       HooxX86PtrTarget target,
                                       HooxX86Reg reg,
                                       hx_boolean increment)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (self->target_cpu == HOOX_CPU_AMD64)
  {
    if (target == HOOX_HX_PTR_QWORD)
      hoox_x86_writer_put_u8 (self, 0x48 | (ri.index_is_extended ? 0x01 : 0x00));
    else if (ri.index_is_extended)
      hoox_x86_writer_put_u8 (self, 0x41);
  }

  switch (target)
  {
    case HOOX_HX_PTR_BYTE:
      hoox_x86_writer_put_u8 (self, 0xfe);
      break;
    case HOOX_HX_PTR_QWORD:
      if (self->target_cpu != HOOX_CPU_AMD64)
        return FALSE;
    case HOOX_HX_PTR_DWORD:
      hoox_x86_writer_put_u8 (self, 0xff);
      break;
  }

  hoox_x86_writer_put_u8 (self, (increment ? 0x00 : 0x08) | ri.index);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_lock_xadd_reg_ptr_reg (HooxX86Writer * self,
                                          HooxX86Reg dst_reg,
                                          HooxX86Reg src_reg)
{
  HooxX86RegInfo dst, src;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);
  hoox_x86_writer_describe_cpu_reg (self, src_reg, &src);

  hoox_x86_writer_put_u8 (self, 0xf0); /* lock prefix */

  if (!hoox_x86_writer_put_prefix_for_registers (self, &src, 32, &dst, &src,
      NULL))
    return FALSE;

  self->code[0] = 0x0f;
  self->code[1] = 0xc1;
  self->code[2] = 0x00 | (src.index << 3) | dst.index;
  hoox_x86_writer_commit (self, 3);

  if (dst.meta == HOOX_HX_META_XSP)
  {
    hoox_x86_writer_put_u8 (self, 0x24);
  }
  else if (dst.meta == HOOX_HX_META_XBP)
  {
    self->code[-1] |= 0x40;
    hoox_x86_writer_put_u8 (self, 0x00);
  }

  return TRUE;
}

static hx_boolean
hoox_x86_writer_put_lock_inc_or_dec_imm32_ptr (HooxX86Writer * self,
                                              hx_pointer target,
                                              hx_boolean increment)
{
  self->code[0] = 0xf0;
  self->code[1] = 0xff;
  self->code[2] = increment ? 0x05 : 0x0d;

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    *((hx_uint32 *) (self->code + 3)) = HX_UINT32_TO_LE (HX_POINTER_TO_SIZE (target));
  }
  else
  {
    hx_int64 distance = (hx_ssize) target - (hx_ssize) (self->pc + 7);
    if (!HOOX_IS_WITHIN_INT32_RANGE (distance))
      return FALSE;
    *((hx_int32 *) (self->code + 3)) = HX_INT32_TO_LE (distance);
  }

  hoox_x86_writer_commit (self, 7);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_lock_inc_imm32_ptr (HooxX86Writer * self,
                                       hx_pointer target)
{
  return hoox_x86_writer_put_lock_inc_or_dec_imm32_ptr (self, target, TRUE);
}

hx_boolean
hoox_x86_writer_put_lock_dec_imm32_ptr (HooxX86Writer * self,
                                       hx_pointer target)
{
  return hoox_x86_writer_put_lock_inc_or_dec_imm32_ptr (self, target, FALSE);
}

hx_boolean
hoox_x86_writer_put_and_reg_reg (HooxX86Writer * self,
                                HooxX86Reg dst_reg,
                                HooxX86Reg src_reg)
{
  HooxX86RegInfo dst, src;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);
  hoox_x86_writer_describe_cpu_reg (self, src_reg, &src);

  if (dst.width != src.width)
    return FALSE;
  if (dst.index_is_extended || src.index_is_extended)
    return FALSE;

  if (!hoox_x86_writer_put_prefix_for_reg_info (self, &dst, 0))
    return FALSE;

  self->code[0] = 0x21;
  self->code[1] = 0xc0 | (src.index << 3) | dst.index;
  hoox_x86_writer_commit (self, 2);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_and_reg_u32 (HooxX86Writer * self,
                                HooxX86Reg reg,
                                hx_uint32 imm_value)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 32, &ri, NULL))
    return FALSE;

  if (ri.meta == HOOX_HX_META_XAX)
  {
    self->code[0] = 0x25;
    *((hx_uint32 *) (self->code + 1)) = HX_UINT32_TO_LE (imm_value);
    hoox_x86_writer_commit (self, 5);
  }
  else
  {
    self->code[0] = 0x81;
    self->code[1] = 0xe0 | ri.index;
    *((hx_uint32 *) (self->code + 2)) = HX_UINT32_TO_LE (imm_value);
    hoox_x86_writer_commit (self, 6);
  }

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_shl_reg_u8 (HooxX86Writer * self,
                               HooxX86Reg reg,
                               hx_uint8 imm_value)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 32, &ri, NULL))
    return FALSE;

  self->code[0] = 0xc1;
  self->code[1] = 0xe0 | ri.index;
  self->code[2] = imm_value;
  hoox_x86_writer_commit (self, 3);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_mov_reg_reg (HooxX86Writer * self,
                                HooxX86Reg dst_reg,
                                HooxX86Reg src_reg)
{
  HooxX86RegInfo dst, src;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);
  hoox_x86_writer_describe_cpu_reg (self, src_reg, &src);

  if (dst.width != src.width)
    return FALSE;

  if (!hoox_x86_writer_put_prefix_for_registers (self, &dst, 32, &dst, &src,
      NULL))
    return FALSE;

  self->code[0] = 0x89;
  self->code[1] = 0xc0 | (src.index << 3) | dst.index;
  hoox_x86_writer_commit (self, 2);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_mov_reg_u32 (HooxX86Writer * self,
                                HooxX86Reg dst_reg,
                                hx_uint32 imm_value)
{
  HooxX86RegInfo dst;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);

  if (dst.width != 32)
    return FALSE;

  if (!hoox_x86_writer_put_prefix_for_reg_info (self, &dst, 0))
    return FALSE;

  self->code[0] = 0xb8 | dst.index;
  *((hx_uint32 *) (self->code + 1)) = HX_UINT32_TO_LE (imm_value);
  hoox_x86_writer_commit (self, 5);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_mov_reg_u64 (HooxX86Writer * self,
                                HooxX86Reg dst_reg,
                                hx_uint64 imm_value)
{
  HooxX86RegInfo dst;

  if (self->target_cpu != HOOX_CPU_AMD64)
    return FALSE;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);

  if (dst.width != 64)
    return FALSE;

  if (!hoox_x86_writer_put_prefix_for_reg_info (self, &dst, 0))
    return FALSE;

  self->code[0] = 0xb8 | dst.index;
  *((hx_uint64 *) (self->code + 1)) = HX_UINT64_TO_LE (imm_value);
  hoox_x86_writer_commit (self, 9);

  return TRUE;
}

void
hoox_x86_writer_put_mov_reg_address (HooxX86Writer * self,
                                    HooxX86Reg dst_reg,
                                    HooxAddress address)
{
  HooxX86RegInfo dst;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);

  if (dst.width == 32)
    hoox_x86_writer_put_mov_reg_u32 (self, dst_reg, (hx_uint32) address);
  else
    hoox_x86_writer_put_mov_reg_u64 (self, dst_reg, (hx_uint64) address);
}

hx_boolean
hoox_x86_writer_put_mov_reg_offset_ptr_u32 (HooxX86Writer * self,
                                           HooxX86Reg dst_reg,
                                           hx_ssize dst_offset,
                                           hx_uint32 imm_value)
{
  HooxX86RegInfo dst;
  hx_boolean offset_fits_in_i8;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (dst.width != 32)
      return FALSE;
  }
  else
  {
    if (dst.width != 64)
      return FALSE;
  }

  offset_fits_in_i8 = HOOX_IS_WITHIN_INT8_RANGE (dst_offset);

  hoox_x86_writer_put_u8 (self, 0xc7);

  if (dst_offset == 0 && dst.meta != HOOX_HX_META_XBP)
  {
    hoox_x86_writer_put_u8 (self, 0x00 | dst.index);
    if (dst.meta == HOOX_HX_META_XSP)
      hoox_x86_writer_put_u8 (self, 0x24);
  }
  else
  {
    hoox_x86_writer_put_u8 (self,
        (offset_fits_in_i8 ? 0x40 : 0x80) | dst.index);

    if (dst.meta == HOOX_HX_META_XSP)
      hoox_x86_writer_put_u8 (self, 0x24);

    if (offset_fits_in_i8)
    {
      hoox_x86_writer_put_u8 (self, (hx_uint8) dst_offset);
    }
    else
    {
      *((hx_int32 *) self->code) = HX_INT32_TO_LE (dst_offset);
      hoox_x86_writer_commit (self, 4);
    }
  }

  *((hx_uint32 *) self->code) = HX_UINT32_TO_LE (imm_value);
  hoox_x86_writer_commit (self, 4);

  return TRUE;
}

void
hoox_x86_writer_put_mov_reg_ptr_reg (HooxX86Writer * self,
                                    HooxX86Reg dst_reg,
                                    HooxX86Reg src_reg)
{
  hoox_x86_writer_put_mov_reg_offset_ptr_reg (self, dst_reg, 0, src_reg);
}

hx_boolean
hoox_x86_writer_put_mov_reg_offset_ptr_reg (HooxX86Writer * self,
                                           HooxX86Reg dst_reg,
                                           hx_ssize dst_offset,
                                           HooxX86Reg src_reg)
{
  HooxX86RegInfo dst, src;
  hx_boolean offset_fits_in_i8;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);
  hoox_x86_writer_describe_cpu_reg (self, src_reg, &src);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (dst.width != 32 || src.width != 32)
      return FALSE;
  }
  else
  {
    if (dst.width != 64)
      return FALSE;
  }

  offset_fits_in_i8 = HOOX_IS_WITHIN_INT8_RANGE (dst_offset);

  if (!hoox_x86_writer_put_prefix_for_registers (self, &src, 32, &dst, &src,
      NULL))
    return FALSE;

  hoox_x86_writer_put_u8 (self, 0x89);

  if (dst_offset == 0 && dst.meta != HOOX_HX_META_XBP)
  {
    hoox_x86_writer_put_u8 (self, 0x00 | (src.index << 3) | dst.index);
    if (dst.meta == HOOX_HX_META_XSP)
      hoox_x86_writer_put_u8 (self, 0x24);
  }
  else
  {
    hoox_x86_writer_put_u8 (self, (offset_fits_in_i8 ? 0x40 : 0x80) |
        (src.index << 3) | dst.index);

    if (dst.meta == HOOX_HX_META_XSP)
      hoox_x86_writer_put_u8 (self, 0x24);

    if (offset_fits_in_i8)
    {
      hoox_x86_writer_put_s8 (self, (hx_int8) dst_offset);
    }
    else
    {
      *((hx_int32 *) self->code) = HX_INT32_TO_LE (dst_offset);
      hoox_x86_writer_commit (self, 4);
    }
  }

  return TRUE;
}

void
hoox_x86_writer_put_mov_reg_reg_ptr (HooxX86Writer * self,
                                    HooxX86Reg dst_reg,
                                    HooxX86Reg src_reg)
{
  hoox_x86_writer_put_mov_reg_reg_offset_ptr (self, dst_reg, src_reg, 0);
}

hx_boolean
hoox_x86_writer_put_mov_reg_reg_offset_ptr (HooxX86Writer * self,
                                           HooxX86Reg dst_reg,
                                           HooxX86Reg src_reg,
                                           hx_ssize src_offset)
{
  HooxX86RegInfo dst, src;
  hx_boolean offset_fits_in_i8;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);
  hoox_x86_writer_describe_cpu_reg (self, src_reg, &src);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (dst.width != 32 || src.width != 32)
      return FALSE;
  }
  else
  {
    if (src.width != 64)
      return FALSE;
  }

  offset_fits_in_i8 = HOOX_IS_WITHIN_INT8_RANGE (src_offset);

  if (!hoox_x86_writer_put_prefix_for_registers (self, &dst, 32, &src, &dst,
      NULL))
    return FALSE;

  self->code[0] = 0x8b;
  self->code[1] = ((offset_fits_in_i8) ? 0x40 : 0x80)
      | (dst.index << 3) | src.index;
  hoox_x86_writer_commit (self, 2);

  if (src.meta == HOOX_HX_META_XSP)
    hoox_x86_writer_put_u8 (self, 0x24);

  if (offset_fits_in_i8)
  {
    hoox_x86_writer_put_s8 (self, (hx_int8) src_offset);
  }
  else
  {
    *((hx_int32 *) self->code) = HX_INT32_TO_LE (src_offset);
    hoox_x86_writer_commit (self, 4);
  }

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_mov_reg_near_ptr (HooxX86Writer * self,
                                     HooxX86Reg dst_reg,
                                     HooxAddress src_address)
{
  HooxX86RegInfo dst;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);

  if (!hoox_x86_writer_put_prefix_for_registers (self, &dst, 32, &dst, NULL))
    return FALSE;

  if (self->target_cpu == HOOX_CPU_IA32 && dst.meta == HOOX_HX_META_XAX)
  {
    hoox_x86_writer_put_u8 (self, 0xa1);
  }
  else
  {
    self->code[0] = 0x8b;
    self->code[1] = (dst.index << 3) | 0x05;
    hoox_x86_writer_commit (self, 2);
  }

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (src_address > HX_MAXUINT32)
      return FALSE;
    *((hx_uint32 *) self->code) = HX_UINT32_TO_LE ((hx_uint32) src_address);
  }
  else
  {
    hx_int64 distance = (hx_int64) src_address - (hx_int64) (self->pc + 4);
    if (distance < HX_MININT32 || distance > HX_MAXINT32)
      return FALSE;
    *((hx_int32 *) self->code) = HX_INT32_TO_LE ((hx_int32) distance);
  }
  hoox_x86_writer_commit (self, 4);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_mov_near_ptr_reg (HooxX86Writer * self,
                                     HooxAddress dst_address,
                                     HooxX86Reg src_reg)
{
  HooxX86RegInfo src;

  hoox_x86_writer_describe_cpu_reg (self, src_reg, &src);

  if (!hoox_x86_writer_put_prefix_for_registers (self, &src, 32, &src, NULL))
    return FALSE;

  if (self->target_cpu == HOOX_CPU_IA32 && src.meta == HOOX_HX_META_XAX)
  {
    hoox_x86_writer_put_u8 (self, 0xa3);
  }
  else
  {
    self->code[0] = 0x89;
    self->code[1] = (src.index << 3) | 0x05;
    hoox_x86_writer_commit (self, 2);
  }

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (dst_address > HX_MAXUINT32)
      return FALSE;
    *((hx_uint32 *) self->code) = HX_UINT32_TO_LE ((hx_uint32) dst_address);
  }
  else
  {
    hx_int64 distance = (hx_int64) dst_address - (hx_int64) (self->pc + 4);
    if (distance < HX_MININT32 || distance > HX_MAXINT32)
      return FALSE;
    *((hx_int32 *) self->code) = HX_INT32_TO_LE ((hx_int32) distance);
  }
  hoox_x86_writer_commit (self, 4);

  return TRUE;
}

static hx_boolean
hoox_x86_writer_put_mov_reg_imm_ptr (HooxX86Writer * self,
                                    HooxX86Reg dst_reg,
                                    hx_uint32 address)
{
  HooxX86RegInfo dst;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);

  if (!hoox_x86_writer_put_prefix_for_registers (self, &dst, 32, &dst, NULL))
    return FALSE;

  self->code[0] = 0x8b;
  self->code[1] = (dst.index << 3) | 0x04;
  self->code[2] = 0x25;
  *((hx_uint32 *) (self->code + 3)) = HX_UINT32_TO_LE (address);
  hoox_x86_writer_commit (self, 7);

  return TRUE;
}

static hx_boolean
hoox_x86_writer_put_mov_imm_ptr_reg (HooxX86Writer * self,
                                    hx_uint32 address,
                                    HooxX86Reg src_reg)
{
  HooxX86RegInfo src;

  hoox_x86_writer_describe_cpu_reg (self, src_reg, &src);

  if (!hoox_x86_writer_put_prefix_for_registers (self, &src, 32, &src, NULL))
    return FALSE;

  self->code[0] = 0x89;
  self->code[1] = (src.index << 3) | 0x04;
  self->code[2] = 0x25;
  *((hx_uint32 *) (self->code + 3)) = HX_UINT32_TO_LE (address);
  hoox_x86_writer_commit (self, 7);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_lea_reg_reg_offset (HooxX86Writer * self,
                                       HooxX86Reg dst_reg,
                                       HooxX86Reg src_reg,
                                       hx_ssize src_offset)
{
  HooxX86RegInfo dst, src;

  hoox_x86_writer_describe_cpu_reg (self, dst_reg, &dst);
  hoox_x86_writer_describe_cpu_reg (self, src_reg, &src);

  if (dst.index_is_extended || src.index_is_extended)
    return FALSE;

  if (self->target_cpu == HOOX_CPU_AMD64)
  {
    if (src.width == 32)
      hoox_x86_writer_put_u8 (self, 0x67);
    if (dst.width == 64)
      hoox_x86_writer_put_u8 (self, 0x48);
  }

  self->code[0] = 0x8d;
  self->code[1] = 0x80 | (dst.index << 3) | src.index;
  hoox_x86_writer_commit (self, 2);

  if (src.meta == HOOX_HX_META_XSP)
    hoox_x86_writer_put_u8 (self, 0x24);

  *((hx_int32 *) self->code) = HX_INT32_TO_LE (src_offset);
  hoox_x86_writer_commit (self, 4);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_xchg_reg_reg_ptr (HooxX86Writer * self,
                                     HooxX86Reg left_reg,
                                     HooxX86Reg right_reg)
{
  HooxX86RegInfo left, right;

  hoox_x86_writer_describe_cpu_reg (self, left_reg, &left);
  hoox_x86_writer_describe_cpu_reg (self, right_reg, &right);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (right.width != 32)
      return FALSE;
  }
  else
  {
    if (right.width != 64)
      return FALSE;
  }

  if (!hoox_x86_writer_put_prefix_for_reg_info (self, &left, 1))
    return FALSE;

  self->code[0] = 0x87;
  self->code[1] = 0x00 | (left.index << 3) | right.index;
  hoox_x86_writer_commit (self, 2);

  if (right.meta == HOOX_HX_META_XSP)
  {
    hoox_x86_writer_put_u8 (self, 0x24);
  }
  else if (right.meta == HOOX_HX_META_XBP)
  {
    self->code[-1] |= 0x40;
    hoox_x86_writer_put_u8 (self, 0x00);
  }

  return TRUE;
}

void
hoox_x86_writer_put_push_u32 (HooxX86Writer * self,
                             hx_uint32 imm_value)
{
  self->code[0] = 0x68;
  *((hx_uint32 *) (self->code + 1)) = HX_UINT32_TO_LE (imm_value);
  hoox_x86_writer_commit (self, 5);
}

hx_boolean
hoox_x86_writer_put_push_near_ptr (HooxX86Writer * self,
                                  HooxAddress address)
{
  self->code[0] = 0xff;
  self->code[1] = 0x35;

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (address > HX_MAXUINT32)
      return FALSE;
    *((hx_uint32 *) (self->code + 2)) = HX_UINT32_TO_LE ((hx_uint32) address);
  }
  else
  {
    hx_int64 distance = (hx_int64) address - (hx_int64) (self->pc + 6);
    if (distance < HX_MININT32 || distance > HX_MAXINT32)
      return FALSE;
    *((hx_int32 *) (self->code + 2)) = HX_INT32_TO_LE ((hx_int32) distance);
  }

  hoox_x86_writer_commit (self, 6);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_push_reg (HooxX86Writer * self,
                             HooxX86Reg reg)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (ri.width != 32)
      return FALSE;
  }
  else
  {
    if (ri.width != 64)
      return FALSE;
  }

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 64, &ri, NULL))
    return FALSE;

  hoox_x86_writer_put_u8 (self, 0x50 | ri.index);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_pop_reg (HooxX86Writer * self,
                            HooxX86Reg reg)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (ri.width != 32)
      return FALSE;
  }
  else
  {
    if (ri.width != 64)
      return FALSE;
  }

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 64, &ri, NULL))
    return FALSE;

  hoox_x86_writer_put_u8 (self, 0x58 | ri.index);

  return TRUE;
}

void
hoox_x86_writer_put_pushax (HooxX86Writer * self)
{
  if (self->target_cpu == HOOX_CPU_IA32)
  {
    hoox_x86_writer_put_u8 (self, 0x60);
  }
  else
  {
    hoox_x86_writer_put_push_reg (self, HOOX_HX_RAX);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_RCX);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_RDX);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_RBX);

    hoox_x86_writer_put_lea_reg_reg_offset (self, HOOX_HX_RAX,
        HOOX_HX_RSP, 4 * 8);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_RAX);
    hoox_x86_writer_put_mov_reg_reg_offset_ptr (self, HOOX_HX_RAX,
        HOOX_HX_RSP, 4 * 8);

    hoox_x86_writer_put_push_reg (self, HOOX_HX_RBP);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_RSI);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_RDI);

    hoox_x86_writer_put_push_reg (self, HOOX_HX_R8);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_R9);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_R10);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_R11);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_R12);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_R13);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_R14);
    hoox_x86_writer_put_push_reg (self, HOOX_HX_R15);
  }
}

void
hoox_x86_writer_put_popax (HooxX86Writer * self)
{
  if (self->target_cpu == HOOX_CPU_IA32)
  {
    hoox_x86_writer_put_u8 (self, 0x61);
  }
  else
  {
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_R15);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_R14);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_R13);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_R12);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_R11);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_R10);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_R9);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_R8);

    hoox_x86_writer_put_pop_reg (self, HOOX_HX_RDI);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_RSI);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_RBP);
    hoox_x86_writer_put_lea_reg_reg_offset (self, HOOX_HX_RSP, HOOX_HX_RSP, 8);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_RBX);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_RDX);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_RCX);
    hoox_x86_writer_put_pop_reg (self, HOOX_HX_RAX);
  }
}

void
hoox_x86_writer_put_pushfx (HooxX86Writer * self)
{
  hoox_x86_writer_put_u8 (self, 0x9c);
}

void
hoox_x86_writer_put_popfx (HooxX86Writer * self)
{
  hoox_x86_writer_put_u8 (self, 0x9d);
}

hx_boolean
hoox_x86_writer_put_test_reg_reg (HooxX86Writer * self,
                                 HooxX86Reg reg_a,
                                 HooxX86Reg reg_b)
{
  HooxX86RegInfo a, b;

  hoox_x86_writer_describe_cpu_reg (self, reg_a, &a);
  hoox_x86_writer_describe_cpu_reg (self, reg_b, &b);

  if (a.width != b.width)
    return FALSE;

  if (!hoox_x86_writer_put_prefix_for_registers (self, &a, 32, &a, &b, NULL))
    return FALSE;

  self->code[0] = 0x85;
  self->code[1] = 0xc0 | (b.index << 3) | a.index;
  hoox_x86_writer_commit (self, 2);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_cmp_reg_i32 (HooxX86Writer * self,
                                HooxX86Reg reg,
                                hx_int32 imm_value)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 32, &ri, NULL))
    return FALSE;

  if (ri.meta == HOOX_HX_META_XAX)
  {
    hoox_x86_writer_put_u8 (self, 0x3d);
  }
  else
  {
    self->code[0] = 0x81;
    self->code[1] = 0xf8 | ri.index;
    hoox_x86_writer_commit (self, 2);
  }

  *((hx_int32 *) self->code) = HX_INT32_TO_LE (imm_value);
  hoox_x86_writer_commit (self, 4);

  return TRUE;
}

hx_boolean
hoox_x86_writer_put_cmp_reg_offset_ptr_reg (HooxX86Writer * self,
                                           HooxX86Reg reg_a,
                                           hx_ssize offset,
                                           HooxX86Reg reg_b)
{
  HooxX86RegInfo a, b;
  hx_boolean offset_fits_in_i8;

  hoox_x86_writer_describe_cpu_reg (self, reg_a, &a);
  hoox_x86_writer_describe_cpu_reg (self, reg_b, &b);

  if (!hoox_x86_writer_put_prefix_for_registers (self, &a, 32, &a, &b, NULL))
    return FALSE;

  offset_fits_in_i8 = HOOX_IS_WITHIN_INT8_RANGE (offset);

  self->code[0] = 0x39;
  self->code[1] = (offset_fits_in_i8 ? 0x40 : 0x80) | (b.index << 3) | a.index;
  hoox_x86_writer_commit (self, 2);

  if (a.index == 4)
    hoox_x86_writer_put_u8 (self, 0x24);

  if (offset_fits_in_i8)
  {
    hoox_x86_writer_put_s8 (self, (hx_int8) offset);
  }
  else
  {
    *((hx_int32 *) self->code) = HX_INT32_TO_LE (offset);
    hoox_x86_writer_commit (self, 4);
  }

  return TRUE;
}

void
hoox_x86_writer_put_cld (HooxX86Writer * self)
{
  hoox_x86_writer_put_u8 (self, 0xfc);
}

void
hoox_x86_writer_put_nop (HooxX86Writer * self)
{
  hoox_x86_writer_put_u8 (self, 0x90);
}

void
hoox_x86_writer_put_breakpoint (HooxX86Writer * self)
{
  hoox_x86_writer_put_u8 (self, 0xcc);
}

/*
 * Whilst the 0x90 opcode for NOP is commonly known, the Intel Optimization
 * Manual actually lists a number of different NOP instructions ranging from
 * one to nine bytes in length. By using longer NOP instructions, we can more
 * efficiently pad unused space with the processor being able to skip more
 * bytes per execution cycle.
 */
void
hoox_x86_writer_put_nop_padding (HooxX86Writer * self,
                                hx_uint n)
{
  static const struct {
    hx_uint8 one[1];
    hx_uint8 two[2];
    hx_uint8 three[3];
    hx_uint8 four[4];
    hx_uint8 five[5];
    hx_uint8 six[6];
    hx_uint8 seven[7];
    hx_uint8 eight[8];
    hx_uint8 nine[9];
  } nops = {
    /* NOP */
    .one =   { 0x90 },
    /* 66 NOP */
    .two =   { 0x66, 0x90 },
    /* NOP DWORD ptr [EAX] */
    .three = { 0x0f, 0x1f, 0x00 },
    /* NOP DWORD ptr [EAX + 00H] */
    .four =  { 0x0f, 0x1f, 0x40, 0x00 },
    /* NOP DWORD ptr [EAX + EAX*1 + 00H] */
    .five =  { 0x0f, 0x1f, 0x44, 0x00, 0x00 },
    /* 66 NOP DWORD ptr [EAX + EAX*1 + 00H] */
    .six =   { 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00 },
    /* NOP DWORD ptr [EAX + 00000000H] */
    .seven = { 0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00 },
    /* NOP DWORD ptr [EAX + EAX*1 + 00000000H] */
    .eight = { 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* 66 NOP DWORD ptr [EAX + EAX*1 + 00000000H] */
    .nine =  { 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
  };
  static const hx_uint8 * nop_index[9] = {
    nops.one,
    nops.two,
    nops.three,
    nops.four,
    nops.five,
    nops.six,
    nops.seven,
    nops.eight,
    nops.nine,
  };
  static const hx_uint max_nop = HX_N_ELEMENTS (nop_index);
  hx_uint remaining;

  for (remaining = n; remaining != 0; remaining -= max_nop)
  {
    if (remaining < max_nop)
    {
      memcpy (self->code, nop_index[remaining - 1], remaining);
      hoox_x86_writer_commit (self, remaining);
      break;
    }

    memcpy (self->code, nop_index[max_nop - 1], max_nop);
    hoox_x86_writer_commit (self, max_nop);
  }
}

hx_boolean
hoox_x86_writer_put_fxsave_reg_ptr (HooxX86Writer * self,
                                   HooxX86Reg reg)
{
  return hoox_x86_writer_put_fx_save_or_restore_reg_ptr (self, 0, reg);
}

hx_boolean
hoox_x86_writer_put_fxrstor_reg_ptr (HooxX86Writer * self,
                                    HooxX86Reg reg)
{
  return hoox_x86_writer_put_fx_save_or_restore_reg_ptr (self, 1, reg);
}

static hx_boolean
hoox_x86_writer_put_fx_save_or_restore_reg_ptr (HooxX86Writer * self,
                                               hx_uint8 operation,
                                               HooxX86Reg reg)
{
  HooxX86RegInfo ri;

  hoox_x86_writer_describe_cpu_reg (self, reg, &ri);

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (ri.width != 32 || ri.index_is_extended)
      return FALSE;
  }
  else
  {
    if (ri.width != 64)
      return FALSE;
  }

  if (!hoox_x86_writer_put_prefix_for_registers (self, &ri, 64, &ri, NULL))
    return FALSE;

  self->code[0] = 0x0f;
  self->code[1] = 0xae;
  self->code[2] = (operation << 3) | ri.index;
  hoox_x86_writer_commit (self, 3);

  if (ri.index == 4)
    hoox_x86_writer_put_u8 (self, 0x24);

  return TRUE;
}

void
hoox_x86_writer_put_u8 (HooxX86Writer * self,
                       hx_uint8 value)
{
  *self->code = value;
  hoox_x86_writer_commit (self, 1);
}

void
hoox_x86_writer_put_s8 (HooxX86Writer * self,
                       hx_int8 value)
{
  *((hx_int8 *) self->code) = value;
  hoox_x86_writer_commit (self, 1);
}

void
hoox_x86_writer_put_bytes (HooxX86Writer * self,
                          const hx_uint8 * data,
                          hx_uint n)
{
  memcpy (self->code, data, n);
  hoox_x86_writer_commit (self, n);
}

static void
hoox_x86_writer_describe_cpu_reg (HooxX86Writer * self,
                                 HooxX86Reg reg,
                                 HooxX86RegInfo * ri)
{
  if (reg >= HOOX_HX_XAX && reg <= HOOX_HX_XDI)
  {
    if (self->target_cpu == HOOX_CPU_IA32)
      reg = (HooxX86Reg) (HOOX_HX_EAX + reg - HOOX_HX_XAX);
    else
      reg = (HooxX86Reg) (HOOX_HX_RAX + reg - HOOX_HX_XAX);
  }

  ri->meta = hoox_meta_reg_from_cpu_reg (reg);

  if (reg >= HOOX_HX_RAX && reg <= HOOX_HX_R15)
  {
    ri->width = 64;

    if (reg < HOOX_HX_R8)
    {
      ri->index = reg - HOOX_HX_RAX;
      ri->index_is_extended = FALSE;
    }
    else
    {
      ri->index = reg - HOOX_HX_R8;
      ri->index_is_extended = TRUE;
    }
  }
  else
  {
    ri->width = 32;

    if (reg < HOOX_HX_R8D)
    {
      ri->index = reg - HOOX_HX_EAX;
      ri->index_is_extended = FALSE;
    }
    else
    {
      ri->index = reg - HOOX_HX_R8D;
      ri->index_is_extended = TRUE;
    }
  }
}

static HooxX86MetaReg
hoox_meta_reg_from_cpu_reg (HooxX86Reg reg)
{
  if (reg >= HOOX_HX_EAX && reg <= HOOX_HX_R15D)
    return (HooxX86MetaReg) (HOOX_HX_META_XAX + reg - HOOX_HX_EAX);

  if (reg >= HOOX_HX_RAX && reg <= HOOX_HX_R15)
    return (HooxX86MetaReg) (HOOX_HX_META_XAX + reg - HOOX_HX_RAX);

  return (HooxX86MetaReg) (HOOX_HX_META_XAX + reg - HOOX_HX_XAX);
}

static hx_boolean
hoox_x86_writer_put_prefix_for_reg_info (HooxX86Writer * self,
                                        const HooxX86RegInfo * ri,
                                        hx_uint operand_index)
{
  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (ri->width != 32 || ri->index_is_extended)
      return FALSE;
  }
  else
  {
    hx_uint mask;

    mask = 1 << (operand_index * 2);

    if (ri->width == 32)
    {
      if (ri->index_is_extended)
        hoox_x86_writer_put_u8 (self, 0x40 | mask);
    }
    else
    {
      hoox_x86_writer_put_u8 (self,
          (ri->index_is_extended) ? 0x48 | mask : 0x48);
    }
  }

  return TRUE;
}

/* TODO: improve this function and get rid of the one above */
static hx_boolean
hoox_x86_writer_put_prefix_for_registers (HooxX86Writer * self,
                                         const HooxX86RegInfo * width_reg,
                                         hx_uint default_width,
                                         ...)
{
  const HooxX86RegInfo * ra, * rb, * rc;
  va_list args;

  va_start (args, default_width);

  ra = va_arg (args, const HooxX86RegInfo *);
  hx_assert (ra != NULL);

  rb = va_arg (args, const HooxX86RegInfo *);
  if (rb != NULL)
  {
    rc = va_arg (args, const HooxX86RegInfo *);
  }
  else
  {
    rc = NULL;
  }

  if (self->target_cpu == HOOX_CPU_IA32)
  {
    if (ra->width != 32 || ra->index_is_extended)
      return FALSE;
    if (rb != NULL && (rb->width != 32 || rb->index_is_extended))
      return FALSE;
    if (rc != NULL && (rc->width != 32 || rc->index_is_extended))
      return FALSE;
  }
  else
  {
    hx_uint nibble = 0;

    if (width_reg->width != default_width)
      nibble |= 0x8;
    if (rb != NULL && rb->index_is_extended)
      nibble |= 0x4;
    if (rc != NULL && rc->index_is_extended)
      nibble |= 0x2;
    if (ra->index_is_extended)
      nibble |= 0x1;

    if (nibble != 0)
      hoox_x86_writer_put_u8 (self, 0x40 | nibble);
  }

  return TRUE;
}

static hx_uint8
hoox_get_jcc_opcode (hx_x86_insn instruction_id)
{
  switch (instruction_id)
  {
    case HX_INS_JO:
      return 0x70;
    case HX_INS_JNO:
      return 0x71;
    case HX_INS_JB:
      return 0x72;
    case HX_INS_JAE:
      return 0x73;
    case HX_INS_JE:
      return 0x74;
    case HX_INS_JNE:
      return 0x75;
    case HX_INS_JBE:
      return 0x76;
    case HX_INS_JA:
      return 0x77;
    case HX_INS_JS:
      return 0x78;
    case HX_INS_JNS:
      return 0x79;
    case HX_INS_JP:
      return 0x7a;
    case HX_INS_JNP:
      return 0x7b;
    case HX_INS_JL:
      return 0x7c;
    case HX_INS_JGE:
      return 0x7d;
    case HX_INS_JLE:
      return 0x7e;
    case HX_INS_JG:
      return 0x7f;
    case HX_INS_JCXZ:
    case HX_INS_JECXZ:
    case HX_INS_JRCXZ:
    default:
      return 0xe3;
  }
}
