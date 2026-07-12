# hoox 开发规划（Plan）

> 一个从 frida-gum 精简提取、只保留 **inline hook** 能力的纯 C 库。
> 目标：无 GLib 依赖、可裁剪、跨平台、可像 SQLite 一样合并为单一 `.c/.h`。

本文档是总体规划与架构决策。可执行的任务拆分见 [`TASKS.md`](./TASKS.md)。

> **进度更新（Windows 切片完成后）**：以下决策中关于命名与工具链的部分已被取代，
> 以本 note 为准：
> - **命名**：全量重构为 `hoox_`/`Hoox`/`HOOX_`（库 API + 引擎）与 `hx_`/`Hx`/`HX_`
>   （nano-glib + 反汇编）。**代码中不再保留任何 `gum_`/`cs_`/`g_` 前缀**，也不再以与上游
>   diff 对拍为目标（`frida-gum` 仅作 provenance 出现在注释/NOTICE）。因此**没有独立的
>   facade 层**——引擎本身即 `hoox_*`；`include/hoox.h` 是手写的干净公共头（仅 API + 必需类型）。
> - **语言标准**：**C99**（非 C11）。原子层用 `__atomic`/`_Interlocked*` 内建，非 `<stdatomic.h>`。
> - **工具链**：**MSVC / clang / gcc 均支持**（非 clang-only）。`hx_alloca` 按编译器分派
>   `_alloca`/`__builtin_alloca`；MSVC 开 `/GF`。

---

## 1. 项目目标（源自需求）

1. **只保留 hook 必需功能**，其余（Stalker 动态追踪、Backtracer、CFG、
   符号解析、模块枚举、内存扫描、DarwinGrafter、异常/守护页 hook 等）全部去掉。
2. **保留 Frida inline hook 的核心平台/架构支持**：OS（Windows / Linux / Android /
   macOS / iOS / tvOS / FreeBSD）× 架构（x86 / x86_64 / ARM / ARM64），
   **按 §6 矩阵分阶段覆盖**（MIPS、QNX 不在支持目标内）。并**保留 frida 中 hook 相关的测试**用于验证。
3. **尽可能移除第三方库**，尤其是 GLib —— 用仓库内自研的极简纯 C 兼容层
   （下称 **nano-glib / `hx_*`**）替代。无法移除的（如反汇编器
   **capstone**）则下载源码、提取并裁剪到最小。
   （注：经调研，capstone 抽取不可行，最终改为自研紧凑解码器替代 —— 见 §2.3 / 决策 D4。）
4. **纯 C 开发**，语言标准 **C99**（gnu99 级扩展按需；见顶部进度 note）。
   提供合并脚本，输出单一 `hoox.c` + `hoox.h`（amalgamation）。
5. `_refs/` 只用于**参考/提取**代码，**不得** include / 链接 / 直接引用进项目。
6. frida 引用但 `_refs/` 下缺失的项目可直接 clone 到 `_refs/`；找不到源码时提示用户。
7. **构建系统统一用 CMake**。
8. 全程 **AI Vibe Coding** 自动开发，按里程碑/任务推进。
9. 继续以 frida 相同的开源协议开源：**wxWindows Library Licence, Version 3.1**。

---

## 2. 关键调研结论（来自对 `_refs/frida/subprojects/frida-gum` 的分析）

### 2.1 Hook 最小依赖链（可提取的核心）
inline hook 引擎（Interceptor）需要且**仅**需要以下模块：

| 类别 | 文件（frida-gum 路径） | 作用 |
|---|---|---|
| Interceptor 前端 | `gum/guminterceptor.c` `.h` `guminterceptor-priv.h` | hook 表、调用栈、事务 |
| Interceptor 架构后端 | `gum/backend-x86/guminterceptor-x86.c`（arm/arm64 同构） | 生成/激活 trampoline |
| Listener 契约 | `gum/guminvocationlistener.c` `guminvocationcontext.c` | enter/leave 回调 + 上下文 |
| Trampoline 内存 | `gum/gumcodeallocator.c` | W^X 安全的可执行代码片 |
| 代码生成 | `gum/arch-x86/gumx86writer.c` `gumx86relocator.c` `gumx86reader.c` | 写 trampoline + 重定位被覆盖的序言 |
| 内存 | `gum/gummemory.c` + `backend-<os>/gummemory-<os>.c` | 页保护、patch code、分配 |
| 代码签名 | `gum/gumcodesegment.c`（非 Apple 为 stub） | 仅 Apple 平台需真实实现 |
| 并发 | `gum/gumspinlock.c` + `backend-<os>/gumtls-<os>.c` | 自旋锁、TLS 调用栈 |
| libc | `gum/gumlibc.c` | trampoline 内不可调用 libc 的 memcpy/memset |
| GLib-free 容器 | `gum/gummetalarray.c` `gummetalhash.c` | 代码生成器用（label 表） |
| 进程服务（分层，见下） | `gum/gumprocess.c` 的部分函数 + 最小模块抽象 | 线程 id、system-error 保存/恢复、代码签名策略；最小 GumModule/range shim；线程枚举/挂起（链接项） |

**结论**：hook 引擎本体是小而自洽的一等 C 代码。

> ⚠️ **进程/模块服务需分层，勿把非首切片路径误当 Windows x64 必需（已按源码逐行核实）**：
> - **线程挂起**：`gum_memory_patch_code` 只在 `rwx_supported == FALSE` 分支枚举+挂起线程
>   （`gummemory.c:605`）。而 `gum_query_rwx_support()` 除 **Darwin-非i386（iOS/macOS-arm64）**外
>   一律 `GUM_RWX_FULL`（`gummemory.c:332`）——**Windows x64 走 RWX 路径，运行时不挂起线程**。
>   但该 `else` 分支无 `#ifdef` 包裹，故 `_gum_process_enumerate_threads`/`gum_thread_suspend/resume`
>   仍是**链接项**（须能编译链接，首切片运行时不触达）。挂起真正生效在 Apple arm64（M8）。
> - **code-cave 模块枚举**：`gumcodeallocator.c` 里用 `gum_process_enumerate_modules` +
>   `gum_module_get_range` 找 code cave 的 `gum_code_deflector_dispatcher_new` 被
>   `#if defined(HAVE_DARWIN) || (defined(HAVE_ELF) && 32-bit)` 包住（`gumcodeallocator.c:571`）——
>   **Windows x64 不编译此段**。Windows 的 near 分配走 `gum_memory_allocate_near` 的
>   `VirtualAlloc` 双向探测（`backend-windows/gummemory-windows.c:248`），**无需模块枚举**。
> - **但仍需最小模块抽象**：`gumprocess.h` 无条件 `#include <gummodule.h>`，`GumFoundModuleFunc`
>   传 `GumModule *`（`gumprocess.h:14/95`），且测试工具用 `gum_process_get_main_module()` +
>   `gum_module_get_range()`（`tests/testutil.c:181`）。故**不提取完整 GObject `GumModule`，但要定义
>   最小 `GumModule`/range shim**，供类型面、process/heap 测试、及后续 Darwin/ELF32 使用。
>
> 小结：Windows x64 首切片 hook 路径**不**在运行时挂起线程、**不**枚举模块找 code cave；
> psapi/Toolhelp 归为"链接项 + 测试/后续所需"，非 hook 关键路径。

**提取基线**：frida-gum commit `a2ebd7b8f570a0aa82ef6823ffa0f7d39703ffa4`（tag `17.15.4`）；
capstone fork rev `d536b1577fd033a31d75f48fd183aa425256cc18`。后续对齐上游以此为 diff 基点。

### 2.2 明确**不需要**、直接丢弃的部分
`gumstalker*`、`gumbacktracer*`、`gumcontrolflowgraph.c`、`gumdarwingrafter*`、
`gumdarwinmodule*`、`gumelfmodule*`、`gummodule*`（**但保留最小 `GumModule`/range shim**，
因 `gumprocess.h` 类型面与测试工具 `gum_process_get_main_module`/`gum_module_get_range` 引用它）、
`gum*apiresolver*`、
`gumswiftapiresolver.c`、`gumobjcapiresolver`、`gummemoryaccessmonitor*`、
`gumkernel*`、`gummemorymap.c`、`gumexceptor*`（异常/守护页 hook 属另一套机制）、
`gumprofiler/gumsampler/gumallocatorprobe`、`gumdbghelp*`、`gumsymbolutil*`、
所有 bindings（gumpp / gumjs / QuickJS / V8）、以及全部非目标架构的 writer/relocator。

### 2.3 反汇编：不用 capstone，自研紧凑解码器（决策 D4）
重定位（把被 trampoline 覆盖的原始指令搬移并修正 PC 相对操作数）**必须**能解析
指令，但**不需要**通用语义反汇编器。经对 capstone 与 Microsoft Detours 的对比调研：

**结论一：从 capstone 抽取“精简功能”不可行（基本 all-or-nothing）。**
- capstone 的**操作数详情不是解码器产出的，而是 Intel 打印器（AsmWriter 表）副作用
  填充的** —— 想要 operands 就得保留打印器 + ~1MB AsmWriter 表 + ~5MB 解码表 +
  ~3.6MB 映射表。
- 唯一可行的仅是配置级裁剪：X86-only + Intel-only + 关 AT&T ≈ 源码 10.5MB →
  二进制约 **2–3.5MB**（每架构都这么大）。`reduce` 模式砍 60% 但丢 SSE/AVX/FPU
  解码，对通用 hook **不安全**（真实序言常含 `movaps/movdqa/AVX`）。
- 手工抽取“长度+分类+可重定位操作数”子集不现实：解码表 TableGen 生成、密集交叉引用。

**结论二：frida relocator 需要的解码信息是一个有界小集合。**
- 每条指令只读 `length / id(助记符分类) / address / bytes`；**完全不用 capstone 的
  group API**（靠 `id` 的 switch 分类）。
- 需要“重写”的指令是**有界集**：arm64≈8、thumb≈11、arm≈8、x86 分支族
  (CALL/JMP/Jcc/JECXZ/RET) + 一个通用 RIP 相对修复。**其余 95% 指令只需正确长度 +
  “无需重定位”判断，原样复制。**
- 唯一硬约束：x86 RIP 修复需 `modrm 字节 / modrm_offset / disp_offset / disp` 等
  **编码级**信息，以及“寄存器读写”判断（选 scratch 寄存器）—— 而这些正是长度解码器
  解析 ModRM/disp 时**顺带就有**的信息。

**结论三：Detours 的反汇编引擎正是为此定制，可作重写蓝本。**
- Detours 本身就是 trampoline inline hook 库，其 `disasm.cpp` 的 `DetourCopyInstruction`
  就是“把序言重定位到 trampoline 并修正 PC/RIP 操作数”。
- 覆盖 x86/x64/ARM(Thumb-2)/ARM64（**无 MIPS**）；x86/x64 核心 ~1520 行、表驱动
  （256 项 opcode 表 + ModRM 表）、覆盖 VEX/EVEX/APX 等现代扩展；可移植到 C。
- **MIT 许可**，与 wxWindows 3.1 兼容。

**决策 D4：自研紧凑解码器 `src/disasm/`**（参考 Detours，产出 frida relocator 需要的
接口），frida 的 relocator/writer **基本不动**。足迹从 MB 级降到 **KB 级**、无 capstone
依赖。frida 的 `x86relocator`/`arm64relocator` 等测试**作为差分/正确性验证网**。

**解码器输出接口（每条指令，替换 capstone 的 `cs_insn`/detail 子集）：**
1. `length`（字节数）；`address`、原始 `bytes`。
2. `control_flow_class` / 助记符 id：区分 顺序 / 条件分支 / 无条件分支 / call / ret /
   间接分支(reg|mem) —— 对应各 arch 的 EOB/EOI 与 rewrite id 集合。
3. 仅对可重定位指令：小操作数描述 `{kind:IMM|REG|MEM, value/reg, mem.base/index/disp,
   shift, subtracted, cc}`（operand 0..2）+ `op_count`。
4. **x86 专用**：`modrm` 值 + `modrm_offset` + `disp_offset` + `disp`，及“读/写某 GP 寄存器”谓词。

**风险/成本**：需给 Detours 式解码器补助记符分类（分支 opcode 为已知固定集）；ARM/ARM64
少量语义位（`cc/shift/subtracted/writeback`）从定长编码位域直接解出（参考 Detours 做法）；
每架构须过 frida relocator 测试方算通过。

- **capstone 与 Detours 均保留在 `_refs/` 仅作参考/差分验证**（capstone fork rev
  `d536b1577fd033a31d75f48fd183aa425256cc18`；Detours 已 clone）—— 不 include/不链接。

### 2.4 GLib 耦合度评估：MEDIUM–LARGE（决定采用兼容层策略）
- **GObject 用得很浅**：无 signal、无 property、无深继承。
  - `GumInvocationListener` = 2 函数 vtable 的 GObject interface；
  - `GumInterceptor` = 单例 + 引用计数 + dispose/finalize。
  - → 可用普通 struct + 函数指针 + 显式 ref/unref 平替。
- **容器耦合重**：核心 `guminterceptor.c` 大量使用
  `GHashTable`/`GArray`/`GPtrArray`/`GQueue`；`gumcodeallocator.c` 用
  `GHashTable`/`GSList`/侵入式 `GList`。
- **热路径原子 + 事务引擎是最微妙处**：
  - `listener_entries` 用 `g_atomic_pointer_*` 做无锁 COW；
  - `begin/end_transaction` 用 `GQueue` + 按页分组的 `GHashTable<GArray>`
    批量翻转页保护、原子激活 trampoline。移植需保证内存序与旧数组安全回收。
- **基础类型遍布**：`gint/guint/gpointer/gsize/gboolean/GDestroyNotify` 等来自
  `<glib.h>`；只有 `GUM_API` 与 `Gum*` 类型是 gum 自有。
- `gummetalhash/gummetalarray` 本就是 GLib-free 的替代实现（代码生成器已在用），
  与 nano-glib 思路天然契合，**保留复用**。

### 2.5 测试体系
- 框架为 **GLib GTest**，外面包了 `TESTLIST/TESTENTRY/TESTCASE` 宏（`testutil.h`）；
  fixture `.c` 被测试 `.c` 直接 `#include`（不单独编译）。
- `tests/core/interceptor.c` 约 **45** 个用例：clobber/attach/detach/replace/
  fast-replace/listener/thread/invocation-context/custom-redirect/relocation。
- 依赖的代码生成器测试：`arch-x86/x86writer|x86relocator`、
  `arch-arm/*writer|*relocator`、`arch-arm64/*`。
- 基础设施：`gumtest.c`、`testutil.{c,h}`、`lowlevelhelpers.{c,h}`、
  `tests/core/targetfunctions/`（被 hook 的“靶函数”共享库，`GUM_NOINLINE`）、
  `tests/data/`（各平台预编译靶 `.so/.dylib`；Windows 下直接编进测试二进制）。
- **移除 GLib ⇒ 必须重写测试骨架**（`TESTLIST/TESTENTRY/TESTCASE` +
  `g_assert_*`）与 listener 辅助类（GObject → C 回调），并提供 `GString` 平替。

### 2.6 第三方依赖全清单与处置
gum **核心库**（`gum/meson.build`）实际只声明：`gum_internal_deps = [glib, gobject,
capstone, threads]` + **可选** `libffi`、`liblzma`。顶层 `meson.build` 里出现的其它
库（gio/json-glib/libsoup/openssl/minizip/quickjs/v8/tinycc/sqlite）都属于 **gumjs
绑定 / inspector / frida-core**，不在 gum core，更与 hook 无关。逐库处置（均已核实用途）：

| 库 | gum 里的用途（核实） | hook 相关 | 处置 |
|---|---|:--:|---|
| **glib / gobject** | 容器/对象系统/类型/原子/锁，遍布 | ✅ 核心 | D1：自研 nano-glib `hx_*` 替代 |
| **capstone** | 反汇编（重定位） | ✅ relocator | D4：自研 `hx_disasm` 替代 |
| **threads** | 互斥锁 / TLS 调用栈 | ✅ | 用 OS 原语（Win `CRITICAL_SECTION`/pthread）封进 `hx_lock`/`hx_tls`；**非捆绑第三方源码** |
| **libffi** | 仅 `gum.c` 调 `ffi_set_mem_callbacks`（让 libffi 可执行内存走 gum）；及 backtracer | ❌ | 丢弃（删 `gum.c` 该段 init；backtracer 不要） |
| **liblzma (xz)** | `gumelfmodule.c` 解压 `.gnu_debugdata`；stalker 测试语料 | ❌ | 丢弃（elfmodule/stalker 都不要；已确认 `x86relocator` 测试不用） |
| **libunwind** | backtracer 后端 | ❌ | 丢弃 |
| **libdwarf** | symbolutil 后端 | ❌ | 丢弃 |
| **dbghelp**(Win) | 符号/backtrace 后端 | ❌ | 丢弃 |
| **psapi**(Win) | 模块枚举（+ 范围） | ⚠️ 测试/后续 | 首切片 hook 路径**不**用它找 code cave（那是 Darwin/ELF32，见 §2.1 ⚠️）；仅测试工具与最小模块 shim 需要；`EnumProcessModules`/`GetModuleInformation`，系统 DLL 直接链接 |
| **系统线程 API**(Win) | 线程枚举 + 挂起/恢复 | ⚠️ 链接项 | Windows x64 走 RWX 路径**运行时不挂起线程**（见 §2.1 ⚠️），但符号须能链接；`CreateToolhelp32Snapshot`/`SuspendThread`/`ResumeThread`，系统 API，非捆绑源码 |
| gio / json-glib / libsoup / glib-networking / openssl / minizip-ng / quickjs / v8 / tinycc / sqlite | 网络/序列化/JS 运行时/DB —— gumjs 绑定与 frida-core | ❌ | 丢弃（本就不在 gum core） |

**结论：除 glib 与 capstone 外，hook 路径不需要任何其它第三方库。** `libffi`/`liblzma`
虽被 gum core 可选链接，但只服务于被丢弃的模块（backtracer/elfmodule/stalker 测试）；
`threads` 与 Windows 系统库属 OS 原语，不属"需下载源码提取"的第三方依赖。因此需求 3
中"下载源码提取"这一动作，最终**只落在 capstone**（而 capstone 又经 D4 改为自研解码器
替代）——即 hoox 最终对第三方**运行时零捆绑**（除自研代码派生自 Detours 的 MIT 声明）。

---

## 3. 架构决策（已与用户确认）

| # | 决策点 | 选择 | 影响 |
|---|---|---|---|
| D1 | GLib 处理 | **自研极简纯 C 兼容层 `hx_*`（nano-glib）** | 仓库内重写容器/原子/锁/类型；gum hook 代码结构基本不动；无外部依赖；测试可保留 |
| D2 | 公共 API 命名 | **重命名为 `hoox_*`** | 对外用 `hoox_interceptor_attach` 等；内部引擎保留 `gum_*` 以便对齐上游；用薄 facade / 别名桥接 |
| D3 | 首个垂直切片 | **Windows x64** | W^X 简单（RWX 页，`gumcodesegment` 可 stub）；靶函数编进测试二进制、无需 dlopen 改造 |
| D4 | 反汇编引擎 | **自研紧凑解码器（参考 Detours，弃用 capstone）** | 见 §2.3；足迹 KB 级、无第三方反汇编依赖；frida relocator/writer 基本不动，其测试作验证网；解码器接口需精确覆盖有界的可重定位指令集 |
| D5 | Windows 编译器 | **首选 clang（clang-cl）** | 提取的 gum 代码含 GNU 扩展（`gumdefs.h` 的 `__attribute__` 对齐/packed），frida 官方在 Windows 亦用 clang；`hx_atomic` 用 C11 `<stdatomic.h>`，MSVC 的 C11 原子支持新/受限。若必须支持 MSVC：需补 GNU-ism 垫片 + `_Interlocked*` 原子回退（列为显式任务） |

### 3.1 由决策推导的策略
- **内部符号保留 `gum_*`**，公共头 `include/hoox.h` 暴露 `hoox_*`（薄 inline wrapper /
  `#define` 别名）。好处：提取 churn 最小、便于日后 `diff` 对齐 frida 上游。
- **兼容层 `hx_*` 提供“形似 GLib”的最小 API**，使提取的 gum 代码以机械替换
  （`g_hash_table_*` → `hx_hash_table_*` 等）即可编译，降低逻辑改动与出错面。
- **`gum_internal_malloc` 重定向到系统 `malloc`**，从而**移除 `dlmalloc.c`（~228KB）**；
  代码生成器在“安装 hook 时”运行，不在 trampoline 内，用系统分配器安全。
- **`gumcloak` / `gumunwindbroker` 先做 no-op stub**（trampoline 页隐藏、异常穿越
  hook 的 unwind 非 hook 正确性必需），后续里程碑按平台需要再补真实实现。
- **W^X / 代码签名复杂度隔离在 backend 层**：Windows/Linux/非越狱平台走 RWX/
  remap-writable；Apple 平台单独实现 `gumcodesegment-darwin`。
- **反汇编改为自研 `hx_disasm`（D4）**：定义统一解码器接口（§2.3），x86/x64 参考 Detours
  的表驱动实现移植为 C；`gumx86relocator/reader` 只改“数据来源”（capstone → `hx_disasm`），
  重写与代码生成逻辑保留。每架构以 frida 对应 relocator 测试为验收闸门。
  **注意 capstone 迁移点比"relocator/reader"更广（已核实）**：
  - `backend-x86/guminterceptor-x86.c` 直接调 `cs_disasm_iter`；
  - 公共头 `guminterceptor.h` 的 `gum_interceptor_detect_hook_size(..., csh, cs_insn *)`
    直接暴露 capstone 类型；
  - **`arch-x86/gumx86writer.h` `#include <capstone.h>` 且在公共签名里用 `x86_insn` 枚举**
    （`put_jcc(x86_insn, ...)` 等），`gumx86writer.c` 内 `switch (X86_INS_*)`；
  - **`gum-init.h` 也 `#include <capstone.h>`**。
  以上都须改为 `hx_disasm` 的类型（`hx_x86_insn`/条件码枚举等），facade 层重塑
  `detect_hook_size` 签名，使公共头**不出现任何 capstone 类型**。故 **writer 也依赖
  `hx_disasm` 的枚举定义（T2.1），并非"完全不依赖 M2"**。
- **amalgamation 可行性需早验证**：gum 非为单文件合并而设计，各 `.c` 有同名 file-local
  `static` 助手与各自宏，直接拼成单 TU 易冲突。M1 阶段先做一次小 spike（合并 compat + 两个
  gum 文件跑通），据此决定策略（每文件静态前缀 / unity-include / 逐一改名）。

---

## 4. 目标目录结构

```
hoox/
├─ CMakeLists.txt              # 顶层构建（选项：目标 arch、平台、测试、amalgamation）
├─ COPYING                     # wxWindows Library Licence 3.1
├─ NOTICE                      # 归属：frida-gum(wxWindows 3.1)、hx_disasm 中派生自 Detours 的部分(MIT)
├─ include/
│  └─ hoox.h                   # 唯一公共头（hoox_* API）
├─ src/
│  ├─ compat/                  # nano-glib（纯 C，无外部依赖）
│  │  ├─ hx_types.h            # gint→int32_t、gpointer→void* … + HOOX_API
│  │  ├─ hx_macros.h           # G_BEGIN_DECLS 等平替、likely/unlikely、assert
│  │  ├─ hx_mem.{h,c}          # g_malloc/g_free/g_slice → malloc 家族
│  │  ├─ hx_atomic.h           # C11 <stdatomic.h> 封装（含指针 COW helper）
│  │  ├─ hx_lock.{h,c}         # mutex / recursive-mutex（Win CRITICAL_SECTION / pthread）
│  │  ├─ hx_array.{h,c}        # GArray / GPtrArray
│  │  ├─ hx_hash.{h,c}         # GHashTable
│  │  ├─ hx_queue.{h,c}        # GQueue
│  │  ├─ hx_slist.{h,c}        # GSList / GList（含侵入式用法）
│  │  └─ hx_string.{h,c}       # GString（主要供测试/靶函数）
│  ├─ core/                    # 提取自 gum/ 的 hook 引擎（保留 gum_* 内部名）
│  │  ├─ gumdefs.h             # 已切断 <glib.h>，改依赖 hx_types.h
│  │  ├─ guminterceptor.{c,h}  guminterceptor-priv.h
│  │  ├─ guminvocationlistener.{c,h}  guminvocationcontext.c
│  │  ├─ gumcodeallocator.{c,h}  gumcodesegment.{c,h}
│  │  ├─ gummemory.{c,h}  gumlibc.{c,h}  gumspinlock.{c,h}  gumtls.h
│  │  ├─ gummetalarray.{c,h}  gummetalhash.{c,h}
│  │  ├─ gumcloak-stub.c  gumunwindbroker-stub.c
│  │  └─ gum.{c,h}             # 精简后的 init/deinit
│  ├─ disasm/                 # 自研紧凑解码器（参考 Detours，替代 capstone）
│  │  ├─ hx_disasm.h          # 统一解码器接口（§2.3 输出契约）
│  │  ├─ hx_disasm_x86.c      # x86/x64 表驱动解码器（含 modrm/disp 偏移、RIP、分支分类）
│  │  ├─ hx_disasm_arm64.c    # 后续里程碑
│  │  └─ hx_disasm_arm.c      # arm + thumb，后续
│  ├─ arch/
│  │  ├─ x86/  gumx86writer|relocator|reader.{c,h}  # relocator/reader 改用 hx_disasm
│  │  ├─ arm/  … arm64/ …    # 后续里程碑
│  ├─ backend/
│  │  ├─ x86/     gumcpucontext-x86.c  guminterceptor-x86.c
│  │  ├─ windows/ gummemory-windows.c  gumtls-windows.c  hoox_process-windows.c
│  │  ├─ linux/   darwin/ freebsd/ posix/   # 后续里程碑
│  └─ facade/
│     └─ hoox.c               # hoox_* → gum_* 的公共 facade 实现
├─ tests/
│  ├─ harness/                 # 无 GLib 的测试骨架（TESTLIST/TESTENTRY/TESTCASE + assert）
│  ├─ core/                    # 移植自 frida 的 interceptor / writer / relocator 测试
│  ├─ targetfunctions/         # 靶函数
│  └─ CMakeLists.txt
├─ tools/
│  └─ amalgamate.py            # 合并为单一 hoox.c / hoox.h
├─ docs/
│  ├─ PLAN.md   TASKS.md   ARCHITECTURE.md   PORTING.md
└─ _refs/                      # 仅参考，.gitignore 已忽略；含 frida、capstone、Detours
```

---

## 5. 里程碑路线图

> 采用**垂直切片优先**：先在 Windows x64 打通 “提取→编译→hook 生效→测试全绿→单文件合并”
> 的完整闭环，再横向铺开到其它平台与架构。

| 里程碑 | 名称 | 出口标准（Definition of Done） |
|---|---|---|
| **M0** | 脚手架与工具链 | 目录/CMake 骨架就绪；参考源码就位（`_refs/`：frida、capstone、Detours）；许可证与 NOTICE 到位；CI 能跑空构建 |
| **M1** | nano-glib 兼容层 | `hx_*`（类型/内存/原子/锁/array/ptrarray/hash/queue/slist/string）实现 + 单元测试全绿；**amalgamation 可行性 spike 通过** |
| **M1.5** | 测试骨架（无 GLib，M4 前置） | 复刻 `TESTLIST/TESTENTRY/TESTCASE` + `g_assert_*` 的骨架 + `testutil`/`lowlevelhelpers` 移植；样例测试可注册运行（**因 M4 的 writer/relocator 测试需要它**，故前移） |
| **M2** | 自研解码器（x86/x64） | `hx_disasm` 接口定稿；x86/x64 解码器（参考 Detours）实现；对 frida `x86relocator` 用例做**差分验证**通过（capstone 作 oracle） |
| **M3** | 底层原语（Win x64） | gumdefs/libc/spinlock/tls/memory(win)/codesegment(stub)/process-shim/cloak+unwind stub 编译通过 |
| **M4** | arch-x86 代码生成 | x86 writer/relocator/reader 编译通过；移植的 writer & relocator 测试全绿 |
| **M5** | Interceptor 核心 | interceptor + listener + context + codeallocator 适配 `hx_*`/`hoox_*`；能 attach/replace 真实函数 |
| **M6** | 拦截器测试（Win x64） | 靶函数就位 + 移植 `interceptor.c` 全部用例在 Windows x64 全绿（骨架已在 M1.5 完成） |
| **M7** | 单文件合并 | `amalgamate.py` 产出 `hoox.c`/`hoox.h`；用合并产物重跑 M6 测试全绿 |
| **M8** | 横向平台铺开 | Linux x64 → macOS(x64/arm64，含真实 codesegment/dlopen 测试) → 各自测试全绿 |
| **M9** | 横向架构铺开 | ARM64 → ARM/Thumb backend+arch+测试；Android/FreeBSD backend |
| **M10** | 收尾 | 全平台 CI 矩阵、文档、示例、发布物（含 amalgamation）；许可证合规复核 |

各里程碑的细粒度任务见 [`TASKS.md`](./TASKS.md)。

---

## 6. 平台 / 架构支持矩阵（最终目标）

| OS \ Arch | x86 | x86_64 | ARM | ARM64 | 备注 |
|---|:--:|:--:|:--:|:--:|---|
| Windows | ✓ | ✓(M6) | – | ✓ | RWX 页；codesegment=stub |
| Linux | ✓ | ✓(M8) | ✓ | ✓ | posix tls/exceptor stub |
| Android | ✓ | ✓ | ✓ | ✓ | linux backend + `gumandroid` |
| macOS | ✓ | ✓ | – | ✓ | **真实** codesegment；越狱集成不移植 |
| iOS/tvOS | – | – | – | ✓ | 代码签名 / ptrauth |
| FreeBSD | ✓ | ✓ | ✓ | ✓ | posix backend |

> 注：barebone（无 OS）后端、dbghelp、libunwind/libdwarf 后端与 hook 无关,不移植。
> **MIPS 已从支持目标中移除**（frida 的 mips relocator 本身即残缺——仅实现 `MIPS_INS_B`,
> `J` 及所有条件分支为 `g_assert_not_reached()`——故不纳入 hoox）。
> **QNX 已从支持目标中移除**：其工具链（qcc / QNX SDP）为专有且需许可证,无托管 CI runner
> 可编译/运行,无法满足"每次改动必须编译通过再提交"的验证要求,故不纳入 hoox。

### 6.1 各平台 backend 实现纪要（现状，均已 CI 实测）

- **Windows**（x86 / x86_64 / ARM64；MSVC / clang / gcc(MinGW)）：`src/backend/windows`，
  RWX 页路径，near 分配用 `VirtualAlloc` 双向探测，TLS 非 x86 上回退 `TlsGetValue`。ARM64 由
  原生 `windows-11-arm` runner 验证；32 位 ARM 不适用（Windows-on-ARM 仅 ARM64）。
- **Linux**（x86 / x86_64 / ARM / ARM64；gcc / clang）：`src/backend/posix` + `src/backend/linux`，
  RWX 路径，页保护/near 分配基于 `/proc/self/maps`，pthread TLS，线程枚举/挂起用 `/proc` + `tgkill`。
  同一份 arch-agnostic backend 覆盖四种架构：x86_64 直接构建，x86 加 `-m32`（`gcc-multilib`），ARM64 用
  原生 `ubuntu-24.04-arm`，ARM（A32+Thumb）交叉编译后在 `qemu-arm` 下跑测试。
- **Android**（arm64-v8a / armeabi-v7a / x86_64 / x86；NDK）：Linux 内核 + bionic，整份复用
  `backend/posix` + `backend/linux`，仅新增极小的 `backend/android`（`hoox_android_get_api_level`——
  API 29+ 上可执行代码页可能不可读，解码前需先加 READ）。CI 用 NDK 交叉编译四个 ABI（静态链接），因
  bionic 即 Linux ABI，静态测试程序可像 Linux ARM32 那样在 `qemu-<arch>-static` 下跑完整 ctest。
  **必链 `-pthread`**（bionic 的 pthread 在 `libthr`，否则符号解析到 libc 空存根，TLS 静默失效）。
- **macOS**（x86_64 / ARM64；AppleClang）：复用 x86/arm64 解码器与 arch、`backend/posix`（mmap +
  pthread TLS），新增 `backend/darwin`（`mach_vm_region` 查页保护、mach 线程 + dyld 模块枚举）。**关键：
  patch 签名 `__TEXT` 需 `mach_vm_protect` + `VM_PROT_COPY`**（生成可写私有副本绕过 W^X；`mprotect(2)`
  会被拒）。x86_64 走 RWX 路径；ARM64 走 COPY 路径。另有自研 Darwin code-segment（老内核用，新内核
  `is_supported` 返回 FALSE）。CI：`macos-15-intel` / Apple Silicon `macos-15`。
- **iOS / tvOS**（ARM64；AppleClang）：复用 `backend/darwin` + `arch/arm64`，仅 `HAVE_IOS`/`HAVE_TVOS`
  分支不同。公开 SDK `#error` 封了 `<mach/mach_vm.h>`，故像 frida 那样自行声明用到的 `mach_vm_*`
  （`backend/darwin/hooxdarwin.h`）；darwin code-segment 的 iOS 路径是老越狱签名机制（hoox 不提供，同
  DarwinGrafter），故 iOS/tvOS 用通用 stub code-segment，走与 macOS arm64 相同的 `VM_PROT_COPY` 路径。
  CI（Apple Silicon runner）：设备 SDK（`iphoneos`/`appletvos`）交叉编译（仅编译，需签名/真机才能运行）
  + 模拟器（`iphonesimulator`/`appletvsimulator`）`simctl spawn` 跑完整套件。**真机（设备端签名强制 +
  arm64e ptrauth）尚未测试**——需越狱设备，由作者本地验证，不在 CI 内。
- **FreeBSD**（x86 / x86_64 / ARM64；clang）：复用 `backend/posix`，新增 `backend/freebsd`，RWX 路径
  （`mprotect`，无需 `VM_PROT_COPY`）；页保护/near 分配用 `sysctl KERN_PROC_VMMAP`（非 `/proc`），线程 id
  用 `pthread_getthreadid_np`，挂起/枚举用 `thr_kill` + `sysctl KERN_PROC`，模块枚举用 `dl_iterate_phdr`。
  CI 无托管 runner，故用 `vmactions` 起 amd64 FreeBSD VM（x86_64 原生 + x86 用 `clang -m32`），ARM64 用
  `cross-platform-actions` 的 FreeBSD/arm64 客户机实跑；ARM（32 位）按构造覆盖（tier-2，无现成 CI 镜像）。

### 6.2 Apple Silicon 自宿主同页（W^X）问题——已根治

Apple arm64（16 KiB 页 + 强制 W^X）上，in-place patch 一页代码时该页会短暂失去执行权限；若被 hook 的
函数与 hoox **自身的 patch 代码**落在同一 16 KiB 页（把 hoox 静态链接进目标、hook 同一二进制内的同页
函数），补丁过程会自崩。**根治方案**（`src/backend/darwin/hooxpatch-darwin.c`）：检测到同页碰撞时，用
`HooxArm64Writer` 在 hoox **自有的独立可执行页**上生成一小段桩，桩里 `mach_vm_protect(RW|COPY)` → 写入
diff 出的连续变更字 → `mach_vm_protect(RX)`；执行中的补丁指令在桩页上而非目标页，故目标页短暂丢 X 不再
自崩。分离页（常规情形）仍走原 in-place 路径，零回归。`interceptor_smoke` 已在 macOS arm64 / iOS·tvOS
模拟器实测通过。（frida 的 `vm_remap` 可写别名在纯进程内不可行：写签名 `__TEXT` 会 COW 到别名副本，需
frida 外部 agent 的 page-plan。）**仍存限制**：硬化签名的正式设备连 `VM_PROT_COPY` 都被内核拒，纯进程内
无解（frida 亦需外部 agent），此时 hoox 干净返回错误而非崩溃。

---

## 7. 第三方与许可证合规

- **主许可证**：wxWindows Library Licence, Version 3.1（同 frida-gum）。保留所有原始
  版权头（Ole André Vadla Ravnås 等），`COPYING` 原样保留。
- **自研解码器 `hx_disasm`**：以 Detours 为蓝本重写。凡**移植/派生**其 opcode 表或
  解码逻辑的部分，视为 MIT 派生作品，须在文件头与 `NOTICE` 保留 Detours 的
  **MIT** 版权与许可声明（Copyright Microsoft Corporation）。纯自研部分归本项目。
- **capstone / Detours**：仅置于 `_refs/`（参考、差分验证 oracle），**不进版本库、不链接、
  不分发**（`.gitignore` 已忽略）；因此其许可不构成分发义务。
- **dlmalloc**（若最终保留）：public domain（CC0）；当前计划**移除**（见 §3.1）。
- `_refs/` 全目录 `.gitignore` 忽略，不进版本库、不参与构建（满足需求 5）。
- amalgamation 产物头部内联汇总所有第三方版权与许可证声明。

---

## 8. 风险与对策

| 风险 | 等级 | 对策 |
|---|---|---|
| 热路径无锁 COW + 事务引擎移植出错（内存序 / 旧数组回收） | 高 | 用 C11 `stdatomic` 精确复刻；`attach_detach_torture` 等并发用例作为回归闸门 |
| nano-glib 语义与 GLib 有细微差异 | 中 | `hx_*` 对齐 GLib 语义并配独立单元测试；hash 迭代/删除、array 自动扩容重点覆盖 |
| 自研解码器解码错误（长度/分支分类/x86 编码偏移） | 中高 | 以 Detours 为蓝本；对 frida relocator 测试做差分验证，capstone 作 oracle 大规模随机指令对拍；覆盖 SSE/AVX/VEX/EVEX 序言 |
| Detours 无 ARM 语义位需补 | 中 | ARM/ARM64 语义位从定长编码位域解出（参考 Detours）；各以对应 relocator 测试验收 |
| 误判进程/模块 shim 的层次（把 Darwin/ELF32 code-cave 或 Apple 挂起路径当成 Windows 首切片必需，或反之漏掉最小模块 shim） | 中 | 严格按 §2.1 ⚠️ 分层：Windows x64 首切片只需 thread-id/system-error/code-signing + 最小 GumModule/range shim + 线程枚举/挂起"链接项"；模块枚举找 code cave 与运行时挂起分别推迟到 Darwin/ELF32 与 Apple arm64 |
| 单文件合并 static 符号/宏冲突（gum 非 amalgamation 友好） | 中 | M1 早做可行性 spike；必要时每文件静态前缀 / unity-include / 改名 |
| Windows 工具链与 C11 原子（MSVC 支持受限、gum 用 GNU 扩展） | 中 | 首选 clang（D5）；若须 MSVC 则补 GNU-ism 垫片 + `_Interlocked*` 原子回退 |
| 全平台 W^X/代码签名差异 | 中 | 逻辑隔离在 backend；Apple 单独 codesegment；先做 RWX 平台 |
| 上游 frida 演进导致对齐困难 | 低 | 内部保留 `gum_*` 名 + 记录提取版本 commit，便于 `diff` |
| 与 frida 相同协议的合规 | 低 | 保留版权头 + NOTICE + COPYING |

---

## 9. AI Vibe Coding 工作方式约定

- 每个任务（见 TASKS.md）**自洽、可独立实现与验证**，粒度适合单个 AI agent 一次完成。
- 每个任务完成需：编译通过 + 对应测试/冒烟验证通过 + 更新任务状态。
- 提取代码时**优先机械搬移 + 最小改动**（切 glib、换 `hx_*`、切目标平台宏），
  避免顺手重构，保证与 frida 行为一致、便于对拍。
- 遇到 `_refs/` 缺失的上游源码：clone 到 `_refs/`；找不到则**停下并提示用户**（需求 6）。
- 每个里程碑结束做一次“漂移检查”：确认未引入被明确排除的模块/依赖。
