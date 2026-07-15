# hoox

[中文](README.md) · **English** — full API reference: [API_en.md](docs/API_en.md)

> [!WARNING]
> This project was **developed entirely by AI (vibe coding)** and has **not been
> rigorously tested**. The in-tree test suite is green on the supported
> platforms, but correctness and stability in production or edge-case scenarios
> are not guaranteed. **Use with caution, at your own risk.**

A minimal **inline-hooking** library in pure C, extracted and slimmed down from
[frida-gum](https://github.com/frida/frida-gum). hoox keeps only the inline-hook
(Interceptor) capability and drops everything else (Stalker, backtracer, symbol
resolution, JS bindings, …).

## Goals

- **No GLib dependency** — replaced by an in-tree nano-glib compatibility layer (`hx_*`).
- **No capstone dependency** — replaced by a compact in-tree instruction decoder
  (`hx_disasm`, informed by Microsoft Detours' relocation engine).
- **No third-party runtime dependencies.**
- **Cross-platform** — Windows / Linux / Android / macOS / iOS / FreeBSD ×
  x86 / x86_64 / ARM / ARM64 (coverage staged).
- **Amalgamatable** — a script merges the selected target sources into one `hoox.c` + `hoox.h`
  (SQLite-style); the public `hoox.h` exposes only the API.
- **Zero-config to consume** — static linkage, the system allocator, and the
  target arch/OS are all defaults; drop `hoox.c`/`hoox.h` in and compile, no `-D`
  flags. Opt into a DLL with `HOOX_SHARED`, or dlmalloc with `HOOX_USE_DLMALLOC`.
  On POSIX, link the platform threads library (`-pthread`) — hoox uses pthread
  keys for TLS. This is required on **FreeBSD** (pthread lives in `libthr`; unlinked,
  the symbols resolve to libc's no-op weak stubs and `pthread_setspecific/getspecific`
  silently do nothing). glibc folds pthread into libc, so Linux usually needs no
  explicit link.
- **Pure C99**, built with **CMake**, on **MSVC / clang / gcc** (the MSVC build
  is warning-clean under `/W3 /sdl /WX`).

All public and engine symbols use the `hoox_`/`Hoox`/`HOOX_` prefix; the internal
utility layers (nano-glib, decoder) use `hx_`/`Hx`/`HX_`. No third-party prefix
(`gum_`, `cs_`, `g_`/GLib) survives in the code.

## Status

**Windows / Linux / Android / macOS / iOS / tvOS / FreeBSD** are all working and
CI-tested (toolchains: MSVC / clang / gcc / MinGW / AppleClang / NDK; full chain
extraction → build → hooks firing → test suite → amalgamation → example). See the
coverage matrix below. CI cross-compiles the iOS/tvOS device SDKs and runs the
simulators; iOS ARM64 has also passed on a real device, while tvOS has not.

## Platform support

Legend: ✅ supported (builds & passes the full test suite) · 🧩 extracted
(sources are in-tree but not yet compiled/verified) · 📋 planned (not started) ·
➖ N/A (no such architecture on this platform)

| OS ＼ Arch | x86 | x86_64 | ARM | ARM64 |
|---|:-:|:-:|:-:|:-:|
| **Windows** | ✅ | ✅ | ➖ | ✅ |
| **Linux** | ✅ | ✅ | ✅ | ✅ |
| **Android** | ✅ | ✅ | ✅ | ✅ |
| **macOS** | ➖ | ✅ | ➖ | ✅ |
| **iOS** | ➖ | ➖ | ➖ | ✅ ‡ |
| **tvOS** | ➖ | ➖ | ➖ | ✅ † |
| **FreeBSD** | ✅ | ✅ | 🧩 | ✅ |

‡ iOS ARM64: the full interceptor, amalgam, and selfhost suites pass on an iPhone 6s (A9, arm64, non-arm64e) running iOS 15.8.2 with Dopamine.

† tvOS ARM64: device-SDK cross-compile + simulator run are green; **a real tvOS device has not been tested**. iOS arm64e is likewise outside the current device coverage.

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

An amalgamation contains only the sources selected for the configured CMake
platform/architecture; it is not a universal package. Generate it alone with
`-DHOOX_ENABLE_TESTS=OFF -DHOOX_BUILD_AMALGAMATION=ON`; release names state the target.

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
