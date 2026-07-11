/*
 * Copyright (C) 2008-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2008 Christian Berentsen <jc.berentsen@gmail.com>
 * Copyright (C) 2024 Yannis Juglaret <yjuglaret@mozilla.com>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_INTERCEPTOR_PRIV_H__
#define __HOOX_INTERCEPTOR_PRIV_H__

#include "hooxinterceptor.h"

#include "hooxcodeallocator.h"
#include "hooxspinlock.h"
#include "hooxtls.h"

typedef struct _HooxInterceptorBackend HooxInterceptorBackend;
typedef hx_uint8 HooxInterceptorType;
typedef struct _HooxFunctionContext HooxFunctionContext;
typedef union _HooxFunctionContextBackendData HooxFunctionContextBackendData;

enum _HooxInterceptorType
{
  HOOX_INTERCEPTOR_TYPE_DEFAULT = 0,
  HOOX_INTERCEPTOR_TYPE_FAST    = 1
};

union _HooxFunctionContextBackendData
{
  hx_char storage[3 * HX_SIZEOF_VOID_P];
  hx_pointer p[3];
};

#define HOOX_INTERCEPTOR_MAX_REDIRECT_SIZE 128

struct _HooxFunctionContext
{
  hx_pointer function_address;

  hx_pointer grafted_hook;
  hx_pointer import_target;

  HooxInterceptorType type;
  hx_uint8 destroyed;
  hx_uint8 activated;
  hx_uint8 has_on_leave_listener;
  hx_uint8 has_unignorable_listener;

  HooxCodeSlice * trampoline_slice;
  HooxCodeDeflector * trampoline_deflector;
  volatile hx_int trampoline_usage_counter;

  hx_pointer on_enter_trampoline;
  hx_uint8 * overwritten_prologue;
  hx_uint overwritten_prologue_len;
  hx_uint8 * redirect_code;

  hx_pointer on_invoke_trampoline;

  hx_pointer on_leave_trampoline;

  volatile HxPtrArray * listener_entries;

  hx_pointer replacement_function;
  hx_pointer replacement_data;

  hx_int scratch_register;
  HooxInterceptorScenario scenario;
  HooxRelocationPolicy relocation_policy;
  HooxWriteRedirectFunc write_redirect;
  hx_pointer write_redirect_data;
  hx_uint redirect_space_hint;

  HooxFunctionContextBackendData backend_data;

  HooxInterceptor * interceptor;
};

HX_GNUC_INTERNAL void _hoox_interceptor_init (void);
HX_GNUC_INTERNAL void _hoox_interceptor_deinit (void);

HX_GNUC_INTERNAL hx_boolean _hoox_function_context_begin_invocation (
    HooxFunctionContext * function_ctx, HooxCpuContext * cpu_context,
    hx_pointer * caller_ret_addr, hx_pointer * next_hop);
HX_GNUC_INTERNAL void _hoox_function_context_end_invocation (
    HooxFunctionContext * function_ctx, HooxCpuContext * cpu_context,
    hx_pointer * next_hop);

HX_GNUC_INTERNAL HooxInterceptorBackend * _hoox_interceptor_backend_create (
    HxRecMutex * mutex, HooxCodeAllocator * allocator);
HX_GNUC_INTERNAL void _hoox_interceptor_backend_destroy (
    HooxInterceptorBackend * backend);
HX_GNUC_INTERNAL hx_boolean _hoox_interceptor_backend_claim_grafted_trampoline (
    HooxInterceptorBackend * self, HooxFunctionContext * ctx);
HX_GNUC_INTERNAL hx_boolean _hoox_interceptor_backend_create_trampoline (
    HooxInterceptorBackend * self, HooxFunctionContext * ctx, hx_boolean overwrite);
HX_GNUC_INTERNAL void _hoox_interceptor_backend_destroy_trampoline (
    HooxInterceptorBackend * self, HooxFunctionContext * ctx);
HX_GNUC_INTERNAL void _hoox_interceptor_backend_activate_trampoline (
    HooxInterceptorBackend * self, HooxFunctionContext * ctx, hx_pointer prologue);
HX_GNUC_INTERNAL void _hoox_interceptor_backend_deactivate_trampoline (
    HooxInterceptorBackend * self, HooxFunctionContext * ctx, hx_pointer prologue);

HX_GNUC_INTERNAL hx_pointer _hoox_interceptor_backend_get_function_address (
    HooxFunctionContext * ctx);
HX_GNUC_INTERNAL hx_pointer _hoox_interceptor_backend_resolve_redirect (
    HooxInterceptorBackend * self, hx_pointer address);
HX_GNUC_INTERNAL hx_size _hoox_interceptor_backend_detect_hook_size (
    hx_constpointer code, hx_csh capstone, hx_insn * insn);

HX_GNUC_INTERNAL hx_pointer _hoox_interceptor_peek_top_caller_return_address (void);
HX_GNUC_INTERNAL hx_pointer _hoox_interceptor_translate_top_return_address (
    hx_pointer return_address);

#endif
