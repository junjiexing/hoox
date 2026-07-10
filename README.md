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
  (SQLite-style).
- **Pure C11**, built with **CMake**.

## Status

Under active development. Vertical slice first: **Windows x64**, then horizontal
roll-out to other platforms/architectures. See [`docs/PLAN.md`](docs/PLAN.md) and
[`docs/TASKS.md`](docs/TASKS.md).

## Build

```
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows: clang / clang-cl is the recommended toolchain (see `docs/PLAN.md` D5).

## Licence

wxWindows Library Licence, Version 3.1 (same as frida-gum). See [`COPYING`](COPYING)
and [`NOTICE`](NOTICE).
