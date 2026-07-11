/*
 * hoox — public umbrella header (trimmed frida-gum hoox.h).
 *
 * Exposes the inline-hook surface: init/deinit plus the Interceptor,
 * invocation listener/context, and the memory API.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __HOOX_H__
#define __HOOX_H__

#include "hooxdefs.h"
#include "hooxmemory.h"
#include "hooxinterceptor.h"
#include "hooxinvocationcontext.h"
#include "hooxinvocationlistener.h"
#include "hooxreturnaddress.h"

HX_BEGIN_DECLS

HOOX_API void hoox_init (void);
HOOX_API void hoox_shutdown (void);
HOOX_API void hoox_deinit (void);

HOOX_API void hoox_init_embedded (void);
HOOX_API void hoox_deinit_embedded (void);

HOOX_API void hoox_prepare_to_fork (void);
HOOX_API void hoox_recover_from_fork_in_parent (void);
HOOX_API void hoox_recover_from_fork_in_child (void);

HX_END_DECLS

#endif
