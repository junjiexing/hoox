/*
 * Copyright (C) 2008-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2008 Christian Berentsen <jc.berentsen@gmail.com>
 * Copyright (C) 2024 Yannis Juglaret <yjuglaret@mozilla.com>
 * Copyright (C) 2025 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxinterceptor-priv.h"

#include "hooxmemory.h"
#include "hooxsysinternals.h"
#include "hooxx86reader.h"
#include "hooxx86relocator.h"

#include <string.h>

#define HOOX_INTERCEPTOR_FULL_REDIRECT_SIZE  16
#define HOOX_INTERCEPTOR_NEAR_REDIRECT_SIZE  5
#define HOOX_HX_JMP_MAX_DISTANCE            (HX_MAXINT32 - 16384)

#define HOOX_FRAME_OFFSET_CPU_CONTEXT 0
#define HOOX_FRAME_OFFSET_CPU_FLAGS \
    (HOOX_FRAME_OFFSET_CPU_CONTEXT + sizeof (HooxCpuContext))
#define HOOX_FRAME_OFFSET_NEXT_HOP \
    (HOOX_FRAME_OFFSET_CPU_FLAGS + sizeof (hx_pointer))
#define HOOX_FRAME_OFFSET_TOP \
    (HOOX_FRAME_OFFSET_NEXT_HOP + sizeof (hx_pointer))

#define HOOX_FCDATA(context) \
    ((HooxX86FunctionContextData *) (context)->backend_data.storage)

typedef struct _HooxX86FunctionContextData HooxX86FunctionContextData;

struct _HooxInterceptorBackend
{
  HooxCodeAllocator * allocator;

  HooxX86Writer writer;
  HooxX86Relocator relocator;

  HooxCodeSlice * enter_thunk;
  HooxCodeSlice * leave_thunk;
};

struct _HooxX86FunctionContextData
{
  hx_uint redirect_code_size;
  hx_pointer push_to_shadow_stack;
  hx_uint available_space;
};

HX_STATIC_ASSERT (sizeof (HooxX86FunctionContextData)
    <= sizeof (HooxFunctionContextBackendData));

static hx_boolean hoox_interceptor_backend_write_custom_redirect (
    HooxInterceptorBackend * self, HooxFunctionContext * ctx, hx_pointer target);
static void hoox_interceptor_backend_create_thunks (
    HooxInterceptorBackend * self);
static void hoox_interceptor_backend_destroy_thunks (
    HooxInterceptorBackend * self);

static void hoox_emit_enter_thunk (HooxX86Writer * cw, HooxAddress base_pc);
static void hoox_emit_leave_thunk (HooxX86Writer * cw);

static void hoox_emit_prolog (HooxX86Writer * cw, hx_ssize stack_displacement);
static void hoox_emit_epilog (HooxX86Writer * cw, HooxPointCut point_cut);

HooxInterceptorBackend *
_hoox_interceptor_backend_create (HxRecMutex * mutex,
                                 HooxCodeAllocator * allocator)
{
  HooxInterceptorBackend * backend;

  backend = hx_slice_new (HooxInterceptorBackend);
  backend->allocator = allocator;

  hoox_x86_writer_init (&backend->writer, NULL);
  hoox_x86_relocator_init (&backend->relocator, NULL, &backend->writer);

  hoox_interceptor_backend_create_thunks (backend);

  return backend;
}

void
_hoox_interceptor_backend_destroy (HooxInterceptorBackend * backend)
{
  hoox_interceptor_backend_destroy_thunks (backend);

  hoox_x86_relocator_clear (&backend->relocator);
  hoox_x86_writer_clear (&backend->writer);

  hx_slice_free (HooxInterceptorBackend, backend);
}

hx_boolean
_hoox_interceptor_backend_claim_grafted_trampoline (HooxInterceptorBackend * self,
                                                   HooxFunctionContext * ctx)
{
  return FALSE;
}

static hx_boolean
hoox_interceptor_backend_prepare_trampoline (HooxInterceptorBackend * self,
                                            HooxFunctionContext * ctx,
                                            hx_boolean force)
{
  HooxX86FunctionContextData * data = HOOX_FCDATA (ctx);
  HooxRelocationScenario scenario =
      (ctx->scenario == HOOX_INTERCEPTOR_SCENARIO_OFFLINE)
      ? HOOX_SCENARIO_OFFLINE
      : HOOX_SCENARIO_ONLINE;
#if HX_SIZEOF_VOID_P == 4
  data->redirect_code_size = HOOX_INTERCEPTOR_NEAR_REDIRECT_SIZE;

  ctx->trampoline_slice = hoox_code_allocator_alloc_slice (self->allocator);
#else
  HooxAddressSpec spec;
  hx_size default_alignment = 0;

  spec.near_address = ctx->function_address;
  spec.max_distance = HOOX_HX_JMP_MAX_DISTANCE;

  /*
   * When creating a fast interceptor, we won't be vectoring from the target
   * function to the trampoline slice, we will instead be re-directing direct to
   * the target replacement function and therefore must consider the worst case
   * scenario of a JMP with RIP-relative immediate embedded in the code stream.
   * We will still use the trampoline slice for writing the trampoline for the
   * original function in the event that the patched function wishes to call the
   * original. Thus it isn't important where the trampoline slice is located.
   *
   * When creating a normal interceptor, the patch to the target function
   * re-directs first to the on_enter trampoline written to the trampoline
   * slice. If we are able to allocate the slice nearby the target function,
   * then we are able to use a near rather than far jump and hence a shorter
   * op-code. This reduces the amount of the target function prologue which
   * needs to be over-written. If we cannot allocate nearby, however, we
   * just revert to assuming the worst case scenario.
   */
  if (ctx->type == HOOX_INTERCEPTOR_TYPE_DEFAULT)
  {
    ctx->trampoline_slice = hoox_code_allocator_try_alloc_slice_near (
        self->allocator, &spec, default_alignment);
  }

  if (ctx->trampoline_slice == NULL)
  {
    data->redirect_code_size = HOOX_INTERCEPTOR_FULL_REDIRECT_SIZE;

    ctx->trampoline_slice = hoox_code_allocator_alloc_slice (self->allocator);
  }
  else
  {
    data->redirect_code_size = HOOX_INTERCEPTOR_NEAR_REDIRECT_SIZE;
  }
#endif

  if (ctx->write_redirect != NULL)
  {
    hx_uint scan_bytes;

    scan_bytes = (ctx->redirect_space_hint != 0)
        ? ctx->redirect_space_hint
        : HOOX_INTERCEPTOR_MAX_REDIRECT_SIZE;
    hoox_x86_relocator_can_relocate (ctx->function_address, scan_bytes,
        scenario, &data->available_space);
    if (ctx->redirect_space_hint != 0 &&
        data->available_space > ctx->redirect_space_hint)
      data->available_space = ctx->redirect_space_hint;
    if (data->available_space == 0)
      goto not_enough_space;

    ctx->redirect_code = hx_malloc (data->available_space);

    return TRUE;
  }

  if (!force && !hoox_x86_relocator_can_relocate (ctx->function_address,
        data->redirect_code_size, scenario, NULL))
    goto not_enough_space;

  return TRUE;

not_enough_space:
  {
    hoox_code_slice_unref (ctx->trampoline_slice);
    ctx->trampoline_slice = NULL;
    return FALSE;
  }
}

hx_boolean
_hoox_interceptor_backend_create_trampoline (HooxInterceptorBackend * self,
                                            HooxFunctionContext * ctx,
                                            hx_boolean force)
{
  HooxX86Writer * cw = &self->writer;
  HooxX86Relocator * rl = &self->relocator;
  HooxX86FunctionContextData * data = HOOX_FCDATA (ctx);
  HooxAddress function_ctx_ptr;
  HooxAddress after_push_to_shadow_stack;
  hx_uint reloc_bytes;

  if (!hoox_interceptor_backend_prepare_trampoline (self, ctx, force))
    return FALSE;

  hoox_x86_writer_reset (cw, ctx->trampoline_slice->data);
  cw->pc = HOOX_ADDRESS (ctx->trampoline_slice->pc);

  if (ctx->type != HOOX_INTERCEPTOR_TYPE_FAST)
  {
    function_ctx_ptr =
        HOOX_ADDRESS (ctx->trampoline_slice->pc) + hoox_x86_writer_offset (cw);
    hoox_x86_writer_put_bytes (cw, (hx_uint8 *) &ctx,
        sizeof (HooxFunctionContext *));

    ctx->on_enter_trampoline =
        (hx_uint8 *) ctx->trampoline_slice->pc + hoox_x86_writer_offset (cw);

    hoox_x86_writer_put_push_near_ptr (cw, function_ctx_ptr);
    hoox_x86_writer_put_jmp_address (cw, HOOX_ADDRESS (self->enter_thunk->pc));

    if ((cw->cpu_features & HOOX_CPU_CET_SS) != 0)
    {
      /*
       * Jumping to push_to_shadow_stack will push the on_leave_trampoline
       * address onto the shadow stack, thereby making it a legit address to
       * return to. Then it will jump back through XAX.
       */

      after_push_to_shadow_stack =
          HOOX_ADDRESS (ctx->trampoline_slice->pc) + hoox_x86_writer_offset (cw);

      hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_XSP,
          HOOX_HX_XSP, (hx_ssize) sizeof (hx_pointer));

      hoox_x86_writer_put_jmp_reg (cw, HOOX_HX_XAX);

      data->push_to_shadow_stack =
          (hx_uint8 *) ctx->trampoline_slice->pc + hoox_x86_writer_offset (cw);

      hoox_x86_writer_put_call_address (cw, after_push_to_shadow_stack);
    }

    ctx->on_leave_trampoline =
        (hx_uint8 *) ctx->trampoline_slice->pc + hoox_x86_writer_offset (cw);

    hoox_x86_writer_put_push_near_ptr (cw, function_ctx_ptr);
    hoox_x86_writer_put_jmp_address (cw, HOOX_ADDRESS (self->leave_thunk->pc));

    hoox_x86_writer_flush (cw);
    hx_assert (hoox_x86_writer_offset (cw) <= ctx->trampoline_slice->size);
  }

  ctx->on_invoke_trampoline =
      (hx_uint8 *) ctx->trampoline_slice->pc + hoox_x86_writer_offset (cw);
  hoox_x86_relocator_reset (rl, ctx->function_address, cw);

  if (ctx->write_redirect != NULL)
  {
    hx_pointer redirect_target;

    redirect_target = (ctx->type == HOOX_INTERCEPTOR_TYPE_FAST)
        ? ctx->replacement_function
        : ctx->on_enter_trampoline;
    if (!hoox_interceptor_backend_write_custom_redirect (self, ctx,
          redirect_target))
      goto redirect_declined;
  }

  do
  {
    reloc_bytes = hoox_x86_relocator_read_one (rl, NULL);
    if (reloc_bytes == 0)
      reloc_bytes = data->redirect_code_size;
  }
  while (reloc_bytes < data->redirect_code_size);
  hoox_x86_relocator_write_all (rl);

  if (!hoox_x86_relocator_eoi (rl))
  {
    hoox_x86_writer_put_jmp_address (cw,
        HOOX_ADDRESS (ctx->function_address) + reloc_bytes);
  }

  hoox_x86_writer_flush (cw);
  hx_assert (hoox_x86_writer_offset (cw) <= ctx->trampoline_slice->size);

  ctx->overwritten_prologue_len = reloc_bytes;
  ctx->overwritten_prologue = hx_malloc (reloc_bytes);
  memcpy (ctx->overwritten_prologue, ctx->function_address, reloc_bytes);

  return TRUE;

redirect_declined:
  {
    hoox_code_slice_unref (ctx->trampoline_slice);
    ctx->trampoline_slice = NULL;
    return FALSE;
  }
}

static hx_boolean
hoox_interceptor_backend_write_custom_redirect (HooxInterceptorBackend * self,
                                               HooxFunctionContext * ctx,
                                               hx_pointer target)
{
  HooxX86FunctionContextData * data = HOOX_FCDATA (ctx);
  HooxRedirectWriteResult result;
  HooxX86Writer rw;
  HooxRedirectWriteDetails details;

  hoox_x86_writer_init (&rw, ctx->redirect_code);
  rw.pc = HOOX_ADDRESS (ctx->function_address);

  details.writer = &rw;
  details.target = target;
  details.scratch_register = ctx->scratch_register;
  details.capacity = data->available_space;

  result = ctx->write_redirect (&details, ctx->write_redirect_data);

  hoox_x86_writer_flush (&rw);
  data->redirect_code_size = hoox_x86_writer_offset (&rw);
  hoox_x86_writer_clear (&rw);

  hx_assert (data->redirect_code_size <= data->available_space);

  return result == HOOX_REDIRECT_WRITTEN;
}

void
_hoox_interceptor_backend_destroy_trampoline (HooxInterceptorBackend * self,
                                             HooxFunctionContext * ctx)
{
  hoox_code_slice_unref (ctx->trampoline_slice);
  ctx->trampoline_slice = NULL;
}

void
_hoox_interceptor_backend_activate_trampoline (HooxInterceptorBackend * self,
                                              HooxFunctionContext * ctx,
                                              hx_pointer prologue)
{
  HooxX86Writer * cw = &self->writer;
  HooxX86FunctionContextData * data = HOOX_FCDATA (ctx);
  hx_uint padding;

  hoox_x86_writer_reset (cw, prologue);
  cw->pc = HX_POINTER_TO_SIZE (ctx->function_address);

  if (ctx->write_redirect != NULL)
  {
    hoox_x86_writer_put_bytes (cw, ctx->redirect_code, data->redirect_code_size);
  }
  else if (ctx->type == HOOX_INTERCEPTOR_TYPE_FAST)
  {
    hoox_x86_writer_put_jmp_address (cw,
        HOOX_ADDRESS (ctx->replacement_function));
  }
  else
  {
    hoox_x86_writer_put_jmp_address (cw,
        HOOX_ADDRESS (ctx->on_enter_trampoline));
  }

  hoox_x86_writer_flush (cw);
  hx_assert (hoox_x86_writer_offset (cw) <= data->redirect_code_size);
  hx_assert (hoox_x86_writer_offset (cw) <= ctx->overwritten_prologue_len);

  padding = ctx->overwritten_prologue_len - hoox_x86_writer_offset (cw);
  hoox_x86_writer_put_nop_padding (cw, padding);
  hoox_x86_writer_flush (cw);
}

void
_hoox_interceptor_backend_deactivate_trampoline (HooxInterceptorBackend * self,
                                                HooxFunctionContext * ctx,
                                                hx_pointer prologue)
{
  memcpy (prologue, ctx->overwritten_prologue,
      ctx->overwritten_prologue_len);
}

hx_pointer
_hoox_interceptor_backend_get_function_address (HooxFunctionContext * ctx)
{
  return ctx->function_address;
}

hx_pointer
_hoox_interceptor_backend_resolve_redirect (HooxInterceptorBackend * self,
                                           hx_pointer address)
{
  hx_pointer target;

  target = hoox_x86_reader_try_get_relative_jump_target (address);
  if (target == NULL)
    target = hoox_x86_reader_try_get_indirect_jump_target (address);

  return target;
}

hx_size
_hoox_interceptor_backend_detect_hook_size (hx_constpointer code,
                                           hx_csh capstone,
                                           hx_insn * insn)
{
  hx_size hook_size;
  const uint8_t * cursor;
  size_t size;
  uint64_t addr;
  const hx_x86_op * dst;

  cursor = code;
  size = 16;
  addr = HX_POINTER_TO_SIZE (cursor);

  if (!hx_disasm_iter (capstone, &cursor, &size, &addr, insn))
    return 0;
  if (insn->id != HX_INS_JMP)
    return 0;
  dst = &insn->detail->x86.operands[0];
  switch (dst->type)
  {
    case HX_OP_IMM:
      hook_size = insn->size;
      break;
    case HX_OP_MEM:
      if (dst->mem.segment == HX_REG_INVALID &&
          dst->mem.base == HX_REG_RIP &&
          dst->mem.index == HX_REG_INVALID &&
          dst->mem.scale == 1 &&
          dst->mem.disp == 2)
      {
        const hx_size inline_data_size = dst->mem.disp + sizeof (hx_pointer);

        cursor += inline_data_size;
        addr += inline_data_size;

        hook_size = insn->size + inline_data_size;
      }
      else
      {
        return 0;
      }
      break;
    default:
      return 0;
  }

  while (hx_disasm_iter (capstone, &cursor, &size, &addr, insn) &&
      insn->id == HX_INS_NOP)
    hook_size += insn->size;

  return hook_size;
}

static void
hoox_interceptor_backend_create_thunks (HooxInterceptorBackend * self)
{
  HooxX86Writer * cw = &self->writer;

  self->enter_thunk = hoox_code_allocator_alloc_slice (self->allocator);
  hoox_x86_writer_reset (cw, self->enter_thunk->data);
  cw->pc = HOOX_ADDRESS (self->enter_thunk->pc);
  hoox_emit_enter_thunk (cw, cw->pc);
  hoox_x86_writer_flush (cw);
  hx_assert (hoox_x86_writer_offset (cw) <= self->enter_thunk->size);

  self->leave_thunk = hoox_code_allocator_alloc_slice (self->allocator);
  hoox_x86_writer_reset (cw, self->leave_thunk->data);
  cw->pc = HOOX_ADDRESS (self->leave_thunk->pc);
  hoox_emit_leave_thunk (cw);
  hoox_x86_writer_flush (cw);
  hx_assert (hoox_x86_writer_offset (cw) <= self->leave_thunk->size);
}

static void
hoox_interceptor_backend_destroy_thunks (HooxInterceptorBackend * self)
{
  hoox_code_slice_unref (self->leave_thunk);

  hoox_code_slice_unref (self->enter_thunk);
}

static void
hoox_emit_enter_thunk (HooxX86Writer * cw,
                      HooxAddress base_pc)
{
  const hx_ssize return_address_stack_displacement = 0;
  const hx_char * prepare_trap_on_leave = "prepare_trap_on_leave";
  HooxX86Reg function_ctx_reg = (sizeof (hx_pointer) == 8)
      ? HOOX_HX_R12
      : HOOX_HX_XDI;

  hoox_emit_prolog (cw, return_address_stack_displacement);

  hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_XSI,
      HOOX_HX_XBX, HOOX_FRAME_OFFSET_CPU_CONTEXT);
  hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_XDX,
      HOOX_HX_XBX, HOOX_FRAME_OFFSET_TOP);
  hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_XCX,
      HOOX_HX_XBX, HOOX_FRAME_OFFSET_NEXT_HOP);
  hoox_x86_writer_put_mov_reg_reg_ptr (cw, function_ctx_reg, HOOX_HX_XCX);

  hoox_x86_writer_put_call_address_with_aligned_arguments (cw, HOOX_CALL_CAPI,
      HOOX_ADDRESS (_hoox_function_context_begin_invocation), 4,
      HOOX_ARG_REGISTER, function_ctx_reg,
      HOOX_ARG_REGISTER, HOOX_HX_XSI,
      HOOX_ARG_REGISTER, HOOX_HX_XDX,
      HOOX_ARG_REGISTER, HOOX_HX_XCX);

  if ((cw->cpu_features & HOOX_CPU_CET_SS) != 0)
  {
    hx_pointer epilog;

    hoox_x86_writer_put_test_reg_reg (cw, HOOX_HX_EAX, HOOX_HX_EAX);
    hoox_x86_writer_put_jcc_short_label (cw, HX_INS_JNE, prepare_trap_on_leave,
        HOOX_NO_HINT);

    epilog = HX_SIZE_TO_POINTER (base_pc + hoox_x86_writer_offset (cw));
    hoox_emit_epilog (cw, HOOX_POINT_ENTER);

    hoox_x86_writer_put_label (cw, prepare_trap_on_leave);
    hoox_x86_writer_put_mov_reg_address (cw, HOOX_HX_XAX, HOOX_ADDRESS (epilog));
    hoox_x86_writer_put_jmp_reg_offset_ptr (cw, function_ctx_reg,
        HX_STRUCT_OFFSET (HooxFunctionContext, backend_data) +
        HX_STRUCT_OFFSET (HooxX86FunctionContextData, push_to_shadow_stack));
  }
  else
  {
    hoox_emit_epilog (cw, HOOX_POINT_ENTER);
  }
}

static void
hoox_emit_leave_thunk (HooxX86Writer * cw)
{
  const hx_ssize next_hop_stack_displacement = -((hx_ssize) sizeof (hx_pointer));

  hoox_emit_prolog (cw, next_hop_stack_displacement);

  hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_XSI,
      HOOX_HX_XBX, HOOX_FRAME_OFFSET_CPU_CONTEXT);
  hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_XDX,
      HOOX_HX_XBX, HOOX_FRAME_OFFSET_NEXT_HOP);
  hoox_x86_writer_put_mov_reg_reg_ptr (cw, HOOX_HX_XDI, HOOX_HX_XDX);

  hoox_x86_writer_put_call_address_with_aligned_arguments (cw, HOOX_CALL_CAPI,
      HOOX_ADDRESS (_hoox_function_context_end_invocation), 3,
      HOOX_ARG_REGISTER, HOOX_HX_XDI,
      HOOX_ARG_REGISTER, HOOX_HX_XSI,
      HOOX_ARG_REGISTER, HOOX_HX_XDX);

  hoox_emit_epilog (cw, HOOX_POINT_LEAVE);
}

static void
hoox_emit_prolog (HooxX86Writer * cw,
                 hx_ssize stack_displacement)
{
  hx_uint8 fxsave[] = {
    0x0f, 0xae, 0x04, 0x24 /* fxsave [esp] */
  };

  /*
   * Set up our stack frame:
   *
   * [function_ctx/next_hop] <-- already pushed before the branch to our thunk
   * [cpu_flags]
   * [cpu_context] <-- xbx points to the start of the cpu_context
   * [alignment_padding]
   * [extended_context]
   */
  hoox_x86_writer_put_pushfx (cw);
  hoox_x86_writer_put_cld (cw); /* C ABI mandates this */
  hoox_x86_writer_put_pushax (cw); /* all of HooxCpuContext except for xip */
  hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_XSP,
      HOOX_HX_XSP, -((hx_ssize) sizeof (hx_pointer))); /* HooxCpuContext.xip */

  /* fixup the HooxCpuContext stack pointer */
  hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_XAX,
      HOOX_HX_XSP, HOOX_FRAME_OFFSET_TOP + stack_displacement);
  hoox_x86_writer_put_mov_reg_offset_ptr_reg (cw,
      HOOX_HX_XSP, HOOX_CPU_CONTEXT_OFFSET_XSP,
      HOOX_HX_XAX);

  hoox_x86_writer_put_mov_reg_reg (cw, HOOX_HX_XBX, HOOX_HX_XSP);
  hoox_x86_writer_put_and_reg_u32 (cw, HOOX_HX_XSP, (hx_uint32) ~(16 - 1));
  hoox_x86_writer_put_sub_reg_imm (cw, HOOX_HX_XSP, 512);
  hoox_x86_writer_put_bytes (cw, fxsave, sizeof (fxsave));
}

static void
hoox_emit_epilog (HooxX86Writer * cw,
                 HooxPointCut point_cut)
{
  hx_uint8 fxrstor[] = {
    0x0f, 0xae, 0x0c, 0x24 /* fxrstor [esp] */
  };

  hoox_x86_writer_put_bytes (cw, fxrstor, sizeof (fxrstor));
  hoox_x86_writer_put_mov_reg_reg (cw, HOOX_HX_XSP, HOOX_HX_XBX);

  hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_XSP,
      HOOX_HX_XSP, sizeof (hx_pointer)); /* discard
                                          HooxCpuContext.xip */
  hoox_x86_writer_put_popax (cw);
  hoox_x86_writer_put_popfx (cw);

  if (point_cut == HOOX_POINT_LEAVE)
  {
    hoox_x86_writer_put_ret (cw);
  }
  else
  {
    /* Emulate a ret without affecting the shadow stack. */
    hoox_x86_writer_put_lea_reg_reg_offset (cw, HOOX_HX_XSP,
        HOOX_HX_XSP, sizeof (hx_pointer));
    hoox_x86_writer_put_jmp_reg_offset_ptr (cw, HOOX_HX_XSP,
        -((hx_ssize) sizeof (hx_pointer)));
  }
}
