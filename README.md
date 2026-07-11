# hoox

**中文** · [English](README_en.md) — 完整 API 参考：[API.md](docs/API.md)

> [!WARNING]
> 本项目**完全由 AI（Vibe Coding）自动开发完成**，**未经过严格测试**。
> 虽然仓库内的测试套件在已支持的平台上全绿，但不保证在生产环境或边缘场景下的
> 正确性与稳定性。**请谨慎使用，风险自负。**

一个用纯 C 编写的极简 **inline hook（内联挂钩）** 库，从
[frida-gum](https://github.com/frida/frida-gum) 中精简提取而来。hoox 只保留
inline hook（Interceptor）能力，去掉了其余一切（Stalker、backtracer、符号解析、
JS 绑定等）。

## 目标

- **无 GLib 依赖** —— 用仓库内自研的 nano-glib 兼容层（`hx_*`）替代。
- **无 capstone 依赖** —— 用一个紧凑的自研指令解码器替代
  （`hx_disasm`，参考 Microsoft Detours 的重定位引擎）。
- **无任何第三方运行时依赖。**
- **跨平台** —— Windows / Linux / Android / macOS / iOS / FreeBSD / QNX ×
  x86 / x86_64 / ARM / ARM64（分阶段覆盖）。
- **可合并为单文件** —— 一个脚本把所有源码合并成单一的 `hoox.c` + `hoox.h`
  （SQLite 风格）；公共头 `hoox.h` 只暴露 API。
- **零配置即可使用** —— 静态链接、系统分配器、目标架构/OS 均为默认值；直接把
  `hoox.c`/`hoox.h` 放进项目编译即可，无需任何 `-D`。需要动态库时定义
  `HOOX_SHARED`，需要 dlmalloc 时定义 `HOOX_USE_DLMALLOC`。
- **纯 C99**，用 **CMake** 构建，支持 **MSVC / clang / gcc**（MSVC 在
  `/W3 /sdl /WX` 下零告警）。

所有公共 API 与引擎符号使用 `hoox_`/`Hoox`/`HOOX_` 前缀；内部工具层（nano-glib、
解码器）使用 `hx_`/`Hx`/`HX_` 前缀。代码中不残留任何第三方前缀
（`gum_`、`cs_`、`g_`/GLib）。

## 状态

Windows 垂直切片已完成，并在 **x86 与 x64** 上、跨 **MSVC 19.x、clang、
gcc (MinGW)** 全部通过：提取 → 构建 → hook 生效 → 完整测试套件 → 单文件合并 →
example。**Windows ARM64 亦已打通**（原生 `windows-11-arm` CI，MSVC，含 interceptor 行为
测试全绿）。**Linux 已覆盖 x86 / x86_64 / ARM / ARM64**：x86 与 x86_64 走 gcc/clang；ARM64 在
原生 `ubuntu-24.04-arm` CI 上构建并通过完整测试；ARM（32 位，A32 + Thumb）交叉编译
（`gcc-arm-linux-gnueabihf`）后在 `qemu-arm` 下跑完整测试套件。**macOS 已覆盖 x86_64 与 ARM64**
（原生 `macos-15-intel` / `macos-15` CI，AppleClang，interceptor 行为套件全绿）。下一步向 iOS/其它
平台横向铺开。

## 平台支持

图例：✅ 已支持（可构建并通过完整测试） · 🧩 源码已提取（在库中但尚未编译/验证） ·
📋 规划中（尚未开始） · ➖ 不适用（该平台无此架构）

| OS ＼ 架构 | x86 | x86_64 | ARM | ARM64 |
|---|:-:|:-:|:-:|:-:|
| **Windows** | ✅ | ✅ | ➖ | ✅ |
| **Linux** | ✅ | ✅ | ✅ | ✅ |
| **Android** | 📋 | 📋 | 📋 | 📋 |
| **macOS** | ➖ | ✅ | ➖ | ✅ |
| **iOS / tvOS** | ➖ | ➖ | ➖ | 📋 |
| **FreeBSD / QNX** | 📋 | 📋 | 📋 | 📋 |

可直接使用的组合是 **Windows × (x86 / x86_64 / ARM64)**、**Linux × (x86 / x86_64 / ARM / ARM64)**
与 **macOS × (x86_64 / ARM64)**。
Windows ARM64 在原生 `windows-11-arm` runner 上由 CI 构建并通过完整测试套件（含 interceptor
行为测试）；它复用同一份 `src/backend/windows`（TLS 在非 x86 上回退到 `TlsGetValue`），并新增
自研 AArch64 解码器 `src/disasm/hx_disasm_arm64.c` 驱动 `src/arch/arm64` 的 relocator/reader。
Windows on ARM 仅 ARM64，故 Windows 的 32 位 ARM 不适用。Linux backend
（`src/backend/posix` + `src/backend/linux`）走 RWX 路径，页保护/near 分配基于 `/proc/self/maps`，
TLS 用 pthread、线程枚举/挂起用 `/proc` + `tgkill`；同一份 arch-agnostic backend 覆盖全部四种架构。
x86_64 直接构建，x86（32 位）加 `-DCMAKE_C_FLAGS="-m32"`（需 `gcc-multilib`），ARM64 用原生
`ubuntu-24.04-arm` runner，ARM（32 位）交叉编译 `gcc-arm-linux-gnueabihf` 并在 `qemu-arm` 下跑测试。
ARM 走自研 A32+Thumb 解码器 `src/disasm/hx_disasm_arm.c` 驱动 `src/arch/arm`。**macOS** 复用 x86/
arm64 解码器与 arch、POSIX 分配器 + pthread TLS，新增 `src/backend/darwin`（mach VM 查询/枚举、
dyld 模块枚举）；关键点是 patch 代码签名过的 `__TEXT` 页需用 `mach_vm_protect` 加 `VM_PROT_COPY`
（生成可写私有副本，绕过 W^X）。x86_64 在 `macos-15-intel`、ARM64 在原生 Apple Silicon `macos-15`
CI 上验证，interceptor 行为套件全绿。ARM64 上还接入了自研 Darwin code-segment（`hooxcodesegment-darwin.c`，
新内核默认走 mprotect+COPY 路径）。其它 OS 还需各自的 backend。

> **⚠️ Apple Silicon 已知限制(自宿主):** 在 Apple Silicon(16 KiB 页 + 强制 W^X)上,patch 一页代码时
> 该页会短暂失去执行权限。若被 hook 的函数与 hoox **自身的 patch 代码**恰好落在同一个 16 KiB 页,补丁过程
> 会崩溃。这只在"把 hoox 静态链接进目标、并 hook 同一二进制内、且恰好同页的函数"时发生;hook 其它模块/库
> 不受影响。hoox 会在 attach/replace 时**检测到这种碰撞并返回错误(`HOOX_ATTACH_POLICY_VIOLATION` /
> 对应的 replace 错误码),而不是崩溃**。彻底消除需要"经独立映射打补丁"机制(计划中)。

## 文档

- **[API 参考（中文）](docs/API.md)** —— 每个公共函数、类型、选项，附线程安全说明。
  （English: [API_en.md](docs/API_en.md)）
- **[example/](example)** —— 覆盖整套 API 的可运行示例。
- 设计与任务：[`docs/PLAN.md`](docs/PLAN.md)、[`docs/TASKS.md`](docs/TASKS.md)。

## 构建

```
cmake -B build -G Ninja        # 或 -DCMAKE_C_COMPILER=clang / gcc；MSVC 用 VS 生成器
cmake --build build
ctest --test-dir build --output-on-failure
```

默认构建面向主机架构（x64）。要做 32 位（x86）构建，目标设为 i686 并把 `LIB`
指向 32 位的 MSVC/SDK 库目录：

```
export LIB="<VC>/lib/x86;<SDK>/ucrt/x86;<SDK>/um/x86"
cmake -B build32 -G Ninja -DCMAKE_C_COMPILER=clang \
  -DCMAKE_C_FLAGS="--target=i686-pc-windows-msvc" -DHOOX_TARGET_ARCH=x86
cmake --build build32 && ctest --test-dir build32 --output-on-failure
```

Windows x64 与 x86 均在三套编译器上通过完整测试套件（interceptor、
writer/relocator、解码器差分对拍、amalgamation）。

## 许可证

wxWindows Library Licence, Version 3.1（与 frida-gum 相同）。见
[`COPYING`](COPYING) 与 [`NOTICE`](NOTICE)。
