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

The demo hooks a plain function, `compute(int, int)`, two different ways:

1. **`hoox_interceptor_attach`** — wraps the function with an *invocation
   listener*. The original body still runs; the listener sees each call on the
   way in (`on_enter`) and out (`on_leave`), reads the arguments, and tampers
   with the return value (`+1`) without modifying `compute` at all.

2. **`hoox_interceptor_replace`** — swaps the function body for
   `replacement_compute`, keeping a trampoline pointer (`original_compute`) that
   still reaches the real `compute`. The replacement calls the original and
   scales the result (`* 10`).

After each step it `detach`/`revert`s to prove the target returns to normal.

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
baseline: compute(3, 4) = 7

== attach listener ==
  [listener] enter: compute(3, 4)  thread=... depth=0
  [listener] leave: original returned 7, tampering -> 8
caller sees: compute(3, 4) = 8  (7 from body, +1 from listener)
  [listener] enter: compute(10, 20)  thread=... depth=0
  [listener] leave: original returned 30, tampering -> 31
caller sees: compute(10, 20) = 31
listener observed 2 call(s)

after detach: compute(3, 4) = 7  (back to normal)

== replace function ==
  [replace] intercepted compute(5, 6); original=11, returning 110
caller sees: compute(5, 6) = 110  ((5+6) * 10)

after revert: compute(5, 6) = 11  (back to normal)

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

- `hoox_init` / `hoox_deinit`
- `hoox_interceptor_obtain` / `_ref` / `_unref`
- `hoox_make_call_listener`, `hoox_invocation_listener_unref`
- `hoox_interceptor_attach` / `_detach`
- `hoox_interceptor_replace` / `_revert`
- `hoox_invocation_context_get_nth_argument`, `_get_return_value`,
  `_replace_return_value`, `_get_thread_id`, `_get_depth`

See `hoox.h` for the full surface.
