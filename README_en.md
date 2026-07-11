# hoox

[中文](README.md) · **English** — full API reference: [API_en.md](docs/API_en.md)

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
- **Zero-config to consume** — static linkage, the system allocator, and the
  target arch/OS are all defaults; drop `hoox.c`/`hoox.h` in and compile, no `-D`
  flags. Opt into a DLL with `HOOX_SHARED`, or dlmalloc with `HOOX_USE_DLMALLOC`.
- **Pure C99**, built with **CMake**, on **MSVC / clang / gcc** (the MSVC build
  is warning-clean under `/W3 /sdl /WX`).

All public and engine symbols use the `hoox_`/`Hoox`/`HOOX_` prefix; the internal
utility layers (nano-glib, decoder) use `hx_`/`Hx`/`HX_`. No third-party prefix
(`gum_`, `cs_`, `g_`/GLib) survives in the code.

## Status

The Windows vertical slice is complete and green on **x86 and x64** across
**MSVC 19.x, clang and gcc (MinGW)**: extraction → build → hooks firing → full
test suite → single-file amalgamation → example. Horizontal roll-out to other
platforms/architectures is next.

## Platform support

Legend: ✅ supported (builds & passes the full test suite) · 🧩 extracted
(sources are in-tree but not yet compiled/verified) · 📋 planned (not started)

| OS ＼ Arch | x86 | x86_64 | ARM | ARM64 | MIPS |
|---|:-:|:-:|:-:|:-:|:-:|
| **Windows** | ✅ | ✅ | 🧩 | 🧩 | 📋 |
| **Linux / Android** | 📋 | 📋 | 📋 | 📋 | 📋 |
| **macOS / iOS / tvOS** | 📋 | 📋 | 📋 | 📋 | 📋 |
| **FreeBSD / QNX** | 📋 | 📋 | 📋 | 📋 | 📋 |

The only directly usable combination today is **Windows × (x86 / x86_64)**,
green on all three compilers. The ARM/ARM64 architecture and backend sources
have been extracted from frida-gum into the tree (🧩) but are not yet wired into
the build or verified; other OSes still need their own backend (memory / TLS /
thread enumeration) before they can be enabled. MIPS is partial/experimental in
frida itself.

## Documentation

- **[API reference (English)](docs/API_en.md)** — every public function, type
  and option, with thread-safety notes. (中文: [API.md](docs/API.md))
- **[example/](example)** — a runnable tour of the whole API.
- Design & tasks: [`docs/PLAN.md`](docs/PLAN.md), [`docs/TASKS.md`](docs/TASKS.md).

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
