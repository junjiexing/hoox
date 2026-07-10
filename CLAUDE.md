# CLAUDE.md

本文件为 Claude Code 在本仓库工作时提供指导。

## hoox 是什么

从 **frida-gum** 精简提取、**只保留 inline hook（Interceptor）**能力的纯 C 库。
目标：**无 GLib 依赖、无 capstone 依赖、跨平台、可像 SQLite 一样合并为单一 `hoox.c`/`hoox.h`**，
以 frida 相同协议（**wxWindows Library Licence 3.1**）开源。

- 完整规划：**`docs/PLAN.md`**（目标/调研结论/架构决策/目录结构/里程碑/依赖处置/风险）。
- 任务拆分：**`docs/TASKS.md`**（M0–M10，每个任务含输入/产出/验收，标注依赖）。
- **开工前先读这两份文档。** 本文件是它们的高频要点摘要 + 易错点清单。

## 当前状态

**规划阶段，尚未开始编码。** 首个垂直切片目标：**Windows x64**（打通
提取→编译→hook 生效→测试全绿→单文件合并的完整闭环），再横向铺开其它平台/架构。

## 核心架构决策（已确认）

| # | 决策 | 要点 |
|---|---|---|
| D1 | GLib → **自研 nano-glib `hx_*`** | 仓库内纯 C 重写容器/原子/锁/类型；gum 代码机械替换即可编译，逻辑基本不动 |
| D2 | 公共 API **`hoox_*`** | 内部引擎保留 `gum_*`（便于对齐上游），`include/hoox.h` facade 暴露 `hoox_*` |
| D3 | 首切片 **Windows x64**，工具链**首选 clang/clang-cl** | gum 用 GNU 扩展；MSVC 的 C11 原子受限，若须 MSVC 则补垫片 + `_Interlocked*` |
| D4 | 反汇编 → **自研紧凑解码器 `hx_disasm`**（参考 Detours，弃用 capstone） | 从 capstone 抽子集不可行；参考 Detours 表驱动重写为 C，产出 relocator 所需接口 |

## 提取基线（对齐上游的 diff 基点）

- frida-gum commit `a2ebd7b8f570a0aa82ef6823ffa0f7d39703ffa4`（tag `17.15.4`）
- capstone fork rev `d536b1577fd033a31d75f48fd183aa425256cc18`（仅作差分验证 oracle，不分发）

## 目录约定

```
include/hoox.h        唯一公共头（hoox_* API）
src/compat/           nano-glib：hx_types/mem/atomic/lock/array/hash/queue/slist/string
src/disasm/           自研解码器：hx_disasm.h(含 hx_x86_insn 枚举) + hx_disasm_<arch>.c
src/core/             提取自 gum/ 的 hook 引擎（保留 gum_* 内部名）
src/arch/<arch>/       gumxxwriter/relocator/reader（relocator/reader 改用 hx_disasm）
src/backend/<os|x86>/  gummemory-<os>/gumtls-<os>/guminterceptor-<arch>/hoox_process-<os>
src/facade/hoox.c     hoox_* → gum_* 映射
tests/harness/         无 GLib 测试骨架（TESTLIST/TESTENTRY/TESTCASE + assert）
tests/core/            移植自 frida 的 interceptor/writer/relocator 测试
tools/amalgamate.py   合并为单一 hoox.c/hoox.h
_refs/                 参考源（frida、capstone、Detours）——见下方铁律
```

## 铁律 / 约束

1. **`_refs/` 只能参考或提取代码，绝不 `#include`/链接/直接引用进项目**（已被 `.gitignore` 忽略）。
   缺失的上游源码可 clone 到 `_refs/`；**找不到源码就停下并提示用户**。
2. **纯 C（C11）**，与 frida-gum 一致。构建系统统一 **CMake**。
3. **无第三方运行时依赖**：GLib→nano-glib，capstone→hx_disasm，dlmalloc→系统 malloc。
   `threads`/psapi/Toolhelp 等是 OS 原语/系统库，直接用，不算捆绑第三方。
4. **提取代码时优先机械搬移 + 最小改动**（切 glib、换 `hx_*`、切目标平台宏、capstone→hx_disasm），
   **不要顺手重构**——保证与 frida 行为一致、便于对拍与日后对齐上游。
5. 每个任务完成需：**编译通过 + 对应测试/冒烟验证通过**，再更新 `docs/TASKS.md` 勾选状态。
6. 每个里程碑结束做一次**漂移检查**：确认未引入被明确排除的模块/依赖
   （Stalker/Backtracer/CFG/符号解析/JS 绑定/GLib/capstone 等）。

## 易错点（已按源码逐行核实——违反这些会返工）

- **Windows x64 patch code 走 RWX 路径，运行时不挂起线程。** `gum_query_rwx_support()` 除
  Darwin-非i386 外都返回 `GUM_RWX_FULL`。线程枚举/挂起（`_gum_process_enumerate_threads`/
  `gum_thread_suspend`）只在 Apple arm64 的非-RWX 分支触达；在 Windows 上它们是**链接项**
  （须能编译链接，运行时不执行）。**别把它当 Windows hook 关键路径。**
- **Windows x64 不做 code-cave 模块枚举找 trampoline。** `gumcodeallocator.c` 里用
  `gum_process_enumerate_modules` 找 code cave 的那段被 `#if HAVE_DARWIN || (HAVE_ELF && 32位)`
  包住，Windows 不编译；Windows near 分配走 `gum_memory_allocate_near` 的 `VirtualAlloc` 双向探测。
- **但仍需最小 `GumModule`/range shim**：`gumprocess.h` 无条件 `#include <gummodule.h>`，
  `GumFoundModuleFunc` 传 `GumModule *`，测试用 `gum_process_get_main_module`/`gum_module_get_range`。
  → 不提取完整 GObject `GumModule`，但要定义最小 shim（供类型面 + 测试）。
- **capstone 泄漏点不止 relocator/reader**：`guminterceptor.h` 的 `detect_hook_size(..., csh, cs_insn*)`、
  `backend-x86/guminterceptor-x86.c` 的 `cs_disasm_iter`、`arch-x86/gumx86writer.h`（include capstone 且
  公共签名用 `x86_insn` 枚举）、`gum-init.h`（include capstone）。**全部改用 `hx_disasm` 类型；
  公共头不得出现 capstone 类型。** 因此 **writer 也依赖 `hx_disasm` 的枚举（T2.1）**。
- **`gummemory` 的 scan/pattern API 必须 header+impl 同时删除，不能留桩**：`GumMatchPattern` 内嵌
  `GRegex *`，留桩会把 GRegex/GThreadPool 依赖带回。
- **测试不能整搬**：`testutil.h` include `gum-heap.h`/`gum-prof.h`，`testutil.c` 用 `gum_exceptor_obtain`；
  `interceptor-fixture.c` 用 `gum_heap_api_list_get_nth()` 找 malloc/free。→ 按 M4/M6 实际引用裁剪，
  并提供最小 heap-api provider（或改为直接 hook CRT malloc/free）。
- **hx_disasm 的 x86 RIP 重写还依赖寄存器读写语义**（`cs_reg_read/write` → `hx_insn_reg_read/write`）
  选 scratch 寄存器，错了会生成污染寄存器的 trampoline。须对 MOV/CMPXCHG/PUSH/RIP-relative 对拍。
- **单文件合并前先做可行性 spike**：gum 非 amalgamation 友好，多个 `.c` 有同名 file-local `static`
  与各自宏，直接拼单 TU 易冲突。M1 阶段先验证策略（静态前缀 / unity-include / 改名）。
- **MIPS hook 在 frida 本身就残缺**（relocator 只实现了 `MIPS_INS_B`）。不要承诺超出 frida 的 MIPS 覆盖。

## 明确不需要（直接丢弃）

Stalker、Backtracer、ControlFlowGraph、DarwinGrafter、符号/模块解析（`gum*apiresolver`/
`gumelfmodule`/`gumdarwinmodule`/完整 `gummodule`）、内存扫描、`gumexceptor`（异常/守护页 hook）、
`gumkernel`、profiler/sampler、dbghelp/symbolutil、所有 JS/C++ 绑定（gumjs/gumpp/QuickJS/V8）、
dlmalloc、以及全部非目标架构的 writer/relocator。

## 代码风格（沿用 frida-gum，便于对拍）

- **2 空格缩进**，无 tab；**80 列**上限。
- 函数调用括号前留空格：`func (arg)`；强转后留空格：`(GumAddress) ptr`。
- 指针声明两侧留空格：`Type * name`。
- `{` 单独成行（`if`/`for`/函数签名之后另起一行）；`{` 后与 `}` 前无空行；无连续空行。
- 命名：类型 `GumInterceptor`、函数 `gum_interceptor_attach`、文件 `guminterceptor.{h,c}`；
  公共 API 标 `GUM_API`（内部）/ `HOOX_API`（对外）。
- 新写的 nano-glib/解码器用 `hx_` 前缀；对外 facade 用 `hoox_` 前缀。

## 许可证

- 主许可证 **wxWindows Library Licence 3.1**（同 frida-gum），保留所有原始版权头与 `COPYING`。
- `hx_disasm` 中**派生自 Detours 的部分**须在文件头与 `NOTICE` 保留 Detours 的 **MIT** 声明。
- capstone/Detours 仅在 `_refs/`（不分发），不构成分发义务。
- amalgamation 产物头部内联汇总所有许可证声明。

## 平台/架构（最终目标，分阶段）

Windows / Linux / Android / macOS / iOS / tvOS / FreeBSD / QNX × x86 / x86_64 / ARM / ARM64 / MIPS，
覆盖度与 Frida 对齐（MIPS 为部分/实验性）。W^X/代码签名差异隔离在 backend 层：
Windows/Linux/非越狱走 RWX；Apple 平台单独实现 `gumcodesegment-darwin`。详见 `docs/PLAN.md` §6。
