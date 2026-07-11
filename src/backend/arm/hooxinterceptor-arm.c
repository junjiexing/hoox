/*
 * Copyright (C) 2010-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2025 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#include "hooxinterceptor-priv.h"

#include "hooxarmreader.h"
#include "hooxarmrelocator.h"
#include "hooxarmwriter.h"
#include "hooxlibc.h"
#include "hooxmemory.h"
#include "hooxthumbreader.h"
#include "hooxthumbrelocator.h"
#include "hooxthumbwriter.h"

#include <string.h>
#include <unistd.h>

#define HOOX_INTERCEPTOR_ARM_FULL_REDIRECT_SIZE   (4 + 4)
#define HOOX_INTERCEPTOR_ARM_TINY_REDIRECT_SIZE   (4)
#define HOOX_INTERCEPTOR_THUMB_FULL_REDIRECT_SIZE (8)
#define HOOX_INTERCEPTOR_THUMB_LINK_REDIRECT_SIZE (6)
#define HOOX_INTERCEPTOR_THUMB_TINY_REDIRECT_SIZE (4)

#define FUNCTION_CONTEXT_ADDRESS_IS_THUMB(ctx) ( \
    (HX_POINTER_TO_SIZE (ctx->function_address) & 0x1) == 0x1)

#define HOOX_FRAME_OFFSET_NEXT_HOP 0
#define HOOX_FRAME_OFFSET_CPU_CONTEXT \
    (HOOX_FRAME_OFFSET_NEXT_HOP + (2 * sizeof (hx_pointer)))

#define HOOX_FCDATA(context) \
    ((HooxArmFunctionContextData *) (context)->backend_data.storage)

typedef struct _HooxArmFunctionContextData HooxArmFunctionContextData;

struct _HooxInterceptorBackend
{
  HooxCodeAllocator * allocator;

  HooxArmWriter arm_writer;
  HooxArmRelocator arm_relocator;

  HooxThumbWriter thumb_writer;
  HooxThumbRelocator thumb_relocator;

  HooxCodeSlice * arm_thunks;
  HooxCodeSlice * thumb_thunks;

  hx_pointer enter_thunk_arm;
  hx_pointer enter_thunk_thumb;
  hx_pointer leave_thunk_arm;
  hx_pointer leave_thunk_thumb;
};

struct _HooxArmFunctionContextData
{
  hx_uint full_redirect_size;
  hx_uint redirect_code_size;
  hx_uint available_space;
};

HX_STATIC_ASSERT (sizeof (HooxArmFunctionContextData)
    <= sizeof (HooxFunctionContextBackendData));

static hx_boolean hoox_interceptor_backend_emit_arm_trampolines (
    HooxInterceptorBackend * self, HooxFunctionContext * ctx,
    hx_pointer function_address);
static hx_boolean hoox_interceptor_backend_emit_thumb_trampolines (
    HooxInterceptorBackend * self, HooxFunctionContext * ctx,
    hx_pointer function_address);
static hx_boolean hoox_interceptor_backend_write_arm_custom_redirect (
    HooxInterceptorBackend * self, HooxFunctionContext * ctx, hx_pointer target);
static hx_boolean hoox_interceptor_backend_write_thumb_custom_redirect (
    HooxInterceptorBackend * self, HooxFunctionContext * ctx, hx_pointer target);

static void hoox_interceptor_backend_create_thunks (
    HooxInterceptorBackend * self);
static void hoox_interceptor_backend_destroy_thunks (
    HooxInterceptorBackend * self);

static void hoox_emit_arm_enter_thunk (HooxArmWriter * aw);
static void hoox_emit_thumb_enter_thunk (HooxThumbWriter * tw);
static void hoox_emit_arm_leave_thunk (HooxArmWriter * aw);
static void hoox_emit_thumb_leave_thunk (HooxThumbWriter * tw);

static void hoox_emit_arm_push_cpu_context_high_part (HooxArmWriter * aw);
static void hoox_emit_thumb_push_cpu_context_high_part (HooxThumbWriter * tw);
static void hoox_emit_arm_prolog (HooxArmWriter * aw);
static void hoox_emit_thumb_prolog (HooxThumbWriter * tw);
static void hoox_emit_arm_epilog (HooxArmWriter * aw);
static void hoox_emit_thumb_epilog (HooxThumbWriter * tw);

HooxInterceptorBackend *
_hoox_interceptor_backend_create (HxRecMutex * mutex,
                                 HooxCodeAllocator * allocator)
{
  HooxInterceptorBackend * backend;

  backend = hx_slice_new (HooxInterceptorBackend);
  backend->allocator = allocator;

  hoox_arm_writer_init (&backend->arm_writer, NULL);
  backend->arm_writer.cpu_features = hoox_query_cpu_features ();
  hoox_arm_relocator_init (&backend->arm_relocator, NULL, &backend->arm_writer);

  hoox_thumb_writer_init (&backend->thumb_writer, NULL);
  hoox_thumb_relocator_init (&backend->thumb_relocator, NULL,
      &backend->thumb_writer);

  hoox_interceptor_backend_create_thunks (backend);

  return backend;
}

void
_hoox_interceptor_backend_destroy (HooxInterceptorBackend * backend)
{
  hoox_interceptor_backend_destroy_thunks (backend);

  hoox_thumb_relocator_clear (&backend->thumb_relocator);
  hoox_thumb_writer_clear (&backend->thumb_writer);

  hoox_arm_relocator_clear (&backend->arm_relocator);
  hoox_arm_writer_clear (&backend->arm_writer);

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
  HooxArmFunctionContextData * data = HOOX_FCDATA (ctx);
  hx_pointer function_address;
  hx_boolean is_thumb;
  HooxRelocationScenario scenario;
  hx_uint redirect_limit;

  function_address = _hoox_interceptor_backend_get_function_address (ctx);
  is_thumb = FUNCTION_CONTEXT_ADDRESS_IS_THUMB (ctx);
  scenario = (ctx->scenario == HOOX_INTERCEPTOR_SCENARIO_OFFLINE)
      ? HOOX_SCENARIO_OFFLINE
      : HOOX_SCENARIO_ONLINE;

  if (ctx->write_redirect != NULL)
  {
    hx_uint scan_bytes;

    scan_bytes = (ctx->redirect_space_hint != 0)
        ? ctx->redirect_space_hint
        : HOOX_INTERCEPTOR_MAX_REDIRECT_SIZE;
    if (is_thumb)
    {
      data->full_redirect_size = HOOX_INTERCEPTOR_THUMB_FULL_REDIRECT_SIZE;
      hoox_thumb_relocator_can_relocate (function_address, scan_bytes, scenario,
          &data->available_space);
    }
    else
    {
      data->full_redirect_size = HOOX_INTERCEPTOR_ARM_FULL_REDIRECT_SIZE;
      hoox_arm_relocator_can_relocate (function_address, scan_bytes,
          &data->available_space);
    }
    if (ctx->redirect_space_hint != 0 &&
        data->available_space > ctx->redirect_space_hint)
      data->available_space = ctx->redirect_space_hint;
    if (data->available_space == 0)
      return FALSE;

    data->redirect_code_size = data->full_redirect_size;
    ctx->trampoline_slice = hoox_code_allocator_alloc_slice (self->allocator);
    ctx->redirect_code = hx_malloc (data->available_space);

    return TRUE;
  }

  if (is_thumb)
  {
    data->full_redirect_size = HOOX_INTERCEPTOR_THUMB_FULL_REDIRECT_SIZE;
    if ((HX_POINTER_TO_SIZE (function_address) & 3) != 0)
      data->full_redirect_size += 2;

    if (hoox_thumb_relocator_can_relocate (function_address,
          data->full_redirect_size, scenario, &redirect_limit))
    {
      data->redirect_code_size = data->full_redirect_size;
    }
    else if (force)
    {
      data->redirect_code_size = data->full_redirect_size;
    }
    else
    {
      if (redirect_limit >= HOOX_INTERCEPTOR_THUMB_LINK_REDIRECT_SIZE)
        data->redirect_code_size = HOOX_INTERCEPTOR_THUMB_LINK_REDIRECT_SIZE;
      else if (redirect_limit >= HOOX_INTERCEPTOR_THUMB_TINY_REDIRECT_SIZE)
        data->redirect_code_size = HOOX_INTERCEPTOR_THUMB_TINY_REDIRECT_SIZE;
      else
        return FALSE;
    }
  }
  else
  {
    data->full_redirect_size = HOOX_INTERCEPTOR_ARM_FULL_REDIRECT_SIZE;

    if (hoox_arm_relocator_can_relocate (function_address,
        data->full_redirect_size, &redirect_limit))
    {
      data->redirect_code_size = data->full_redirect_size;
    }
    else if (force)
    {
      data->redirect_code_size = data->full_redirect_size;
    }
    else
    {
      if (redirect_limit >= HOOX_INTERCEPTOR_ARM_TINY_REDIRECT_SIZE)
        data->redirect_code_size = HOOX_INTERCEPTOR_ARM_TINY_REDIRECT_SIZE;
      else
        return FALSE;
    }
  }

  ctx->trampoline_slice = hoox_code_allocator_alloc_slice (self->allocator);
  return TRUE;
}

hx_boolean
_hoox_interceptor_backend_create_trampoline (HooxInterceptorBackend * self,
                                            HooxFunctionContext * ctx,
                                            hx_boolean force)
{
  hx_pointer func;
  hx_boolean success;

  func = _hoox_interceptor_backend_get_function_address (ctx);

  if (!hoox_interceptor_backend_prepare_trampoline (self, ctx, force))
    return FALSE;

  if (FUNCTION_CONTEXT_ADDRESS_IS_THUMB (ctx))
    success = hoox_interceptor_backend_emit_thumb_trampolines (self, ctx, func);
  else
    success = hoox_interceptor_backend_emit_arm_trampolines (self, ctx, func);
  if (!success)
    return FALSE;

  ctx->overwritten_prologue = hx_malloc (ctx->overwritten_prologue_len);
  hoox_memcpy (ctx->overwritten_prologue, func, ctx->overwritten_prologue_len);

  return TRUE;
}

static hx_boolean
hoox_interceptor_backend_emit_arm_trampolines (HooxInterceptorBackend * self,
                                              HooxFunctionContext * ctx,
                                              hx_pointer function_address)
{
  HooxArmFunctionContextData * data = HOOX_FCDATA (ctx);
  HooxArmWriter * aw = &self->arm_writer;
  HooxArmRelocator * ar = &self->arm_relocator;
  hx_pointer deflector_target;
  hx_uint reloc_bytes;

  hoox_arm_writer_reset (aw, ctx->trampoline_slice->data);
  aw->pc = HOOX_ADDRESS (ctx->trampoline_slice->pc);

  if (ctx->type == HOOX_INTERCEPTOR_TYPE_FAST)
  {
    deflector_target = ctx->replacement_function;
  }
  else
  {
    ctx->on_enter_trampoline =
        ctx->trampoline_slice->pc + hoox_arm_writer_offset (aw);
    deflector_target = ctx->on_enter_trampoline;
  }

  if (ctx->write_redirect != NULL &&
      !hoox_interceptor_backend_write_arm_custom_redirect (self, ctx,
        deflector_target))
  {
    hoox_code_slice_unref (ctx->trampoline_slice);
    ctx->trampoline_slice = NULL;
    return FALSE;
  }

  if (ctx->write_redirect == NULL &&
      data->redirect_code_size != data->full_redirect_size)
  {
    HooxAddressSpec caller;
    hx_pointer return_address;
    hx_boolean dedicated;

    caller.near_address = function_address + data->redirect_code_size + 4;
    caller.max_distance = HOOX_ARM_B_MAX_DISTANCE;

    return_address = function_address + data->redirect_code_size;

    dedicated = TRUE;

    ctx->trampoline_deflector = hoox_code_allocator_alloc_deflector (
        self->allocator, &caller, return_address, deflector_target, dedicated);
    if (ctx->trampoline_deflector == NULL)
    {
      hoox_code_slice_unref (ctx->trampoline_slice);
      ctx->trampoline_slice = NULL;
      return FALSE;
    }
  }

  if (ctx->type != HOOX_INTERCEPTOR_TYPE_FAST)
  {
    hoox_emit_arm_push_cpu_context_high_part (aw);
    hoox_arm_writer_put_ldr_reg_address (aw, HX_ARM_REG_R6, HOOX_ADDRESS (ctx));
    hoox_arm_writer_put_ldr_reg_address (aw, HX_ARM_REG_PC,
        HOOX_ADDRESS (self->enter_thunk_arm));

    ctx->on_leave_trampoline =
        ctx->trampoline_slice->pc + hoox_arm_writer_offset (aw);

    hoox_emit_arm_push_cpu_context_high_part (aw);
    hoox_arm_writer_put_ldr_reg_address (aw, HX_ARM_REG_R6, HOOX_ADDRESS (ctx));
    hoox_arm_writer_put_ldr_reg_address (aw, HX_ARM_REG_PC,
        HOOX_ADDRESS (self->leave_thunk_arm));

    hoox_arm_writer_flush (aw);
    hx_assert (hoox_arm_writer_offset (aw) <= ctx->trampoline_slice->size);
  }

  ctx->on_invoke_trampoline =
      ctx->trampoline_slice->pc + hoox_arm_writer_offset (aw);

  hoox_arm_writer_reset (aw, ctx->on_invoke_trampoline);
  hoox_arm_relocator_reset (ar, function_address, aw);

  do
  {
    reloc_bytes = hoox_arm_relocator_read_one (ar, NULL);
    if (reloc_bytes == 0)
      reloc_bytes = data->redirect_code_size;
  }
  while (reloc_bytes < data->redirect_code_size);

  hoox_arm_relocator_write_all (ar);

  if (!hoox_arm_relocator_eoi (ar))
  {
    hoox_arm_writer_put_ldr_reg_address (aw, HX_ARM_REG_PC,
        HOOX_ADDRESS (function_address + reloc_bytes));
  }

  hoox_arm_writer_flush (aw);
  hx_assert (hoox_arm_writer_offset (aw) <= ctx->trampoline_slice->size);

  ctx->overwritten_prologue_len = reloc_bytes;

  return TRUE;
}

static hx_boolean
hoox_interceptor_backend_emit_thumb_trampolines (HooxInterceptorBackend * self,
                                                HooxFunctionContext * ctx,
                                                hx_pointer function_address)
{
  HooxArmFunctionContextData * data = HOOX_FCDATA (ctx);
  HooxThumbWriter * tw = &self->thumb_writer;
  HooxThumbRelocator * tr = &self->thumb_relocator;
  hx_pointer deflector_target;
  HxString * signature;
  const hx_insn * insn, * trailing_bl;
  hx_uint reloc_bytes;
  hx_boolean is_branch_back_needed;
  hx_boolean is_eligible_for_lr_rewriting;

  hoox_thumb_writer_reset (tw, ctx->trampoline_slice->data);
  tw->pc = HOOX_ADDRESS (ctx->trampoline_slice->pc);

  if (ctx->type == HOOX_INTERCEPTOR_TYPE_FAST)
  {
    deflector_target = ctx->replacement_function;
  }
  else
  {
    ctx->on_enter_trampoline = hoox_thumb_writer_cur (tw) + 1;
    deflector_target = ctx->on_enter_trampoline;
  }

  if (ctx->write_redirect != NULL &&
      !hoox_interceptor_backend_write_thumb_custom_redirect (self, ctx,
        deflector_target))
  {
    hoox_code_slice_unref (ctx->trampoline_slice);
    ctx->trampoline_slice = NULL;
    return FALSE;
  }

  if (ctx->write_redirect == NULL &&
      data->redirect_code_size != data->full_redirect_size)
  {
    HooxAddressSpec caller;
    hx_pointer return_address;
    hx_boolean dedicated;

    caller.near_address = function_address + data->redirect_code_size;
    caller.max_distance = HOOX_THUMB_B_MAX_DISTANCE;

    return_address = function_address + data->redirect_code_size + 1;

    dedicated =
        data->redirect_code_size == HOOX_INTERCEPTOR_THUMB_TINY_REDIRECT_SIZE;

    ctx->trampoline_deflector = hoox_code_allocator_alloc_deflector (
        self->allocator, &caller, return_address, deflector_target, dedicated);
    if (ctx->trampoline_deflector == NULL)
    {
      hoox_code_slice_unref (ctx->trampoline_slice);
      ctx->trampoline_slice = NULL;
      return FALSE;
    }
  }

  if (ctx->type != HOOX_INTERCEPTOR_TYPE_FAST)
  {
    if (data->redirect_code_size != HOOX_INTERCEPTOR_THUMB_LINK_REDIRECT_SIZE)
    {
      hoox_emit_thumb_push_cpu_context_high_part (tw);
    }

    hoox_thumb_writer_put_ldr_reg_address (tw, HX_ARM_REG_R6, HOOX_ADDRESS (ctx));
    hoox_thumb_writer_put_ldr_reg_address (tw, HX_ARM_REG_PC,
        HOOX_ADDRESS (self->enter_thunk_thumb));

    ctx->on_leave_trampoline = hoox_thumb_writer_cur (tw) + 1;

    hoox_emit_thumb_push_cpu_context_high_part (tw);
    hoox_thumb_writer_put_ldr_reg_address (tw, HX_ARM_REG_R6, HOOX_ADDRESS (ctx));
    hoox_thumb_writer_put_ldr_reg_address (tw, HX_ARM_REG_PC,
        HOOX_ADDRESS (self->leave_thunk_thumb));

    hoox_thumb_writer_flush (tw);
    hx_assert (hoox_thumb_writer_offset (tw) <= ctx->trampoline_slice->size);
  }

  ctx->on_invoke_trampoline = hoox_thumb_writer_cur (tw) + 1;

  hoox_thumb_relocator_reset (tr, function_address, tw);

  signature = hx_string_sized_new (16);

  insn = NULL;
  do
  {
    reloc_bytes = hoox_thumb_relocator_read_one (tr, &insn);

    if (reloc_bytes != 0)
    {
      if (signature->len != 0)
        hx_string_append_c (signature, ';');
      hx_string_append (signature, insn->mnemonic);
    }
    else
    {
      reloc_bytes = data->redirect_code_size;
    }
  }
  while (reloc_bytes < data->redirect_code_size);

  /*
   * When we are hooking a function already hooked by another copy of
   * Hoox, we need to be very careful when relocating BL instructions.
   * This is because the deflector trampoline looks at LR to determine
   * which hook is invoking it. So when the last of the overwritten
   * instructions is a BL, we might as well just transform it so it
   * looks just as if it had executed at its original memory location.
   */
  trailing_bl = (insn != NULL && insn->id == HX_ARM_INS_BL &&
      insn->detail->arm.operands[0].type == HX_ARM_OP_IMM) ? insn : NULL;

  is_branch_back_needed = !hoox_thumb_relocator_eoi (tr);

  /*
   * Try to deal with minimal thunks that determine their caller and pass
   * it along to some inner function. This is important to support hooking
   * dlopen() on Android, where the dynamic linker uses the caller address
   * to decide on namespace and whether to allow the particular library to
   * be used by a particular caller.
   *
   * Because we potentially replace LR in order to trap the return, we end
   * up breaking dlopen() in such cases. We work around this by detecting
   * LR being read, and replace that instruction with a load of the actual
   * caller.
   *
   * This is however a bit risky done blindly, so we try to limit the
   * scope to the bare minimum. A potentially better longer term solution
   * is to analyze the function and patch each point of return, so we don't
   * have to replace LR on entry. That is however a bit complex, so we
   * opt for this simpler solution for now.
   */
  is_eligible_for_lr_rewriting = strcmp (signature->str, "mov;b") == 0 ||
      strcmp (signature->str, "mov;bx") == 0 ||
      hx_str_has_prefix (signature->str, "push;mov;bl");

  hx_string_free (signature, TRUE);

  if (is_eligible_for_lr_rewriting)
  {
    const hx_insn * insn;

    while ((insn = hoox_thumb_relocator_peek_next_write_insn (tr)) != NULL)
    {
      if (insn->id == HX_ARM_INS_MOV &&
          insn->detail->arm.operands[1].reg == HX_ARM_REG_LR)
      {
        hx_arm_reg dst_reg = insn->detail->arm.operands[0].reg;
        const hx_arm_reg clobbered_regs[] = {
          HX_ARM_REG_R0, HX_ARM_REG_R1, HX_ARM_REG_R2, HX_ARM_REG_R3,
          HX_ARM_REG_R4, HX_ARM_REG_R5, HX_ARM_REG_R6, HX_ARM_REG_R7,
          HX_ARM_REG_R9, HX_ARM_REG_R12, HX_ARM_REG_LR,
        };
        HxArray * saved_regs;
        hx_uint i;
        hx_arm_reg nzcvq_reg;

        saved_regs = hx_array_sized_new (FALSE, FALSE, sizeof (hx_arm_reg),
            HX_N_ELEMENTS (clobbered_regs));
        for (i = 0; i != HX_N_ELEMENTS (clobbered_regs); i++)
        {
          hx_arm_reg reg = clobbered_regs[i];
          if (reg != dst_reg)
            hx_array_append_val (saved_regs, reg);
        }

        nzcvq_reg = HX_ARM_REG_R4;
        if (nzcvq_reg == dst_reg)
          nzcvq_reg = HX_ARM_REG_R5;

        hoox_thumb_writer_put_push_regs_array (tw, saved_regs->len,
            (const hx_arm_reg *) saved_regs->data);
        hoox_thumb_writer_put_mrs_reg_reg (tw, nzcvq_reg,
            HX_ARM_SYSREG_APSR_NZCVQ);

        hoox_thumb_writer_put_call_address_with_arguments (tw,
            HOOX_ADDRESS (_hoox_interceptor_translate_top_return_address), 1,
            HOOX_ARG_REGISTER, HX_ARM_REG_LR);
        hoox_thumb_writer_put_mov_reg_reg (tw, dst_reg, HX_ARM_REG_R0);

        hoox_thumb_writer_put_msr_reg_reg (tw, HX_ARM_SYSREG_APSR_NZCVQ,
            nzcvq_reg);
        hoox_thumb_writer_put_pop_regs_array (tw, saved_regs->len,
            (const hx_arm_reg *) saved_regs->data);

        hx_array_free (saved_regs, TRUE);

        hoox_thumb_relocator_skip_one (tr);
      }
      else
      {
        hoox_thumb_relocator_write_one (tr);
      }
    }
  }
  else if (trailing_bl != NULL)
  {
    const hx_arm_op * target = &trailing_bl->detail->arm.operands[0];

    while (hoox_thumb_relocator_peek_next_write_insn (tr) != trailing_bl)
      hoox_thumb_relocator_write_one (tr);
    hoox_thumb_relocator_skip_one (tr);

    hoox_thumb_writer_put_ldr_reg_address (tw, HX_ARM_REG_LR,
        trailing_bl->address + trailing_bl->size + 1);
    hoox_thumb_writer_put_ldr_reg_address (tw, HX_ARM_REG_PC,
        target->imm | 1);

    is_branch_back_needed = FALSE;
  }
  else
  {
    hoox_thumb_relocator_write_all (tr);
  }

  if (is_branch_back_needed)
  {
    hoox_thumb_writer_put_push_regs (tw, 2, HX_ARM_REG_R0, HX_ARM_REG_R1);
    hoox_thumb_writer_put_ldr_reg_address (tw, HX_ARM_REG_R0,
        HOOX_ADDRESS (function_address + reloc_bytes + 1));
    hoox_thumb_writer_put_str_reg_reg_offset (tw, HX_ARM_REG_R0,
        HX_ARM_REG_SP, 4);
    hoox_thumb_writer_put_pop_regs (tw, 2, HX_ARM_REG_R0, HX_ARM_REG_PC);
  }

  hoox_thumb_writer_flush (tw);
  hx_assert (hoox_thumb_writer_offset (tw) <= ctx->trampoline_slice->size);

  ctx->overwritten_prologue_len = reloc_bytes;

  return TRUE;
}

static hx_boolean
hoox_interceptor_backend_write_arm_custom_redirect (
    HooxInterceptorBackend * self,
    HooxFunctionContext * ctx,
    hx_pointer target)
{
  HooxArmFunctionContextData * data = HOOX_FCDATA (ctx);
  HooxRedirectWriteResult result;
  HooxArmWriter rw;
  HooxRedirectWriteDetails details;

  hoox_arm_writer_init (&rw, ctx->redirect_code);
  rw.pc = HOOX_ADDRESS (_hoox_interceptor_backend_get_function_address (ctx));

  details.writer = &rw;
  details.target = target;
  details.scratch_register = ctx->scratch_register;
  details.capacity = data->available_space;

  result = ctx->write_redirect (&details, ctx->write_redirect_data);

  hoox_arm_writer_flush (&rw);
  data->redirect_code_size = hoox_arm_writer_offset (&rw);
  hoox_arm_writer_clear (&rw);

  hx_assert (data->redirect_code_size <= data->available_space);

  return result == HOOX_REDIRECT_WRITTEN;
}

static hx_boolean
hoox_interceptor_backend_write_thumb_custom_redirect (
    HooxInterceptorBackend * self,
    HooxFunctionContext * ctx,
    hx_pointer target)
{
  HooxArmFunctionContextData * data = HOOX_FCDATA (ctx);
  HooxRedirectWriteResult result;
  HooxThumbWriter rw;
  HooxRedirectWriteDetails details;

  hoox_thumb_writer_init (&rw, ctx->redirect_code);
  rw.pc = HOOX_ADDRESS (_hoox_interceptor_backend_get_function_address (ctx));

  details.writer = &rw;
  details.target = target;
  details.scratch_register = ctx->scratch_register;
  details.capacity = data->available_space;

  result = ctx->write_redirect (&details, ctx->write_redirect_data);

  hoox_thumb_writer_flush (&rw);
  data->redirect_code_size = hoox_thumb_writer_offset (&rw);
  hoox_thumb_writer_clear (&rw);

  hx_assert (data->redirect_code_size <= data->available_space);

  return result == HOOX_REDIRECT_WRITTEN;
}

void
_hoox_interceptor_backend_destroy_trampoline (HooxInterceptorBackend * self,
                                             HooxFunctionContext * ctx)
{
  hoox_code_slice_unref (ctx->trampoline_slice);
  hoox_code_deflector_unref (ctx->trampoline_deflector);
  ctx->trampoline_slice = NULL;
  ctx->trampoline_deflector = NULL;
}

void
_hoox_interceptor_backend_activate_trampoline (HooxInterceptorBackend * self,
                                              HooxFunctionContext * ctx,
                                              hx_pointer prologue)
{
  HooxAddress function_address;
  HooxArmFunctionContextData * data = HOOX_FCDATA (ctx);

  function_address = HOOX_ADDRESS (
      _hoox_interceptor_backend_get_function_address (ctx));

  if (FUNCTION_CONTEXT_ADDRESS_IS_THUMB (ctx))
  {
    HooxThumbWriter * tw = &self->thumb_writer;

    hoox_thumb_writer_reset (tw, prologue);
    tw->pc = function_address;

    if (ctx->write_redirect != NULL)
    {
      hoox_thumb_writer_put_bytes (tw, ctx->redirect_code,
          data->redirect_code_size);
    }
    else if (ctx->trampoline_deflector != NULL)
    {
      if (data->redirect_code_size == HOOX_INTERCEPTOR_THUMB_LINK_REDIRECT_SIZE)
      {
        hoox_emit_thumb_push_cpu_context_high_part (tw);
        hoox_thumb_writer_put_bl_imm (tw,
            HOOX_ADDRESS (ctx->trampoline_deflector->trampoline));
      }
      else
      {
        hx_assert (data->redirect_code_size ==
            HOOX_INTERCEPTOR_THUMB_TINY_REDIRECT_SIZE);
        hoox_thumb_writer_put_b_imm (tw,
            HOOX_ADDRESS (ctx->trampoline_deflector->trampoline));
      }
    }
    else if (ctx->type == HOOX_INTERCEPTOR_TYPE_FAST)
    {
      hoox_thumb_writer_put_ldr_reg_address (tw, HX_ARM_REG_PC,
          HOOX_ADDRESS (ctx->replacement_function));
    }
    else
    {
      hoox_thumb_writer_put_ldr_reg_address (tw, HX_ARM_REG_PC,
          HOOX_ADDRESS (ctx->on_enter_trampoline));
    }

    hoox_thumb_writer_flush (tw);
    hx_assert (hoox_thumb_writer_offset (tw) <= data->redirect_code_size);
  }
  else
  {
    HooxArmWriter * aw = &self->arm_writer;

    hoox_arm_writer_reset (aw, prologue);
    aw->pc = function_address;

    if (ctx->write_redirect != NULL)
    {
      hoox_arm_writer_put_bytes (aw, ctx->redirect_code,
          data->redirect_code_size);
    }
    else if (ctx->trampoline_deflector != NULL)
    {
      hx_assert (data->redirect_code_size ==
          HOOX_INTERCEPTOR_ARM_TINY_REDIRECT_SIZE);
      hoox_arm_writer_put_b_imm (aw,
          HOOX_ADDRESS (ctx->trampoline_deflector->trampoline));
    }
    else if (ctx->type == HOOX_INTERCEPTOR_TYPE_FAST)
    {
      hoox_arm_writer_put_ldr_reg_address (aw, HX_ARM_REG_PC,
          HOOX_ADDRESS (ctx->replacement_function));
    }
    else
    {
      hoox_arm_writer_put_ldr_reg_address (aw, HX_ARM_REG_PC,
          HOOX_ADDRESS (ctx->on_enter_trampoline));
    }

    hoox_arm_writer_flush (aw);
    hx_assert (hoox_arm_writer_offset (aw) == data->redirect_code_size);
  }
}

void
_hoox_interceptor_backend_deactivate_trampoline (HooxInterceptorBackend * self,
                                                HooxFunctionContext * ctx,
                                                hx_pointer prologue)
{
  hoox_memcpy (prologue, ctx->overwritten_prologue,
      ctx->overwritten_prologue_len);
}

hx_pointer
_hoox_interceptor_backend_get_function_address (HooxFunctionContext * ctx)
{
  return HX_SIZE_TO_POINTER (
      HX_POINTER_TO_SIZE (ctx->function_address) & ~((hx_size) 1));
}

hx_pointer
_hoox_interceptor_backend_resolve_redirect (HooxInterceptorBackend * self,
                                           hx_pointer address)
{
  hx_pointer target;

  if ((HX_POINTER_TO_SIZE (address) & 1) == 1)
  {
    target = hoox_thumb_reader_try_get_relative_jump_target (address);
  }
  else
  {
    target = hoox_arm_reader_try_get_relative_jump_target (address);
    if (target == NULL)
      target = hoox_arm_reader_try_get_indirect_jump_target (address);
  }

  return target;
}

hx_size
_hoox_interceptor_backend_detect_hook_size (hx_constpointer code,
                                           hx_csh capstone,
                                           hx_insn * insn)
{
  /* TODO: implement hook size detection */
  return 0;
}

static void
hoox_interceptor_backend_create_thunks (HooxInterceptorBackend * self)
{
  HooxArmWriter * aw = &self->arm_writer;
  HooxThumbWriter * tw = &self->thumb_writer;

  self->arm_thunks = hoox_code_allocator_alloc_slice (self->allocator);
  hoox_arm_writer_reset (aw, self->arm_thunks->data);
  aw->pc = HOOX_ADDRESS (self->arm_thunks->pc);

  self->enter_thunk_arm = self->arm_thunks->pc + hoox_arm_writer_offset (aw);
  hoox_emit_arm_enter_thunk (aw);

  self->leave_thunk_arm = self->arm_thunks->pc + hoox_arm_writer_offset (aw);
  hoox_emit_arm_leave_thunk (aw);

  hoox_arm_writer_flush (aw);
  hx_assert (hoox_arm_writer_offset (aw) <= self->arm_thunks->size);

  self->thumb_thunks = hoox_code_allocator_alloc_slice (self->allocator);
  hoox_thumb_writer_reset (tw, self->thumb_thunks->data);
  tw->pc = HOOX_ADDRESS (self->thumb_thunks->pc);

  self->enter_thunk_thumb =
      self->thumb_thunks->pc + hoox_thumb_writer_offset (tw) + 1;
  hoox_emit_thumb_enter_thunk (tw);

  self->leave_thunk_thumb =
      self->thumb_thunks->pc + hoox_thumb_writer_offset (tw) + 1;
  hoox_emit_thumb_leave_thunk (tw);

  hoox_thumb_writer_flush (tw);
  hx_assert (hoox_thumb_writer_offset (tw) <= self->thumb_thunks->size);
}

static void
hoox_interceptor_backend_destroy_thunks (HooxInterceptorBackend * self)
{
  hoox_code_slice_unref (self->thumb_thunks);
  hoox_code_slice_unref (self->arm_thunks);
}

static void
hoox_emit_arm_enter_thunk (HooxArmWriter * aw)
{
  hoox_emit_arm_prolog (aw);

  hoox_arm_writer_put_add_reg_reg_imm (aw, HX_ARM_REG_R1, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_CPU_CONTEXT);
  hoox_arm_writer_put_sub_reg_reg_imm (aw, HX_ARM_REG_R2, HX_ARM_REG_R4, 4);
  hoox_arm_writer_put_add_reg_reg_imm (aw, HX_ARM_REG_R3, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_NEXT_HOP);

  hoox_arm_writer_put_call_address_with_arguments (aw,
      HOOX_ADDRESS (_hoox_function_context_begin_invocation), 4,
      HOOX_ARG_REGISTER, HX_ARM_REG_R6,
      HOOX_ARG_REGISTER, HX_ARM_REG_R1,
      HOOX_ARG_REGISTER, HX_ARM_REG_R2,
      HOOX_ARG_REGISTER, HX_ARM_REG_R3);

  hoox_emit_arm_epilog (aw);
}

static void
hoox_emit_thumb_enter_thunk (HooxThumbWriter * tw)
{
  hoox_emit_thumb_prolog (tw);

  hoox_thumb_writer_put_add_reg_reg_imm (tw, HX_ARM_REG_R1, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_CPU_CONTEXT);
  hoox_thumb_writer_put_sub_reg_reg_imm (tw, HX_ARM_REG_R2, HX_ARM_REG_R4, 4);
  hoox_thumb_writer_put_add_reg_reg_imm (tw, HX_ARM_REG_R3, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_NEXT_HOP);

  hoox_thumb_writer_put_call_address_with_arguments (tw,
      HOOX_ADDRESS (_hoox_function_context_begin_invocation), 4,
      HOOX_ARG_REGISTER, HX_ARM_REG_R6,
      HOOX_ARG_REGISTER, HX_ARM_REG_R1,
      HOOX_ARG_REGISTER, HX_ARM_REG_R2,
      HOOX_ARG_REGISTER, HX_ARM_REG_R3);

  hoox_emit_thumb_epilog (tw);
}

static void
hoox_emit_arm_leave_thunk (HooxArmWriter * aw)
{
  hoox_emit_arm_prolog (aw);

  hoox_arm_writer_put_add_reg_reg_imm (aw, HX_ARM_REG_R1, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_CPU_CONTEXT);
  hoox_arm_writer_put_add_reg_reg_imm (aw, HX_ARM_REG_R2, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_NEXT_HOP);

  hoox_arm_writer_put_call_address_with_arguments (aw,
      HOOX_ADDRESS (_hoox_function_context_end_invocation), 3,
      HOOX_ARG_REGISTER, HX_ARM_REG_R6,
      HOOX_ARG_REGISTER, HX_ARM_REG_R1,
      HOOX_ARG_REGISTER, HX_ARM_REG_R2);

  hoox_emit_arm_epilog (aw);
}

static void
hoox_emit_thumb_leave_thunk (HooxThumbWriter * tw)
{
  hoox_emit_thumb_prolog (tw);

  hoox_thumb_writer_put_add_reg_reg_imm (tw, HX_ARM_REG_R1, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_CPU_CONTEXT);
  hoox_thumb_writer_put_add_reg_reg_imm (tw, HX_ARM_REG_R2, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_NEXT_HOP);

  hoox_thumb_writer_put_call_address_with_arguments (tw,
      HOOX_ADDRESS (_hoox_function_context_end_invocation), 3,
      HOOX_ARG_REGISTER, HX_ARM_REG_R6,
      HOOX_ARG_REGISTER, HX_ARM_REG_R1,
      HOOX_ARG_REGISTER, HX_ARM_REG_R2);

  hoox_emit_thumb_epilog (tw);
}

static void
hoox_emit_arm_push_cpu_context_high_part (HooxArmWriter * aw)
{
  hoox_arm_writer_put_push_regs (aw, 9,
      HX_ARM_REG_R0, HX_ARM_REG_R1, HX_ARM_REG_R2,
      HX_ARM_REG_R3, HX_ARM_REG_R4, HX_ARM_REG_R5,
      HX_ARM_REG_R6, HX_ARM_REG_R7, HX_ARM_REG_LR);
}

static void
hoox_emit_thumb_push_cpu_context_high_part (HooxThumbWriter * tw)
{
  hoox_thumb_writer_put_push_regs (tw, 9,
      HX_ARM_REG_R0, HX_ARM_REG_R1, HX_ARM_REG_R2,
      HX_ARM_REG_R3, HX_ARM_REG_R4, HX_ARM_REG_R5,
      HX_ARM_REG_R6, HX_ARM_REG_R7, HX_ARM_REG_LR);
}

static void
hoox_emit_arm_prolog (HooxArmWriter * aw)
{
  HooxCpuFeatures cpu_features;

  /*
   * Set up our stack frame:
   *
   * [cpu_context] <-- high part already pushed
   * [padding]
   * [next_hop]
   */

  hoox_arm_writer_put_mov_reg_cpsr (aw, HX_ARM_REG_R5);
  hoox_arm_writer_put_add_reg_reg_imm (aw, HX_ARM_REG_R4, HX_ARM_REG_SP, 9 * 4);

  /* Store vector registers + padding */
  cpu_features = hoox_query_cpu_features ();

  if ((cpu_features & HOOX_CPU_VFP2) != 0)
  {
    if ((cpu_features & HOOX_CPU_VFPD32) != 0)
    {
      hoox_arm_writer_put_sub_reg_u16 (aw, HX_ARM_REG_SP, 4);
      hoox_arm_writer_put_vpush_range (aw, HX_ARM_REG_Q8, HX_ARM_REG_Q15);
    }
    else
    {
      hoox_arm_writer_put_sub_reg_u16 (aw, HX_ARM_REG_SP,
          (8 * sizeof (HooxArmVectorReg)) + 4);
    }

    hoox_arm_writer_put_vpush_range (aw, HX_ARM_REG_Q0, HX_ARM_REG_Q7);
  }
  else
  {
    hoox_arm_writer_put_sub_reg_u16 (aw, HX_ARM_REG_SP,
        (16 * sizeof (HooxArmVectorReg)) + 4);
  }

  /* Store SP, CPSR, followed by R8-R12 */
  hoox_arm_writer_put_push_regs (aw, 7,
      HX_ARM_REG_R4, HX_ARM_REG_R5,
      HX_ARM_REG_R8, HX_ARM_REG_R9, HX_ARM_REG_R10, HX_ARM_REG_R11, HX_ARM_REG_R12);

  /* Reserve space for next_hop, padding, and the PC placeholder */
  hoox_arm_writer_put_sub_reg_u16 (aw, HX_ARM_REG_SP, 3 * 4);
}

static void
hoox_emit_thumb_prolog (HooxThumbWriter * tw)
{
  HooxCpuFeatures cpu_features;

  hoox_thumb_writer_put_mov_reg_cpsr (tw, HX_ARM_REG_R5);
  hoox_thumb_writer_put_add_reg_reg_imm (tw, HX_ARM_REG_R4, HX_ARM_REG_SP, 9 * 4);

  cpu_features = hoox_query_cpu_features ();

  if ((cpu_features & HOOX_CPU_VFP2) != 0)
  {
    if ((cpu_features & HOOX_CPU_VFPD32) != 0)
    {
      hoox_thumb_writer_put_sub_reg_imm (tw, HX_ARM_REG_SP, 4);
      hoox_thumb_writer_put_vpush_range (tw, HX_ARM_REG_Q8, HX_ARM_REG_Q15);
    }
    else
    {
      hoox_thumb_writer_put_sub_reg_imm (tw, HX_ARM_REG_SP,
          (8 * sizeof (HooxArmVectorReg)) + 4);
    }

    hoox_thumb_writer_put_vpush_range (tw, HX_ARM_REG_Q0, HX_ARM_REG_Q7);
  }
  else
  {
    hoox_thumb_writer_put_sub_reg_imm (tw, HX_ARM_REG_SP,
        (16 * sizeof (HooxArmVectorReg)) + 4);
  }

  hoox_thumb_writer_put_push_regs (tw, 7,
      HX_ARM_REG_R4, HX_ARM_REG_R5,
      HX_ARM_REG_R8, HX_ARM_REG_R9, HX_ARM_REG_R10, HX_ARM_REG_R11, HX_ARM_REG_R12);

  hoox_thumb_writer_put_sub_reg_imm (tw, HX_ARM_REG_SP, 3 * 4);
}

static void
hoox_emit_arm_epilog (HooxArmWriter * aw)
{
  HooxCpuFeatures cpu_features;

  /* Restore LR */
  hoox_arm_writer_put_sub_reg_reg_imm (aw, HX_ARM_REG_R0, HX_ARM_REG_R4, 4);
  hoox_arm_writer_put_ldr_reg_reg (aw, HX_ARM_REG_LR, HX_ARM_REG_R0);

  /* Replace LR with next_hop so we can pop it straight into PC */
  hoox_arm_writer_put_ldr_reg_reg_offset (aw, HX_ARM_REG_R1, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_NEXT_HOP);
  hoox_arm_writer_put_str_reg_reg (aw, HX_ARM_REG_R1, HX_ARM_REG_R0);

  hoox_arm_writer_put_ldr_reg_reg_offset (aw, HX_ARM_REG_R5, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_CPU_CONTEXT + HX_STRUCT_OFFSET (HooxCpuContext, cpsr));

  /* Skip [next_hop, padding] and [PC, SP, and CPSR] */
  hoox_arm_writer_put_add_reg_u16 (aw, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_CPU_CONTEXT + (3 * 4));

  hoox_arm_writer_put_pop_regs (aw, 5,
      HX_ARM_REG_R8, HX_ARM_REG_R9, HX_ARM_REG_R10, HX_ARM_REG_R11, HX_ARM_REG_R12);

  cpu_features = hoox_query_cpu_features ();

  if ((cpu_features & HOOX_CPU_VFP2) != 0)
  {
    hoox_arm_writer_put_vpop_range (aw, HX_ARM_REG_Q0, HX_ARM_REG_Q7);

    if ((cpu_features & HOOX_CPU_VFPD32) != 0)
    {
      hoox_arm_writer_put_vpop_range (aw, HX_ARM_REG_Q8, HX_ARM_REG_Q15);
      hoox_arm_writer_put_add_reg_u16 (aw, HX_ARM_REG_SP, 4);
    }
    else
    {
      hoox_arm_writer_put_add_reg_u16 (aw, HX_ARM_REG_SP,
          (8 * sizeof (HooxArmVectorReg)) + 4);
    }
  }
  else
  {
    hoox_arm_writer_put_add_reg_u16 (aw, HX_ARM_REG_SP,
        (16 * sizeof (HooxArmVectorReg)) + 4);
  }

  hoox_arm_writer_put_mov_cpsr_reg (aw, HX_ARM_REG_R5);

  hoox_arm_writer_put_pop_regs (aw, 9,
      HX_ARM_REG_R0, HX_ARM_REG_R1, HX_ARM_REG_R2,
      HX_ARM_REG_R3, HX_ARM_REG_R4, HX_ARM_REG_R5,
      HX_ARM_REG_R6, HX_ARM_REG_R7, HX_ARM_REG_PC);
}

static void
hoox_emit_thumb_epilog (HooxThumbWriter * tw)
{
  HooxCpuFeatures cpu_features;

  hoox_thumb_writer_put_sub_reg_reg_imm (tw, HX_ARM_REG_R0, HX_ARM_REG_R4, 4);
  hoox_thumb_writer_put_ldr_reg_reg (tw, HX_ARM_REG_R1, HX_ARM_REG_R0);
  hoox_thumb_writer_put_mov_reg_reg (tw, HX_ARM_REG_LR, HX_ARM_REG_R1);

  hoox_thumb_writer_put_ldr_reg_reg_offset (tw, HX_ARM_REG_R1, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_NEXT_HOP);
  hoox_thumb_writer_put_str_reg_reg (tw, HX_ARM_REG_R1, HX_ARM_REG_R0);

  hoox_thumb_writer_put_ldr_reg_reg_offset (tw, HX_ARM_REG_R5, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_CPU_CONTEXT + HX_STRUCT_OFFSET (HooxCpuContext, cpsr));

  hoox_thumb_writer_put_add_reg_imm (tw, HX_ARM_REG_SP,
      HOOX_FRAME_OFFSET_CPU_CONTEXT + (3 * 4));

  hoox_thumb_writer_put_pop_regs (tw, 5,
      HX_ARM_REG_R8, HX_ARM_REG_R9, HX_ARM_REG_R10, HX_ARM_REG_R11, HX_ARM_REG_R12);

  cpu_features = hoox_query_cpu_features ();

  if ((cpu_features & HOOX_CPU_VFP2) != 0)
  {
    hoox_thumb_writer_put_vpop_range (tw, HX_ARM_REG_Q0, HX_ARM_REG_Q7);

    if ((cpu_features & HOOX_CPU_VFPD32) != 0)
    {
      hoox_thumb_writer_put_vpop_range (tw, HX_ARM_REG_Q8, HX_ARM_REG_Q15);
      hoox_thumb_writer_put_add_reg_imm (tw, HX_ARM_REG_SP, 4);
    }
    else
    {
      hoox_thumb_writer_put_add_reg_imm (tw, HX_ARM_REG_SP,
          (8 * sizeof (HooxArmVectorReg)) + 4);
    }
  }
  else
  {
    hoox_thumb_writer_put_add_reg_imm (tw, HX_ARM_REG_SP,
        (16 * sizeof (HooxArmVectorReg)) + 4);
  }

  hoox_thumb_writer_put_mov_cpsr_reg (tw, HX_ARM_REG_R5);

  hoox_thumb_writer_put_pop_regs (tw, 9,
      HX_ARM_REG_R0, HX_ARM_REG_R1, HX_ARM_REG_R2,
      HX_ARM_REG_R3, HX_ARM_REG_R4, HX_ARM_REG_R5,
      HX_ARM_REG_R6, HX_ARM_REG_R7, HX_ARM_REG_PC);
}
