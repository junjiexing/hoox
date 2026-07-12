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
- **Amalgamatable** — a script merges the sources into a single `hoox.c` + `hoox.h`
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

The Windows vertical slice is complete and green on **x86 and x64** across
**MSVC 19.x, clang and gcc (MinGW)**: extraction → build → hooks firing → full
test suite → single-file amalgamation → example. **Windows ARM64 works too**
(native `windows-11-arm` CI, MSVC; the interceptor behaviour suite and all other
tests pass). **Linux now covers x86 / x86_64 / ARM / ARM64**: x86 and x86_64 on
gcc/clang; ARM64 built and fully tested on the native `ubuntu-24.04-arm` CI; ARM
(32-bit, A32 + Thumb) cross-compiled (`gcc-arm-linux-gnueabihf`) and run through
the full ctest suite under `qemu-arm`. **macOS now covers x86_64 and ARM64**
(native `macos-15-intel` and Apple Silicon `macos-15` CI, AppleClang; interceptor
behaviour suite green on both). **FreeBSD now covers x86 / x86_64** (an amd64
FreeBSD VM, clang, including 32-bit via `-m32`) **and ARM64** (a `cross-platform-actions`
FreeBSD/arm64 guest). **Android now covers all four ABIs** (NDK cross-build of
arm64-v8a / armeabi-v7a / x86_64 / x86, statically linked and run through the full
ctest suite under `qemu-user`). Horizontal roll-out to other platforms is next.

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
| **iOS / tvOS** | ➖ | ➖ | ➖ | 📋 |
| **FreeBSD** | ✅ | ✅ | 🧩 | ✅ |

Directly usable today: **Windows × (x86 / x86_64 / ARM64)**,
**Linux × (x86 / x86_64 / ARM / ARM64)**, **macOS × (x86_64 / ARM64)**,
**FreeBSD × (x86 / x86_64 / ARM64)** and **Android × (x86 / x86_64 / ARM / ARM64)**.
Windows ARM64 is built and fully
tested on the native `windows-11-arm` runner; it reuses the same
`src/backend/windows` (TLS falls back to `TlsGetValue` off x86) and adds an
in-tree AArch64 decoder (`src/disasm/hx_disasm_arm64.c`) that drives the
`src/arch/arm64` relocator/reader. 32-bit ARM on Windows is N/A — Windows on ARM
is ARM64-only. The Linux backend (`src/backend/posix` + `src/backend/linux`)
takes the RWX path, drives page protection / near-allocation off
`/proc/self/maps`, uses pthread keys for TLS, and enumerates/suspends threads
via `/proc` + `tgkill`; the one arch-agnostic backend covers all four
architectures. x86_64 builds directly; x86 adds `-DCMAKE_C_FLAGS="-m32"` (needs
`gcc-multilib`); ARM64 uses the native `ubuntu-24.04-arm` runner; ARM (32-bit)
cross-compiles with `gcc-arm-linux-gnueabihf` and runs its tests under
`qemu-arm`. 32-bit ARM uses an in-tree A32+Thumb decoder
(`src/disasm/hx_disasm_arm.c`) driving `src/arch/arm`. **macOS** reuses the
x86/arm64 decoders and arch, the POSIX allocator + pthread TLS, and adds
`src/backend/darwin` (mach VM query/enumerate, dyld module enumeration). The key
detail is that patching code-signed `__TEXT` pages requires `mach_vm_protect`
with `VM_PROT_COPY` (a private, writable-then-executable copy — sidestepping
W^X). x86_64 is verified on `macos-15-intel` and ARM64 on the Apple Silicon
`macos-15` runner; the interceptor behaviour suite is green on both. (An in-tree
Darwin code segment, `hooxcodesegment-darwin.c`, is also provided for older
kernels.) **FreeBSD** reuses the x86/arm64 decoders and arch, the POSIX allocator
+ pthread TLS, and adds `src/backend/freebsd`: it takes the RWX path (`mprotect`,
no `VM_PROT_COPY` needed), drives page-protection queries and near-allocation off
`sysctl KERN_PROC_VMMAP` (not `/proc`), gets the thread id via
`pthread_getthreadid_np`, suspends/enumerates threads with `thr_kill` +
`sysctl KERN_PROC`, and enumerates modules with `dl_iterate_phdr`. As there is no
hosted FreeBSD runner, CI boots an amd64 FreeBSD VM on the ubuntu host
(`vmactions`): **x86_64 builds and runs the full suite natively, and x86 (32-bit)
builds and runs the full suite via `clang -m32` (freebsd32/lib32)**. **ARM64
FreeBSD is tested too**: `cross-platform-actions` boots a real **FreeBSD/arm64
15.1 guest** on the ubuntu host (QEMU emulating aarch64) and compiles + runs the
full suite natively inside it (interceptor behaviour suite and amalgam included)
— the same TCG path already exercises hoox's code-patching on the Linux `qemu-arm`
job. ARM (32-bit) FreeBSD shares this OS backend plus the A32/Thumb arch layer
validated on the Linux `qemu-arm` job (FreeBSD armv7 is tier-2 with no ready CI
image), so it is covered by construction rather than executed. **Android** is a
Linux kernel with bionic libc, so it reuses `src/backend/posix` + `src/backend/linux`
wholesale and adds only a tiny `src/backend/android` (`hoox_android_get_api_level` —
on API 29+ executable code pages may be unreadable, so the engine adds READ before
the decoder reads them). CI cross-builds all four ABIs with the NDK (statically
linked); since bionic is a Linux ABI, the static test binaries run under
`qemu-<arch>-static` just like the Linux ARM32 job — arm64-v8a / armeabi-v7a /
x86_64 / x86 all run the full ctest suite green. Other OSes still need their own
backend.

> **⚠️ Apple Silicon limitation (self-hosting):** on Apple Silicon (16 KiB pages
> + enforced W^X), patching a page briefly removes its execute permission. If the
> hooked function shares its 16 KiB page with hoox's *own* patch-time code, the
> patch faults. This only happens when hoox is statically linked into the same
> binary as the target and the two land on the same page; hooking other modules/
> libraries is unaffected. hoox **detects this collision at attach/replace time
> and returns an error (`HOOX_ATTACH_POLICY_VIOLATION` / the matching replace
> code) instead of crashing**. Removing it entirely needs a patch-through-a-
> separate-mapping mechanism (planned).

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
