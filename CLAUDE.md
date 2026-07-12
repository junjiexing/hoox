# CLAUDE.md

本文件为 Claude Code 在本仓库工作时提供指导。

## hoox 是什么

从 **frida-gum** 精简提取、**只保留 inline hook（Interceptor）**能力的纯 C 库。
目标：**无 GLib 依赖、无 capstone 依赖、跨平台、可像 SQLite 一样合并为单一 `hoox.c`/`hoox.h`**，
以 frida 相同协议（**wxWindows Library Licence 3.1**）开源。

- 完整规划：**`docs/PLAN.md`**（目标/调研结论/架构决策/目录结构/里程碑/依赖处置/风险）。
- 任务拆分：**`docs/TASKS.md`**（M0–M10，每个任务含输入/产出/验收，标注依赖）。
- 本文件是它们的高频要点摘要 + 易错点清单。

## 当前状态

**Windows 垂直切片已打通（x86 与 x64，实测全绿）：** 提取 → 编译 → hook 生效 →
测试全绿 → 单文件合并 (`hoox.c`/`hoox.h`) → example 闭环。
构建在 **MSVC / clang / gcc** 三套工具链下全部通过。

**Windows ARM64 已打通（原生 `windows-11-arm` CI，MSVC，实测全绿）：** 复用同一份
`src/backend/windows`（TLS 在非 x86 上回退 `TlsGetValue`，并补上 `hoox_process_get_native_os`），
新增自研 AArch64 解码器 `src/disasm/hx_disasm_arm64.c` 驱动 `src/arch/arm64` 的
relocator/reader/writer 与 `src/backend/arm64` 的 interceptor/cpucontext（frida 的非-Darwin 路径）。
解码器只精确解码 relocator 关心的 PC-relative/分支/控制指令 + STP/MOV，其余指令原样拷贝但仍
保守上报 GP 寄存器以保证 scratch 选择安全；因是定长指令、解码与架构无关，可用
`clang --target=aarch64-pc-windows-msvc -Wshorten-64-to-32` 在 x64 主机上本地校验（连同 golden
向量测试 `tests/disasm/test_arm64_decode.c`）。**Windows on ARM 仅 ARM64，32 位 ARM 不适用。**

**Linux x86 与 x86_64 已打通（gcc，实测全绿）：** backend 位于 `src/backend/posix/`
（`hooxmemory-posix.c`/`hooxtls-posix.c`）与 `src/backend/linux/`
（`hooxmemory-linux.c`/`hoox_process-linux.c`/`hooxlinux-priv.h`）。Linux 走 RWX 路径，
线程挂起/枚举为链接项（`tgkill`/`/proc/self/task`），页保护查询与 near-alloc 走 `/proc/self/maps`，
TLS 用 pthread key。同一份 arch-agnostic backend 覆盖全部四种架构：x86_64 直接构建；x86（32 位）加
`-DCMAKE_C_FLAGS="-m32"`（需 `gcc-multilib`）；**ARM64** 用原生 `ubuntu-24.04-arm` CI；**ARM（32 位，
A32 + Thumb）** 交叉编译 `gcc-arm-linux-gnueabihf` 后在 `qemu-arm` 下跑完整测试套件。ARM 新增自研
A32+Thumb 解码器 `src/disasm/hx_disasm_arm.c`（配 `backend/arm/hooxcpu-arm.c` 用 `getauxval` 查 VFP/
interwork 特性）驱动 `src/arch/arm`。测试套件 + amalgamation 在所有已支持组合均全绿。

**macOS x86_64 与 ARM64 均已打通（原生 `macos-15-intel` / Apple Silicon `macos-15` CI，AppleClang，
interceptor 行为套件实测全绿）：** 复用 x86/arm64 解码器与 arch、`backend/posix`（mmap + pthread TLS），
新增 `backend/darwin`（`hooxmemory-darwin.c` 用 `mach_vm_region` 查页保护，`hoox_process-darwin.c` 走
mach 线程 + dyld 模块枚举）。**关键坑：patch 代码签名的 `__TEXT` 页必须用 `mach_vm_protect` 且在需要
WRITE 时加 `VM_PROT_COPY`**（生成可写私有副本，绕过 W^X；`mprotect(2)` 会被拒）。x86_64 与 arm64
都走这条路径。**Apple Silicon 额外注意：16 KiB 页**——单个目标页可能横跨 hoox 自身代码，故不能把可执行页
mprotect 成 RW（会掉 X）；vm_remap 可写别名也不行（签名 `__TEXT` 的 max_prot 为 RX，需 frida 未提取的
page-plan-builder/调试器映射）。因此 `test_memory`（裸 RWX）与 `interceptor_smoke`（自宿主、目标与 hoox
代码同页）在 Apple Silicon 上跳过——完整 interceptor 套件（目标在独立页）已充分验证。**运行时护栏**：
`hoox_interceptor_instrument` 里 `hoox_interceptor_target_unsafe_to_patch()`（仅 `HAVE_DARWIN && HAVE_ARM64`
编译)检测"目标页是否与 hoox 自身 patch 代码同页",若是则 attach/replace 返回 `POLICY_VIOLATION` 而非崩溃。
彻底解决需"经独立映射打补丁"（page-plan / MAP_JIT stub），留待在 Apple Silicon 设备上开发。`arch/arm64` interceptor
的 DarwinGrafter（Mach-O import 挂钩，hoox 不提供）用 `HOOX_HAVE_DARWIN_GRAFTER`（永不定义）门控掉。
另提供 `hooxcodesegment-darwin.c`（老内核用；新内核 `is_supported` 返回 FALSE）。

**FreeBSD x86 与 x86_64 已打通（amd64 FreeBSD VM，clang，实测全绿）：** backend 位于
`src/backend/freebsd/`（`hooxmemory-freebsd.c`/`hoox_process-freebsd.c`），复用 `backend/posix`
（mmap + pthread TLS）。走 RWX 路径（`mprotect`，**无需** `VM_PROT_COPY`——不同于 macOS）；页保护查询
与 near-alloc 走 `sysctl {CTL_KERN,KERN_PROC,KERN_PROC_VMMAP,pid}`（迭代变长 `struct kinfo_vmentry`，
按 `kve_structsize` 步进；**非 `/proc`**），线程 id 用 `pthread_getthreadid_np`（`<pthread_np.h>`）、
挂起/枚举用 `thr_kill` + `sysctl KERN_PROC`，模块枚举用 `dl_iterate_phdr`（FreeBSD 用原生
`Elf_Addr/Elf_Half/Elf_Phdr`，**非 glibc 的 `ElfW()`**）。CMake 以 `CMAKE_SYSTEM_NAME STREQUAL
"FreeBSD"` 分派 OS backend（`HAVE_FREEBSD` 由 `__FreeBSD__` 自动推导），以 `HOOX_ARCH_FAMILY` 分派 arch。
CI 无托管 FreeBSD runner，故用 `vmactions/freebsd-vm` 在 ubuntu 宿主上启动 amd64 FreeBSD VM：x86_64 原生
跑完整套件，x86（32 位）用 `clang -m32`（freebsd32/lib32）同样跑完整套件。**ARM64 FreeBSD 已实测**：
用 `cross-platform-actions/action@v1.3.0` 在 ubuntu 宿主上启动真实的 **FreeBSD/arm64 15.1 客户机**
（QEMU 模拟 aarch64，其官方 CI 亦是在 x86-64 宿主上模拟 arm64 客户机；`run:` 输入已 deprecated 但 v1.3.0
仍可用），客户机内原生编译并跑完整套件（interceptor 行为套件 + amalgam 全绿，~3 分钟）。ARM（32 位）
FreeBSD 共用同一 backend + 在 Linux `qemu-arm` 验证过的 A32/Thumb arch 层（FreeBSD armv7 tier-2、无现成
CI 镜像），按构造覆盖、未在 CI 执行。
**关键坑（FreeBSD 必链 libthr）：** hoox 的 TLS 用 pthread key；FreeBSD 的 pthread 在 `libthr`，
消费方（含 amalgam）**必须链接 `-pthread`**，否则 `pthread_*` 会解析到 libc 的**空存根**（静默无操作）——
`pthread_getspecific` 恒返回 NULL，导致 interceptor 线程上下文每次调用都新建，enter/leave 拿到不同调用栈
→ on-leave 空栈崩溃。glibc 已把 pthread 并入 libc，Linux 无此问题；`test_amalgam`（直接编译 `hoox.c`、
不链接 `hoox` 目标）因此在 `tests/CMakeLists.txt` 显式链接 `Threads::Threads`。

下一步：iOS/Android/其它平台。

> 命名已完成一次性重构：**所有 `gum`/`cs`/glib 前缀均已清除**（见 D2）。仓库不再以
> 与上游 diff 对拍为目标——以行为一致 + 测试通过为准。

## 核心架构决策（已确认）

| # | 决策 | 要点 |
|---|---|---|
| D1 | GLib → **自研 nano-glib `hx_*`** | 仓库内纯 C 重写容器/原子/锁/类型；符号统一 `hx_` 前缀（`hx_pointer`/`hx_uint`/`HxArray`/…） |
| D2 | **全量 hoox/hx 命名** | 库 API + 引擎 = `hoox_`/`Hoox`/`HOOX_`（长前缀）；内部工具层（nano-glib + 反汇编）= `hx_`/`Hx`/`HX_`（短前缀）。**不再保留任何 `gum_`/`cs_` 内部名。** |
| D3 | **MSVC / clang / gcc 全支持，语言标准 C99** | 唯一 MSVC 拦路虎是 `__builtin_alloca`（`hx_alloca` 已按编译器分派 `_alloca`/`__builtin_alloca`）。原子层已有 `_Interlocked*` 分支。 |
| D4 | 反汇编 → **自研紧凑解码器 `hx_disasm`**（参考 Detours，弃用 capstone） | 提供 capstone 兼容子集的 C 重写（`src/disasm/hx_disasm.h`，枚举 `HX_INS_*`/`HX_REG_*`、类型 `hx_insn`/`hx_x86` 等）。**按架构分文件**：x86 = `hx_disasm_x86.c`；arm64 = `hx_disasm_arm64.c`（只解码 hook relocator 所需子集）。两者提供同一套 `hx_open`/`hx_disasm_iter`/… API，按 `HOOX_ARCH_FAMILY` 择一编译 |

## 提取基线（provenance）

- frida-gum commit `a2ebd7b8f570a0aa82ef6823ffa0f7d39703ffa4`（tag `17.15.4`）
- capstone fork rev `d536b1577fd033a31d75f48fd183aa425256cc18`（仅历史参考，不分发）

## 目录约定

```
include/hoox.h        手写的干净公共头（仅 hoox_* API + 必需类型）——amalgam 的 out-h 来源
src/compat/           nano-glib：hx{defs,mem,atomic,thread,array,hash,list,string,strfuncs,glib,messages}
src/disasm/           自研解码器：hx_disasm.h(含 hx_x86_insn 枚举) + hx_disasm_x86.c
src/core/             hook 引擎（hoox_* 命名）；hoox.h 为内部 umbrella（amalgam 的 out-c 来源）
src/arch/<arch>/      hooxx86writer/relocator/reader（relocator/reader 使用 hx_disasm）
src/backend/<os|x86>/ hooxmemory-<os>/hooxtls-<os>/hooxinterceptor-<arch>/hoox_process-<os>
tests/harness/        无 GLib 测试骨架（TESTLIST/TESTENTRY/TESTCASE + assert）
tests/core/           移植自 frida 的 interceptor/writer/relocator 测试
tools/amalgamate.py   合并为单一 hoox.c/hoox.h（--header=内部 umbrella；--public-header=干净公共头）
example/              使用 amalgam 单文件库的完整示例
_refs/                参考源（frida、capstone、Detours）——见下方铁律
```

## 铁律 / 约束

1. **`_refs/` 只能参考或提取代码，绝不 `#include`/链接/直接引用进项目**（已被 `.gitignore` 忽略）。
   缺失的上游源码可 clone 到 `_refs/`；**找不到源码就停下并提示用户**。
2. **纯 C（C99）**，构建系统统一 **CMake**。目标编译器：**MSVC / clang / gcc（较新版本）**。
   允许 gnu99 级扩展（原子用语句表达式 + `__typeof__`，`hx_alloca`），MSVC 走各自 intrinsic 分支。
3. **无第三方运行时依赖**：GLib→nano-glib，capstone→hx_disasm，dlmalloc→系统 malloc。
   `threads`/psapi/Toolhelp 等是 OS 原语/系统库，直接用，不算捆绑第三方。
4. **改动以保证行为一致 + 测试通过为准**。提取阶段的“机械搬移、勿重构”约束已解除；
   可为 clean 做合理清理，但**每次改动必须编译通过 + 对应测试全绿**再提交。
5. **公共头 `include/hoox.h` 只暴露功能 API + 必需类型**，不得泄漏内部/反汇编/内存扫描类型。
6. 每个里程碑结束做一次**漂移检查**：确认未引入被明确排除的模块/依赖
   （Stalker/Backtracer/CFG/符号解析/JS 绑定/GLib/capstone 等）。

## 编译器可移植性要点

- **`hx_alloca`（`src/compat/hxdefs.h`）**：MSVC→`_alloca`（`<malloc.h>`），clang/gcc→`__builtin_alloca`。
  这是曾经唯一让 MSVC 链接失败的点（`__builtin_alloca` 非 cl 内建 + 隐式 int 声明触发 C4047）。
- **MSVC 严格构建**：顶层 CMakeLists 为 MSVC 开启 `/W3 /GF /sdl /WX`（warnings-as-errors），
  全树零告警。`/GF` 池化字面量（匹配 gcc/clang，有测试按指针相等比较字面量）；`/sdl` 曾抓出
  `hoox_memory_patch_code_pages` 的未初始化指针（C4703）。窄化转换（size_t→hx_uint 等）一律显式强转。
- **`HX_GNUC_INTERNAL`** 在 `_WIN32` 上定义为空（PE 无 ELF 可见性，gcc 会告警）。
- **原子层（`hxatomic.h`）** 已按 `__GNUC__/__clang__` vs MSVC(`_Interlocked*`) 分派。
- 测试中的 noinline 用可移植宏（`_MSC_VER`→`__declspec(noinline)`，否则 `__attribute__`）。

## 配置宏（默认零配置）

消费 amalgam（或源码）默认**无需任何 -D**：

- **链接**：默认静态（`HOOX_API` 为空）。作为 Windows DLL 消费时定义 `HOOX_SHARED`
  （构建该 DLL 时另加 `HOOX_EXPORTS`）。
- **分配器**：默认系统 `malloc`。要用 dlmalloc 定义 `HOOX_USE_DLMALLOC`（需自备 `dlmalloc.c`）。
- **架构/OS**：`hooxdefs.h` 从编译器内置宏（`_M_IX86`/`__x86_64__`/`_WIN32`/…）推导
  `HAVE_I386`/`HAVE_WINDOWS` 等；无法判断时才由用户显式定义。构建系统（CMake）仍显式传入
  作为交叉编译的权威覆盖。

## 易错点（已按源码逐行核实——违反这些会返工）

> 下列符号名已随重命名更新为 hoox_/hx_。

- **Windows patch code 走 RWX 路径，运行时不挂起线程。** `hoox_query_rwx_support()` 除
  Darwin-非i386 外都返回 `HOOX_RWX_FULL`。线程枚举/挂起（`_hoox_process_enumerate_threads`/
  `hoox_thread_suspend`）只在 Apple arm64 的非-RWX 分支触达；在 Windows 上它们是**链接项**
  （须能编译链接，运行时不执行）。**别把它当 Windows hook 关键路径。**
- **Windows 不做 code-cave 模块枚举找 trampoline。** `hooxcodeallocator.c` 里用
  `hoox_process_enumerate_modules` 找 code cave 的那段被 `#if HAVE_DARWIN || (HAVE_ELF && 32位)`
  包住，Windows 不编译；Windows near 分配走 `hoox_memory_allocate_near` 的 `VirtualAlloc` 双向探测。
- **仍需最小 `HooxModule`/range shim**：`hooxprocess.h` 无条件 `#include <hooxmodule.h>`，
  `HooxFoundModuleFunc` 传 `HooxModule *`，测试用 `hoox_process_get_main_module`/`hoox_module_get_range`。
- **反汇编泄漏点已全部改用 `hx_disasm` 类型，公共头 `include/hoox.h` 不含任何反汇编类型。**
  内部 `detect_hook_size(..., hx_csh, hx_insn*)` 仍在实现里，但已从公共 API 移除。
- **`hooxmemory` 的 scan/pattern API**：`HooxMatchPattern` 曾内嵌 `GRegex *`，务必确保不把
  GRegex/GThreadPool 依赖带回（当前公共头已不暴露 scan/pattern；如后续彻底删实现需连 header 一起删）。
- **hx_disasm 的 x86 RIP 重写依赖寄存器读写语义**（`hx_reg_read/write`）选 scratch 寄存器，
  错了会生成污染寄存器的 trampoline。须对 MOV/CMPXCHG/PUSH/RIP-relative 对拍。

## 明确不需要（直接丢弃）

Stalker、Backtracer、ControlFlowGraph、DarwinGrafter、符号/模块解析（apiresolver/
elfmodule/darwinmodule/完整 module）、`hooxexceptor`（异常/守护页 hook）、kernel、
profiler/sampler、dbghelp/symbolutil、所有 JS/C++ 绑定（gumjs/gumpp/QuickJS/V8）、
dlmalloc、以及全部非目标架构的 writer/relocator。

## 代码风格

- **2 空格缩进**，无 tab；**80 列**上限。
- 函数调用括号前留空格：`func (arg)`；强转后留空格：`(HooxAddress) ptr`。
- 指针声明两侧留空格：`Type * name`。
- `{` 单独成行（`if`/`for`/函数签名之后另起一行）；`{` 后与 `}` 前无空行；无连续空行。
- 命名：库 API + 引擎用 `Hoox`/`hoox_`/`HOOX_`（类型 `HooxInterceptor`、函数 `hoox_interceptor_attach`、
  文件 `hooxinterceptor.{h,c}`）；内部工具层（nano-glib、反汇编）用 `Hx`/`hx_`/`HX_`。
  对外导出标 `HOOX_API`。

## 许可证

- 主许可证 **wxWindows Library Licence 3.1**（同 frida-gum），保留所有原始版权头与 `COPYING`。
- `hx_disasm` 中**派生自 Detours 的部分**须在文件头与 `NOTICE` 保留 Detours 的 **MIT** 声明。
- capstone/Detours 仅在 `_refs/`（不分发），不构成分发义务。
- amalgamation 产物头部内联汇总所有许可证声明。

## 平台/架构（最终目标，分阶段）

Windows / Linux / Android / macOS / iOS / tvOS / FreeBSD × x86 / x86_64 / ARM / ARM64。
W^X/代码签名差异隔离在 backend 层：Windows/Linux/非越狱走 RWX；Apple 平台单独实现
`hooxcodesegment-darwin`。详见 `docs/PLAN.md` §6。（**MIPS、QNX 已从目标中移除**。）
