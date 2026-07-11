# hoox example

A complete example built against the **amalgamated single-file** hoox library
the project generates at `build/amalgam/hoox.{c,h}`. The example does not carry
its own copy — it compiles and links the project-generated amalgamation.

```
example/
├── hook_example.c    the demo
├── CMakeLists.txt    standalone build (points at ../build/amalgam)
└── README.md
```

## What it shows

A guided tour of the public API, hooking two plain functions
(`compute`/`mul`). Each part `detach`/`revert`s afterward so the target
returns to normal:

1. **attach a call listener** — `on_enter`/`on_leave` read and *rewrite* an
   argument (`replace_nth_argument`) and the return value
   (`replace_return_value`), and read `get_return_address` / `get_thread_id` /
   `get_depth`.
2. **custom listener** — implement the `HooxInvocationListener` vtable
   directly, branch on `get_point_cut`, and use all three data slots
   (function data via `HooxAttachOptions`, per-thread, per-invocation).
3. **probe listener** — a lightweight enter-only `hoox_make_probe_listener`.
4. **transaction** — `begin_transaction`/`end_transaction` batch two attaches
   so they activate as a unit.
5. **replace** — swap the body, reach the original through a trampoline, and
   read `replacement_data` via `get_current_invocation`.
6. **replace_fast** — the lighter direct-replacement variant.
7. **ignore current thread** — `ignore`/`unignore_current_thread` silences a
   hook's listener for the calling thread.

## Build & run

Builds with **MSVC, clang, or gcc**.

First generate the single-file library from the repo root (once):

```sh
cmake -S .. -B ../build -G Ninja -DHOOX_BUILD_AMALGAMATION=ON
cmake --build ../build --target test_amalgam
```

Then build and run the example, which links `../build/amalgam/hoox.c`:

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/hook_example            # Windows: .\build\hook_example.exe
```

Override the amalgam location with `-DHOOX_AMALGAM_DIR=<dir>` if it lives
elsewhere.

### Or compile directly

No feature macros are needed — static linkage, the system allocator, and the
target arch/OS are all defaults:

```sh
# Windows x64, from this directory (clang/gcc; MSVC cl works the same way)
clang -I ../build/amalgam \
      ../build/amalgam/hoox.c hook_example.c -lpsapi -o hook_example
```

## Expected output

```
baseline: compute(3, 4) = 7, mul(6, 7) = 42

== 1. attach a call listener ==
  [listener] enter: compute(3, 4) -> forcing b=40  thread=... depth=0 ret_addr=...
  [listener] leave: body returned 43, bumping -> 44
caller sees: compute(3, 4) = 44  (3 + 4*10 = 43, +1 = 44)
  ...
== 2. custom listener + data slots + point-cut ==
  [tap:compute] enter a=2 b=5  (call #1 on this thread)
  [tap] leave: a+b(from enter)=7 result=7 cumulative=7
  ...
== 3. probe listener (counts calls) ==
mul was called 4 time(s); results still correct (e.g. 6*7=42)
== 4. transaction (batch two attaches) ==
two hooks installed as one transaction; combined hits = 2
== 5. replace function body ==
  [replace:v1] compute(5, 6): original=11, returning 110
caller sees: compute(5, 6) = 110  ((5+6) * 10)
after revert: compute(5, 6) = 11  (back to normal)
== 6. replace_fast ==
caller sees: mul(6, 7) = 43  (42 + 1)
after revert: mul(6, 7) = 42
== 7. ignore current thread ==
hits with one ignored + one observed call = 1 (expected 1)

done.
```

## Compile definitions

None are required for the common case. hoox defaults to **static linkage** and
the **system allocator**, and detects the target **architecture and OS** from
the compiler's built-in macros. On Windows you only link `psapi` (the
`CMakeLists.txt` here does that for you).

Define a macro only to opt out of a default:

| Define              | Effect                                                     |
|---------------------|------------------------------------------------------------|
| `HOOX_SHARED`       | Consume hoox as a Windows DLL (`__declspec(dllimport)`).   |
| `HOOX_USE_DLMALLOC` | Use a bundled dlmalloc instead of the system allocator (you must supply `dlmalloc.c` on the include path). |
| `HAVE_I386` etc.    | Force the target arch/OS if auto-detection can't classify it. |

## Public API used

- Lifecycle: `hoox_init` / `hoox_deinit`
- Interceptor: `hoox_interceptor_obtain` / `_ref` / `_unref`, `_attach` /
  `_detach`, `_replace` / `_replace_fast` / `_revert`,
  `_begin_transaction` / `_end_transaction`,
  `_ignore_current_thread` / `_unignore_current_thread`,
  `_get_current_invocation`
- Listeners: `hoox_make_call_listener`, `hoox_make_probe_listener`,
  `hoox_invocation_listener_init` (custom vtable), `_unref`,
  `HOOX_INVOCATION_LISTENER`
- Options: `HooxAttachOptions.listener_function_data`,
  `HooxReplaceOptions.replacement_data`
- Invocation context: `get_point_cut`, `get_nth_argument`,
  `replace_nth_argument`, `get_return_value`, `replace_return_value`,
  `get_return_address`, `get_thread_id`, `get_depth`,
  `get_listener_function_data` / `_thread_data` / `_invocation_data`,
  `get_replacement_data`

See `hoox.h` for the full surface.
