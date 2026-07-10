/*
 * hoox — public umbrella header (trimmed frida-gum gum.h).
 *
 * Exposes the inline-hook surface: init/deinit plus the Interceptor,
 * invocation listener/context, and the memory API.
 *
 * Licence: wxWindows Library Licence, Version 3.1
 */

#ifndef __GUM_H__
#define __GUM_H__

#include "gumdefs.h"
#include "gummemory.h"
#include "guminterceptor.h"
#include "guminvocationcontext.h"
#include "guminvocationlistener.h"
#include "gumreturnaddress.h"

G_BEGIN_DECLS

GUM_API void gum_init (void);
GUM_API void gum_shutdown (void);
GUM_API void gum_deinit (void);

GUM_API void gum_init_embedded (void);
GUM_API void gum_deinit_embedded (void);

GUM_API void gum_prepare_to_fork (void);
GUM_API void gum_recover_from_fork_in_parent (void);
GUM_API void gum_recover_from_fork_in_child (void);

G_END_DECLS

#endif
