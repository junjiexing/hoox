# hoox

**中文** · [English](README_en.md) — 完整 API 参考：[API.md](docs/API.md)

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
  x86 / x86_64 / ARM / ARM64 / MIPS（分阶段覆盖，与 Frida 对齐）。
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
example。**Linux x86 与 x86_64 亦已打通**（gcc，全测试套件 + amalgamation 全绿）。下一步向
其它平台/架构横向铺开。

## 平台支持

图例：✅ 已支持（可构建并通过完整测试） · 🧩 源码已提取（在库中但尚未编译/验证） ·
📋 规划中（尚未开始）

| OS ＼ 架构 | x86 | x86_64 | ARM | ARM64 | MIPS |
|---|:-:|:-:|:-:|:-:|:-:|
| **Windows** | ✅ | ✅ | 🧩 | 🧩 | 📋 |
| **Linux** | ✅ | ✅ | 📋 | 📋 | 📋 |
| **Android** | 📋 | 📋 | 📋 | 📋 | 📋 |
| **macOS / iOS / tvOS** | 📋 | 📋 | 📋 | 📋 | 📋 |
| **FreeBSD / QNX** | 📋 | 📋 | 📋 | 📋 | 📋 |

可直接使用的组合是 **Windows × (x86 / x86_64)**（三套编译器全绿）与 **Linux × (x86 / x86_64)**
（gcc 全绿）。Linux backend（`src/backend/posix` + `src/backend/linux`）走 RWX 路径，页保护/near
分配基于 `/proc/self/maps`，TLS 用 pthread、线程枚举/挂起用 `/proc` + `tgkill`；同一份 backend
覆盖 x86 与 x86_64（32 位构建加 `-DCMAKE_C_FLAGS="-m32"`，需 `gcc-multilib`）。ARM/ARM64 的架构与
backend 源码已从 frida-gum 提取进仓库（🧩），但尚未接入构建；其它 OS 还需各自的 backend。MIPS 在
frida 本身即为部分/实验性支持。

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
