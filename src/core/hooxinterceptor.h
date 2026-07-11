/*
 * Copyright (C) 2008-2026 Ole André Vadla Ravnås <oleavr@nowsecure.com>
 * Copyright (C) 2008 Christian Berentsen <jc.berentsen@gmail.com>
 * Copyright (C) 2024 Francesco Tamagni <mrmacete@protonmail.ch>
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_INTERCEPTOR_H__
#define __HOOX_INTERCEPTOR_H__

#include "hooxdefs.h"
#include "hooxinvocationlistener.h"


HX_BEGIN_DECLS

typedef struct _HooxInterceptor HooxInterceptor;

HOOX_API HooxInterceptor * hoox_interceptor_ref (HooxInterceptor * self);
HOOX_API void hoox_interceptor_unref (HooxInterceptor * self);

typedef HxArray HooxInvocationStack;
typedef hx_uint HooxInvocationState;
typedef void (* HooxInterceptorLockedFunc) (hx_pointer user_data);

typedef enum {
  HOOX_INTERCEPTOR_SCENARIO_DEFAULT,
  HOOX_INTERCEPTOR_SCENARIO_ONLINE,
  HOOX_INTERCEPTOR_SCENARIO_OFFLINE,
} HooxInterceptorScenario;

typedef enum {
  HOOX_INVOCATION_IGNORABLE,
  HOOX_INVOCATION_UNIGNORABLE,
} HooxInvocationIgnorability;

typedef enum _HooxRedirectWriteResult {
  HOOX_REDIRECT_WRITTEN,
  HOOX_REDIRECT_DECLINED,
} HooxRedirectWriteResult;

typedef struct _HooxInterceptorOptions HooxInterceptorOptions;
typedef struct _HooxAttachOptions HooxAttachOptions;
typedef struct _HooxReplaceOptions HooxReplaceOptions;
typedef struct _HooxRedirectWriteDetails HooxRedirectWriteDetails;

typedef HooxRedirectWriteResult (* HooxWriteRedirectFunc) (
    const HooxRedirectWriteDetails * details, hx_pointer user_data);

struct _HooxInterceptorOptions
{
  hx_int scratch_register;
  HooxInterceptorScenario scenario;
  HooxRelocationPolicy relocation_policy;
  HooxWriteRedirectFunc write_redirect;
  hx_pointer write_redirect_data;
  hx_uint redirect_space_hint;
};

struct _HooxAttachOptions
{
  HooxInterceptorOptions instrumentation;
  hx_pointer listener_function_data;
  HooxInvocationIgnorability ignorability;
};

struct _HooxReplaceOptions
{
  HooxInterceptorOptions instrumentation;
  hx_pointer replacement_data;
};

struct _HooxRedirectWriteDetails
{
  hx_pointer writer;
  hx_pointer target;
  hx_int scratch_register;
  hx_uint capacity;
};

typedef enum
{
  HOOX_ATTACH_OK               =  0,
  HOOX_ATTACH_WRONG_SIGNATURE  = -1,
  HOOX_ATTACH_ALREADY_ATTACHED = -2,
  HOOX_ATTACH_POLICY_VIOLATION = -3,
  HOOX_ATTACH_WRONG_TYPE       = -4,
} HooxAttachReturn;

typedef enum
{
  HOOX_REPLACE_OK               =  0,
  HOOX_REPLACE_WRONG_SIGNATURE  = -1,
  HOOX_REPLACE_ALREADY_REPLACED = -2,
  HOOX_REPLACE_POLICY_VIOLATION = -3,
  HOOX_REPLACE_WRONG_TYPE       = -4,
} HooxReplaceReturn;

HOOX_API HooxInterceptor * hoox_interceptor_obtain (void);

HOOX_API void hoox_interceptor_set_default_options (HooxInterceptor * self,
    const HooxInterceptorOptions * options);

HOOX_API HooxAttachReturn hoox_interceptor_attach (HooxInterceptor * self,
    hx_pointer target, HooxInvocationListener * listener,
    const HooxAttachOptions * options);
HOOX_API void hoox_interceptor_detach (HooxInterceptor * self,
    HooxInvocationListener * listener);

HOOX_API HooxReplaceReturn hoox_interceptor_replace (HooxInterceptor * self,
    hx_pointer function_address, hx_pointer replacement_function,
    hx_pointer * original_function, const HooxReplaceOptions * options);
HooxReplaceReturn hoox_interceptor_replace_fast (HooxInterceptor * self,
    hx_pointer function_address, hx_pointer replacement_function,
    hx_pointer * original_function, const HooxInterceptorOptions * options);
HOOX_API void hoox_interceptor_revert (HooxInterceptor * self,
    hx_pointer target);

HOOX_API void hoox_interceptor_begin_transaction (HooxInterceptor * self);
HOOX_API void hoox_interceptor_end_transaction (HooxInterceptor * self);
HOOX_API hx_boolean hoox_interceptor_flush (HooxInterceptor * self);
HOOX_API hx_boolean hoox_interceptor_flush_function (HooxInterceptor * self,
    hx_constpointer function_address);
HOOX_API hx_boolean hoox_interceptor_flush_listener (HooxInterceptor * self,
    HooxInvocationListener * listener);

HOOX_API HooxInvocationContext * hoox_interceptor_get_current_invocation (void);
HOOX_API HooxInvocationContext * hoox_interceptor_get_live_replacement_invocation (
    hx_pointer replacement_function);
HOOX_API HooxInvocationStack * hoox_interceptor_get_current_stack (void);

HOOX_API void hoox_interceptor_ignore_current_thread (HooxInterceptor * self);
HOOX_API void hoox_interceptor_unignore_current_thread (HooxInterceptor * self);
HOOX_API hx_boolean hoox_interceptor_maybe_unignore_current_thread (
    HooxInterceptor * self);

HOOX_API void hoox_interceptor_ignore_other_threads (HooxInterceptor * self);
HOOX_API void hoox_interceptor_unignore_other_threads (HooxInterceptor * self);

HOOX_API hx_pointer hoox_invocation_stack_translate (HooxInvocationStack * self,
    hx_pointer return_address);

HOOX_API void hoox_interceptor_save (HooxInvocationState * state);
HOOX_API void hoox_interceptor_restore (HooxInvocationState * state);

HOOX_API void hoox_interceptor_with_lock_held (HooxInterceptor * self,
    HooxInterceptorLockedFunc func, hx_pointer user_data);
HOOX_API hx_boolean hoox_interceptor_is_locked (HooxInterceptor * self);


HX_END_DECLS

#endif
