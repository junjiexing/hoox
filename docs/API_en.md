# hoox API reference

[中文](API.md) · **English** — back to [README](../README_en.md)

The entire public surface lives in a single header:

```c
#include "hoox.h"
```

Everything is declared `extern "C"`, so the header is usable from C and C++.
All public functions and types use the `hoox_` / `Hoox` / `HOOX_` prefix.

---

## Contents

- [Quick start](#quick-start)
- [Build-time macros](#build-time-macros)
- [Scalar types](#scalar-types)
- [Library lifecycle](#library-lifecycle)
- [Interceptor](#interceptor)
  - [Obtain / lifetime](#obtain--lifetime)
  - [attach / detach](#attach--detach)
  - [replace / replace_fast / revert](#replace--replace_fast--revert)
  - [Transactions](#transactions)
  - [Current invocation & stack](#current-invocation--stack)
  - [Thread scoping](#thread-scoping)
  - [Save / restore & locking](#save--restore--locking)
- [Invocation listeners](#invocation-listeners)
- [Invocation context](#invocation-context)
- [Options & enums](#options--enums)
- [Thread safety & concurrency](#thread-safety--concurrency)

---

## Quick start

```c
#include "hoox.h"

static int (*orig_open)(const char *, int);
static int my_open (const char * path, int flags) {
  return orig_open (path, flags);            /* call through to the original */
}

int main (void) {
  hoox_init ();
  HooxInterceptor * i = hoox_interceptor_obtain ();

  hoox_interceptor_replace (i, (hx_pointer) open, (hx_pointer) my_open,
      (hx_pointer *) &orig_open, NULL);
  /* ... open() is now my_open() ... */
  hoox_interceptor_revert (i, (hx_pointer) open);

  hoox_interceptor_unref (i);
  hoox_deinit ();
}
```

A fully worked, runnable tour of every feature is in
[`example/hook_example.c`](../example).

---

## Build-time macros

Consuming the amalgamated `hoox.c`/`hoox.h` needs **no** `-D` flags in the
common case. Define a macro only to opt out of a default:

| Macro | Effect |
|---|---|
| `HOOX_SHARED` | Consume hoox as a Windows DLL — `HOOX_API` becomes `__declspec(dllimport)`. Default is static linkage (`HOOX_API` empty). |
| `HOOX_EXPORTS` | Set **while building** a hoox DLL (with `HOOX_SHARED`) → `__declspec(dllexport)`. |
| `HOOX_USE_DLMALLOC` | Use a bundled dlmalloc instead of the system allocator. You must supply `dlmalloc.c` on the include path. Default: system `malloc`. |
| `HAVE_I386` / `HAVE_ARM` / `HAVE_ARM64`, `HAVE_WINDOWS` / `HAVE_LINUX` / `HAVE_DARWIN` | Force the target arch/OS. Normally auto-detected from compiler built-ins (`_M_X64`, `__aarch64__`, `_WIN32`, …); define only if detection cannot classify your target. |

On Windows, link `psapi`.

---

## Scalar types

Thin aliases used throughout the API:

| Type | Definition |
|---|---|
| `hx_int` | `int` |
| `hx_uint` | `unsigned int` |
| `hx_size` | `size_t` |
| `hx_boolean` | `int` (0 / non-0) |
| `hx_pointer` | `void *` |
| `hx_constpointer` | `const void *` |
| `hx_uint8` / `hx_uint16` / `hx_uint32` / `hx_uint64` | fixed-width unsigned ints (`uintN_t`) — used by the register context fields |
| `hx_float` / `hx_double` | `float` / `double` — used by the vector-register views |
| `HxDestroyNotify` | `void (*)(hx_pointer data)` — a cleanup callback |

---

## Library lifecycle

```c
void hoox_init (void);
void hoox_deinit (void);
void hoox_shutdown (void);
void hoox_init_embedded (void);
void hoox_deinit_embedded (void);
void hoox_prepare_to_fork (void);
void hoox_recover_from_fork_in_parent (void);
void hoox_recover_from_fork_in_child (void);
```

- **`hoox_init`** / **`hoox_deinit`** — bring the library up / tear it down.
  Call `hoox_init` once before using any other API; `hoox_deinit` at shutdown.
- **`hoox_shutdown`** — request an orderly shutdown of internal workers ahead of
  `hoox_deinit` (rarely needed directly).
- **`hoox_init_embedded`** / **`hoox_deinit_embedded`** — variant init for use
  when hoox is embedded into another runtime that manages process globals.
- **`hoox_prepare_to_fork`** / **`hoox_recover_from_fork_in_parent`** /
  **`..._in_child`** — bracket a `fork()` so internal locks/state are consistent
  in both processes (POSIX).

---

## Interceptor

The `HooxInterceptor` is the object that installs and removes hooks. It is a
process-wide singleton obtained with `hoox_interceptor_obtain`.

### Obtain / lifetime

```c
HooxInterceptor * hoox_interceptor_obtain (void);
HooxInterceptor * hoox_interceptor_ref (HooxInterceptor * self);
void hoox_interceptor_unref (HooxInterceptor * self);
void hoox_interceptor_set_default_options (HooxInterceptor * self,
    const HooxInterceptorOptions * options);
```

- **`hoox_interceptor_obtain`** — returns the shared interceptor (ref-counted;
  first call creates it). Pair every `obtain` with an `unref`.
- **`hoox_interceptor_ref` / `_unref`** — adjust the reference count; the
  instance is destroyed when it reaches zero.
- **`hoox_interceptor_set_default_options`** — set the default
  [`HooxInterceptorOptions`](#options--enums) applied to subsequent
  attach/replace calls that don't override them.

### attach / detach

```c
HooxAttachReturn hoox_interceptor_attach (HooxInterceptor * self,
    hx_pointer target, HooxInvocationListener * listener,
    const HooxAttachOptions * options);
void hoox_interceptor_detach (HooxInterceptor * self,
    HooxInvocationListener * listener);
```

- **`hoox_interceptor_attach`** — wrap `target` so `listener`'s `on_enter` fires
  before the original body and `on_leave` after it. The original body still
  runs. `options` may be `NULL`. Returns [`HooxAttachReturn`](#options--enums)
  (`HOOX_ATTACH_OK` on success).
  - Multiple listeners may attach to the same target (up to a small fixed
    maximum).
  - Inside the callbacks you inspect/modify arguments, return value, etc. via
    the [invocation context](#invocation-context).
- **`hoox_interceptor_detach`** — remove a previously attached `listener` from
  every target it was attached to.

### replace / replace_fast / revert

```c
HooxReplaceReturn hoox_interceptor_replace (HooxInterceptor * self,
    hx_pointer function_address, hx_pointer replacement_function,
    hx_pointer * original_function, const HooxReplaceOptions * options);
HooxReplaceReturn hoox_interceptor_replace_fast (HooxInterceptor * self,
    hx_pointer function_address, hx_pointer replacement_function,
    hx_pointer * original_function, const HooxInterceptorOptions * options);
void hoox_interceptor_revert (HooxInterceptor * self, hx_pointer target);
```

- **`hoox_interceptor_replace`** — redirect `function_address` to
  `replacement_function`. If `original_function` is non-NULL it receives a
  callable trampoline that reaches the original, so the replacement can call
  through. `options` may be `NULL`; via
  [`HooxReplaceOptions`](#options--enums) you can attach `replacement_data`
  (readable inside the replacement through
  `hoox_interceptor_get_current_invocation` +
  `hoox_invocation_context_get_replacement_data`). Returns
  [`HooxReplaceReturn`](#options--enums).
- **`hoox_interceptor_replace_fast`** — a lighter variant that redirects
  straight to `replacement_function` with the smallest possible trampoline and
  no invocation-context bookkeeping. Same trampoline-out semantics via
  `original_function`. Use when you don't need `get_current_invocation` in the
  replacement.
- **`hoox_interceptor_revert`** — undo an `attach`/`replace`/`replace_fast` on
  `target`, restoring the original bytes.

> A single target may be *attached* to (listeners) **or** *replaced*, not both.

### Transactions

```c
void hoox_interceptor_begin_transaction (HooxInterceptor * self);
void hoox_interceptor_end_transaction (HooxInterceptor * self);
hx_boolean hoox_interceptor_flush (HooxInterceptor * self);
```

- **`hoox_interceptor_begin_transaction` / `_end_transaction`** — group several
  attach/detach/replace/revert calls. Changes are *staged* between the two calls
  and committed together on `end_transaction` — faster, and atomic from the
  target's point of view. Transactions nest (they are counted); the outermost
  `end` performs the commit. Outside a transaction each change commits
  immediately.
- **`hoox_interceptor_flush`** — force any pending trampoline/code changes to
  become visible immediately. Returns whether anything was flushed.

### Current invocation & stack

```c
HooxInvocationContext * hoox_interceptor_get_current_invocation (void);
HooxInvocationStack * hoox_interceptor_get_current_stack (void);
hx_pointer hoox_invocation_stack_translate (HooxInvocationStack * self,
    hx_pointer return_address);
```

- **`hoox_interceptor_get_current_invocation`** — from within a listener callback
  or a `replace`ment, returns the current [`HooxInvocationContext`](#invocation-context),
  or `NULL` if not inside an intercepted call.
- **`hoox_interceptor_get_current_stack`** — the current thread's invocation
  stack (opaque handle), for use with `hoox_invocation_stack_translate`.
- **`hoox_invocation_stack_translate`** — map a `return_address` that points into
  a hoox trampoline back to the real caller address (useful when unwinding /
  symbolizing a stack that runs through a hook).

### Thread scoping

```c
void hoox_interceptor_ignore_current_thread (HooxInterceptor * self);
void hoox_interceptor_unignore_current_thread (HooxInterceptor * self);
void hoox_interceptor_ignore_other_threads (HooxInterceptor * self);
void hoox_interceptor_unignore_other_threads (HooxInterceptor * self);
```

- **`ignore_current_thread` / `unignore_current_thread`** — while ignored, the
  calling thread runs hooked functions *without* firing listeners. Nestable.
  The classic use is to avoid recursion when your own callback calls a function
  you have hooked.
- **`ignore_other_threads` / `unignore_other_threads`** — the inverse: suppress
  listeners on every thread *except* the current one (e.g. to observe only your
  own thread).

### Save / restore & locking

```c
void hoox_interceptor_save (HooxInvocationState * state);
void hoox_interceptor_restore (HooxInvocationState * state);
void hoox_interceptor_with_lock_held (HooxInterceptor * self,
    HooxInterceptorLockedFunc func, hx_pointer user_data);
hx_boolean hoox_interceptor_is_locked (HooxInterceptor * self);
```

- **`hoox_interceptor_save` / `_restore`** — save and restore the per-thread
  ignore state around a region (so you can temporarily change it and put it
  back exactly).
- **`hoox_interceptor_with_lock_held`** — run `func(user_data)` with the
  interceptor's internal lock held (advanced; for coordinating your own state
  with hook installation).
- **`hoox_interceptor_is_locked`** — whether the interceptor lock is currently
  held.

---

## Invocation listeners

A listener is what `attach` calls on entry/exit. There are two ready-made
constructors plus a vtable interface for custom listeners.

```c
typedef void (* HooxInvocationCallback) (HooxInvocationContext * context,
    hx_pointer user_data);

HooxInvocationListener * hoox_make_call_listener (
    HooxInvocationCallback on_enter, HooxInvocationCallback on_leave,
    hx_pointer data, HxDestroyNotify data_destroy);
HooxInvocationListener * hoox_make_probe_listener (
    HooxInvocationCallback on_hit, hx_pointer data, HxDestroyNotify data_destroy);

HooxInvocationListener * hoox_invocation_listener_ref (HooxInvocationListener * self);
void hoox_invocation_listener_unref (HooxInvocationListener * self);
```

- **`hoox_make_call_listener`** — a listener with separate `on_enter` and
  `on_leave` callbacks (either may be `NULL`). `data` is passed to both;
  `data_destroy` (may be `NULL`) is called on it when the listener is destroyed.
- **`hoox_make_probe_listener`** — a lighter, **enter-only** listener: `on_hit`
  fires once per call, at entry. Ideal for counters/tracing.
- **`hoox_invocation_listener_ref` / `_unref`** — reference counting. `unref` at
  zero calls the listener's `finalize` and frees it. After `detach`, `unref` the
  listener you created.

### Custom listener (vtable)

For full control, embed `HooxInvocationListener` as the **first** member of your
struct and initialize it with a vtable:

```c
struct _HooxInvocationListenerInterface {
  void (* on_enter) (HooxInvocationListener * self, HooxInvocationContext * ctx);
  void (* on_leave) (HooxInvocationListener * self, HooxInvocationContext * ctx);
};

void hoox_invocation_listener_init (HooxInvocationListener * self,
    const HooxInvocationListenerInterface * iface, HxDestroyNotify finalize);

#define HOOX_INVOCATION_LISTENER(obj) ((HooxInvocationListener *) (obj))
```

- Allocate your struct on the heap (with `malloc`/`calloc`); `unref` will
  `finalize` it (for embedded cleanup — may be `NULL`) and then free it.
- The same function may serve both `on_enter` and `on_leave`; branch on
  [`hoox_invocation_context_get_point_cut`](#invocation-context).
- Pass `HOOX_INVOCATION_LISTENER(your_ptr)` to `attach`/`detach`/`unref`.
- **`hoox_invocation_listener_on_enter` / `_on_leave`** dispatch to the vtable;
  you normally don't call them yourself.

---

## Invocation context

`HooxInvocationContext` is handed to every listener callback (and reachable
inside a `replace`ment via `hoox_interceptor_get_current_invocation`).

Public fields:

```c
struct _HooxInvocationContext {
  hx_pointer       function;      /* intercepted function (or hooked insn) address */
  HooxCpuContext * cpu_context;   /* register state at the hook point (layout public) */
  hx_int           system_error;  /* errno / GetLastError() for this call */
  /* ... private ... */
};
```

Accessors:

```c
HooxPointCut hoox_invocation_context_get_point_cut (HooxInvocationContext *);

hx_pointer hoox_invocation_context_get_nth_argument (HooxInvocationContext *, hx_uint n);
void       hoox_invocation_context_replace_nth_argument (HooxInvocationContext *, hx_uint n, hx_pointer value);
hx_pointer hoox_invocation_context_get_return_value (HooxInvocationContext *);
void       hoox_invocation_context_replace_return_value (HooxInvocationContext *, hx_pointer value);
hx_pointer hoox_invocation_context_get_return_address (HooxInvocationContext *);
hx_uint    hoox_invocation_context_get_thread_id (HooxInvocationContext *);
hx_uint    hoox_invocation_context_get_depth (HooxInvocationContext *);

hx_pointer hoox_invocation_context_get_listener_thread_data (HooxInvocationContext *, hx_size size);
hx_pointer hoox_invocation_context_get_listener_function_data (HooxInvocationContext *);
hx_pointer hoox_invocation_context_get_listener_invocation_data (HooxInvocationContext *, hx_size size);
hx_pointer hoox_invocation_context_get_replacement_data (HooxInvocationContext *);
```

- **`get_point_cut`** — `HOOX_POINT_ENTER` or `HOOX_POINT_LEAVE`; lets one
  callback tell entry from exit.
- **`get_nth_argument` / `replace_nth_argument`** — read / overwrite the `n`-th
  integer/pointer argument (by calling convention). Arguments read as
  `hx_pointer`; cast as needed (e.g. `(int)(intptr_t)`).
- **`get_return_value` / `replace_return_value`** — read / overwrite the return
  value (meaningful in `on_leave`).
- **`get_return_address`** — the address the intercepted function will return to.
- **`get_thread_id`** — the OS thread id of the current call.
- **`get_depth`** — nesting depth of intercepted calls on this thread (0 at the
  outermost).
- **Data slots** — scratch memory tied to different lifetimes:
  - **`get_listener_function_data`** — the per-attach pointer you set in
    `HooxAttachOptions.listener_function_data`.
  - **`get_listener_thread_data (size)`** — zero-initialized, per-(listener,
    thread) buffer of `size` bytes; persists across calls on that thread.
  - **`get_listener_invocation_data (size)`** — zero-initialized, per-call buffer
    of `size` bytes; the **same** pointer in `on_enter` and `on_leave`, so use it
    to pass state from entry to exit (timings, saved arguments, …).
  - **`get_replacement_data`** — the `HooxReplaceOptions.replacement_data`
    pointer, for functions installed via `replace`.

### CPU context

`context->cpu_context` points at the full register state captured at the hook
point. Its layout is **public** in `hoox.h` (`HooxCpuContext` expands per target
architecture to `HooxArm64CpuContext` / `HooxX64CpuContext` /
`HooxArmCpuContext` / `HooxIA32CpuContext`), so you can read and modify
registers directly — including, frida-style, at a probe placed on an **arbitrary
instruction address** rather than a function entry:

```c
static void on_hit (HooxInvocationContext * ic, hx_pointer u) {
  HooxCpuContext * c = ic->cpu_context;
#if defined (__aarch64__)
  printf ("pc=%llx x0=%llx sp=%llx\n",
      (unsigned long long) c->pc, (unsigned long long) c->x[0],
      (unsigned long long) c->sp);
#endif
}
/* attach to an instruction in the middle of a function (arm64 is fixed-width,
 * so entry+8 is a valid boundary) */
hoox_interceptor_attach (itc,
    (hx_pointer) ((hx_uint32 *) (void *) fn + 2),
    hoox_make_probe_listener (on_hit, NULL, NULL), NULL);
```

Per-arch fields (as in `hoox.h`): `HooxArm64CpuContext` has
`pc/sp/nzcv/x[29]/fp/lr/v[32]`; `HooxX64CpuContext` has `rip/r8..r15/rdi/rsi/
rbp/rsp/rbx/rdx/rcx/rax`; `HooxArmCpuContext` has `pc/sp/cpsr/r8..r12/v[16]/
r[8]/lr`; `HooxIA32CpuContext` has `eip/edi/esi/ebp/esp/ebx/edx/ecx/eax`.
Writing back (e.g. changing `x[0]`) takes effect when execution resumes. The
higher-level `get_nth_argument` / `get_return_value` are convenience wrappers
over this same context.

> **Arbitrary instruction position:** `attach`'s `target` only has to be a
> relocatable instruction boundary, not a function entry — the constraints match
> frida's (the overwritten bytes must be relocatable, and nothing may branch
> into that range). `hoox_make_probe_listener` (enter-only) is the clean probe
> primitive for an arbitrary address; a call listener's `on_leave` there means
> "when the enclosing function returns".

---

## Options & enums

```c
typedef struct {
  hx_int                  scratch_register;
  HooxInterceptorScenario scenario;
  HooxRelocationPolicy    relocation_policy;
  HooxWriteRedirectFunc   write_redirect;
  hx_pointer              write_redirect_data;
  hx_uint                 redirect_space_hint;
} HooxInterceptorOptions;

typedef struct {
  HooxInterceptorOptions     instrumentation;
  hx_pointer                 listener_function_data;
  HooxInvocationIgnorability ignorability;
} HooxAttachOptions;

typedef struct {
  HooxInterceptorOptions instrumentation;
  hx_pointer             replacement_data;
} HooxReplaceOptions;
```

- **`HooxInterceptorOptions`** — low-level knobs shared by attach/replace.
  `scratch_register` picks the register the trampoline may clobber (or leave
  default); `scenario` and `relocation_policy` control how conservatively the
  prologue is relocated; `write_redirect`/`write_redirect_data` let you supply a
  custom redirect writer; `redirect_space_hint` bounds prologue scanning. Zero
  the struct for sensible defaults.
- **`HooxAttachOptions`** — `listener_function_data` becomes
  `get_listener_function_data`; `ignorability` marks whether this hook may be
  suppressed by thread-ignoring.
- **`HooxReplaceOptions`** — `replacement_data` becomes
  `get_replacement_data`.

Enums:

| Enum | Values | Meaning |
|---|---|---|
| `HooxInterceptorScenario` | `DEFAULT`, `ONLINE`, `OFFLINE` | Whether other threads may be executing the target. **`ONLINE`** (the default) instruments conservatively; **`OFFLINE`** allows more aggressive rewriting when the target is quiescent (e.g. a freshly-spawned, still-suspended process). |
| `HooxInvocationIgnorability` | `IGNORABLE`, `UNIGNORABLE` | Whether `ignore_*_thread` may suppress this hook. |
| `HooxRelocationPolicy` | `DEFAULT`, `CHECKED`, `UNCHECKED`, `FORCED` | How strictly prologue relocation is validated. |
| `HooxRedirectWriteResult` | `WRITTEN`, `DECLINED` | Return of a custom `HooxWriteRedirectFunc`. |
| `HooxAttachReturn` | `OK(0)`, `WRONG_SIGNATURE(-1)`, `ALREADY_ATTACHED(-2)`, `POLICY_VIOLATION(-3)`, `WRONG_TYPE(-4)`, `TOO_MANY_LISTENERS(-5)` | Result of `attach`. `TOO_MANY_LISTENERS` means the target already has `HOOX_MAX_LISTENERS_PER_FUNCTION` (default 2) listeners; raise it with `-DHOOX_MAX_LISTENERS_PER_FUNCTION=N`. |
| `HooxReplaceReturn` | `OK(0)`, `WRONG_SIGNATURE(-1)`, `ALREADY_REPLACED(-2)`, `POLICY_VIOLATION(-3)`, `WRONG_TYPE(-4)` | Result of `replace`/`replace_fast`. |

`HooxPointCut` is `HOOX_POINT_ENTER` / `HOOX_POINT_LEAVE`.

---

## Thread safety & concurrency

- **Steady state is thread-safe.** Once a hook is installed, any number of
  threads may call the hooked function concurrently. Listener dispatch uses a
  lock-free copy-on-write listener set and a per-thread invocation stack.
- **Re-entrancy.** A listener callback that itself calls the hooked function
  won't recurse into the listener for that thread — wrap such calls with
  `hoox_interceptor_ignore_current_thread` / `_unignore_current_thread` if you
  need to be explicit.
- **Installation/removal is *not* fully atomic against a running target.** hoox
  (like frida-gum) does **not** suspend other threads on Windows/Linux. It
  builds the trampoline off to the side, relocates only the minimal prologue,
  and — with the default `SCENARIO_ONLINE` — refuses to relocate past a `CALL`
  where another thread could be waiting to return into the patched region. There
  remains a tiny window where a thread executing the exact bytes being
  overwritten at the patch instant could misbehave. This is unlike Microsoft
  Detours, which suspends all threads and rewrites their instruction pointers.
  In practice: install/remove hooks at a quiescent time, or suspend the other
  threads yourself around the transaction if you need Detours-level guarantees.
- **Transactions** make a *batch* of changes commit together and are the
  efficient way to install many hooks, but they do not add thread suspension.
