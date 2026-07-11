# hoox

A minimal **inline-hooking** library in pure C, extracted and slimmed down from
[frida-gum](https://github.com/frida/frida-gum). hoox keeps only the inline-hook
(Interceptor) capability and drops everything else (Stalker, backtracer, symbol
resolution, JS bindings, …).

## Goals

- **No GLib dependency** — replaced by an in-tree nano-glib compatibility layer (`hx_*`).
- **No capstone dependency** — replaced by a compact in-tree instruction decoder
  (`hx_disasm`, informed by Microsoft Detours' relocation engine).
- **No third-party runtime dependencies.**
- **Cross-platform** — Windows / Linux / Android / macOS / iOS / FreeBSD / QNX ×
  x86 / x86_64 / ARM / ARM64 / MIPS (coverage staged, on par with Frida).
- **Amalgamatable** — a script merges the sources into a single `hoox.c` + `hoox.h`
  (SQLite-style); the public `hoox.h` exposes only the API.
- **Pure C99**, built with **CMake**, on **MSVC / clang / gcc**.

All public and engine symbols use the `hoox_`/`Hoox`/`HOOX_` prefix; the internal
utility layers (nano-glib, decoder) use `hx_`/`Hx`/`HX_`. No third-party prefix
(`gum_`, `cs_`, `g_`/GLib) survives in the code.

## Status

The Windows vertical slice is complete and green on **x86 and x64** across
**MSVC 19.x, clang and gcc (MinGW)**: extraction → build → hooks firing → full
test suite → single-file amalgamation → example. Horizontal roll-out to other
platforms/architectures is next. See [`docs/PLAN.md`](docs/PLAN.md) and
[`docs/TASKS.md`](docs/TASKS.md).

## Build

```
cmake -B build -G Ninja        # or -DCMAKE_C_COMPILER=clang / gcc, or the VS generator for MSVC
cmake --build build
ctest --test-dir build --output-on-failure
```

The default build targets the host arch (x64). For a 32-bit (x86) build, target
i686 and point `LIB` at the 32-bit MSVC/SDK library directories:

```
export LIB="<VC>/lib/x86;<SDK>/ucrt/x86;<SDK>/um/x86"
cmake -B build32 -G Ninja -DCMAKE_C_COMPILER=clang \
  -DCMAKE_C_FLAGS="--target=i686-pc-windows-msvc" -DHOOX_TARGET_ARCH=x86
cmake --build build32 && ctest --test-dir build32 --output-on-failure
```

Both Windows x64 and x86 pass the full suite (interceptor, writer/relocator,
decoder differential, amalgamation) on all three compilers.

## Licence

wxWindows Library Licence, Version 3.1 (same as frida-gum). See [`COPYING`](COPYING)
and [`NOTICE`](NOTICE).
