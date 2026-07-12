# hoox 任务拆分（AI Vibe Coding）

配套 [`PLAN.md`](./PLAN.md)。任务按里程碑组织。每个任务标注：
**输入**（参考的 `_refs` 源）、**产出**、**验收**。除非注明，源均在
`_refs/frida/subprojects/frida-gum/`。任务尽量单 agent 可独立完成。

图例：`[ ]` 未开始 · `[~]` 进行中 · `[x]` 完成 · `⛓` 有前置依赖。

> **进度 note**：Windows 垂直切片（x86 + x64）已完成，MSVC/clang/gcc 三工具链全绿。
> 随后做了一次一次性命名重构：任务描述里的 `gum_*`/`cs_*`/glib 符号名在代码中现已分别为
> `hoox_*`/`hx_*`。语言标准为 **C99**；公共头 `include/hoox.h` 仅暴露 API。详见
> [`PLAN.md`](./PLAN.md) 顶部进度 note 与 [`../CLAUDE.md`](../CLAUDE.md)。

---

## M0 · 脚手架与工具链

- **T0.1** 建立目录结构与顶层 `CMakeLists.txt` 骨架
  - 产出：§4 目录树空壳；CMake 选项 `HOOX_TARGET_ARCH`、`HOOX_ENABLE_TESTS`、
    `HOOX_BUILD_AMALGAMATION`；`C_STANDARD 11`；**Windows 默认工具链 clang/clang-cl（决策 D5）**，
    MSVC 作可选（触发 GNU-ism 垫片 + Interlocked 原子路径）。
  - 验收：`cmake -B build && cmake --build build` 空构建成功。
- **T0.2** 许可证与合规文件
  - 产出：`COPYING`（wxWindows 3.1）、`NOTICE`（frida-gum(wxWindows 3.1) + `hx_disasm`
    派生自 Detours 的部分(MIT)；capstone 不分发故不列入）、
    `README.md` 骨架、`.gitignore`（确认忽略 `/_refs`、`/build`）。
  - 验收：文件齐备，许可证文本与 frida `COPYING` 一致。
- **T0.3** 参考源码就位（仅 `_refs/`，不进版本库/不链接）
  - 现状：`_refs/capstone`（frida fork）与 `_refs/Detours` 均已 clone。
  - 动作：确认 capstone 为 rev `d536b1577fd033a31d75f48fd183aa425256cc18`（作差分
    验证 oracle）；Detours 作 x86/x64 解码器重写蓝本。缺失则提示用户。
  - 验收：两目录存在；`.gitignore` 忽略 `/_refs`。
- **T0.4** CI 骨架（GitHub Actions）
  - 产出：Windows x64 job（先只跑空构建 + 后续接测试）。
  - 验收：CI 通过。

## M1 · nano-glib 兼容层（`src/compat/`）

> 目标：提供“形似 GLib”的最小纯 C API，让提取的 gum 代码机械替换即可编译。
> 每个子任务都要有独立单元测试（放 `tests/compat/`）。

- **T1.1** `hx_types.h` + `hx_macros.h`
  - 内容：`gint/guint/gsize/gpointer/gconstpointer/gboolean/guint8/guint64/gchar`
    等 → 定长/标准类型；`TRUE/FALSE`、`G_BEGIN_DECLS`/`G_END_DECLS`、
    `G_GNUC_*`、`HOOX_API`、`gum` 侧 `GUM_API`；**`GLIB_SIZEOF_VOID_P`、`G_BYTE_ORDER`、
    对齐/packed 宏（`GUM_ALIGN` 等）的 clang/MSVC 双写法（`__attribute__` vs `__declspec`）**。
  - 验收：单独编译 + 尺寸静态断言通过。
- **T1.2** `hx_mem` + `hx_assert`
  - 内容：`g_malloc/g_malloc0/g_free/g_new/g_new0/g_slice_*` → malloc 家族；
    `g_assert*/g_assert_not_reached` → 平替；**`g_debug/g_warning/g_critical` →
    no-op 或 fprintf 平替**。
  - 验收：分配/释放/断言单测通过。
- **T1.3** `hx_atomic.h`
  - 内容：基于 C11 `<stdatomic.h>` 封装 `g_atomic_int_inc/dec_and_test/get/set`、
    `g_atomic_pointer_get/set/compare_and_exchange`；提供**指针 COW helper**。
  - 验收：多线程 inc/dec、CAS 单测通过（TSan 可选）。
- **T1.4** `hx_lock`（mutex + recursive mutex）
  - 内容：`GMutex`/`GRecMutex` → Windows `CRITICAL_SECTION` / pthread。
  - 验收：递归加锁、跨线程互斥单测通过。
- **T1.5** `hx_array`（`GArray`）与 `hx_ptrarray`（`GPtrArray`）
  - 内容：`g_array_new/append_val/index/set_size/remove_index/free`、
    `g_ptr_array_new_full/add/remove/foreach/free`（含 `element free func`）。
  - 验收：扩容、按值索引、带析构释放的单测通过。
- **T1.6** `hx_hash`（`GHashTable`）
  - 内容：`g_hash_table_new_full/insert/lookup/remove/foreach/iter/size/destroy`；
    支持 `GHashFunc/GEqualFunc/GDestroyNotify`；指针/整型键 hash。
  - 验收：插入/查找/删除/迭代中删除单测通过。
- **T1.7** `hx_queue`（`GQueue`）+ `hx_slist`（`GSList`/`GList`，含侵入式）
  - 内容：`g_queue_*`、`g_slist_*`、`g_list_*`（覆盖 gumcodeallocator 侵入式用法）。
  - 验收：入队/出队/遍历/侵入式插删单测通过。
- **T1.8** `hx_string`（`GString`）
  - 内容：`g_string_new/append/append_c/append_printf/free`（供测试与靶函数）。
  - 验收：拼接/格式化单测通过。
- **T1.9** ⛓(T1.1) amalgamation 可行性 spike（**风险前置验证**）
  - 内容：先写 `tools/amalgamate.py` 最小版，把 `hx_types.h` + 2~3 个 compat 单元
    合并为单一 `.c/.h` 并编译/跑通其单测；探明 file-local `static` 同名冲突、宏重定义、
    include 去重等问题，据此定策略（每文件静态前缀 / unity-include / 改名）。
  - 验收：最小合并产物 `cc` 通过、单测全绿；产出一页《amalgamation 策略》结论。
- **T1.10** ⛓(T1.3) `hx_atomic` 的 **MSVC 回退**（若采纳 MSVC；clang 下可空）
  - 内容：MSVC 用 `_Interlocked*` 内建实现 `hx_atomic` 的 inc/dec/get/set/CAS 与指针 COW。
  - 验收：MSVC 下原子单测通过（或明确标注仅 clang 目标而跳过）。

## M1.5 · 测试骨架（无 GLib，M4 前置）

> **前移原因**：M4 的 writer/relocator 测试（T4.4/T4.5）需要它，故提前到 M2/M4 之前完成。

- **T1H.1** ⛓(M1) 无 GLib 测试骨架（`tests/harness/`）
  - 内容：复刻 `TESTLIST_BEGIN/END`、`TESTENTRY`、`TESTCASE`、`TESTGROUP_*`、
    `TESTLIST_REGISTER`，及 `g_assert_*` 等价断言；简单 runner `main`
    （支持按路径 `/Core/Interceptor/xxx` 过滤）。
  - 验收：一个样例测试模块可注册、运行、断言。
- **T1H.2** ⛓(T1H.1) **按需裁剪**移植 `testutil.{c,h}` + `lowlevelhelpers.{c,h}`（去 glib，用 `hx_string` 等）
  - ⚠️ **不可整搬（已核实）**：`testutil.h` `#include` 了 `gum-heap.h`/`gum-prof.h`，`testutil.c`
    用 `gum_exceptor_obtain()` 和 `gum_process_find_heap_apis()`（均属已丢弃模块）。→ 只移植
    M4/M6 实际引用到的部分（字节 diff、CPU 上下文魔数、数据目录定位），**剔除 heap/prof/exceptor**
    相关声明与实现。
  - 验收：字节 diff、CPU 上下文魔数填充/比对可用；不引入 heap/prof/exceptor 依赖。

## M2 · 自研紧凑解码器（x86/x64 优先，替代 capstone）

> 参考 `_refs/Detours/src/disasm.cpp`（x86/x64 ~1520 行表驱动）重写为 C，产出
> frida relocator 需要的接口（见 PLAN §2.3）。capstone 仅作差分验证 oracle。

- **T2.1** 定义解码器接口 `src/disasm/hx_disasm.h` + **指令/条件码枚举**
  - 内容：`hx_insn { length; address; bytes[]; id/class; op_count; operands[0..2]
    {kind,reg,imm,mem.base/index/disp,shift,subtracted}; cc }`；**x86 专用**
    `modrm/modrm_offset/disp_offset/disp` + `hx_insn_reg_read/write()` 谓词；
    解码入口 `hx_disasm_one(bytes, addr, mode) -> hx_insn`。
  - **必须定义 `hx_x86_insn` 枚举与条件码枚举**（替代 capstone 的 `x86_insn`/`X86_INS_*`）：
    不仅 relocator/reader 用，**writer（`gumx86writer.h/.c`）的公共签名与内部 `switch` 也依赖它**
    （见 PLAN §3.1）。此枚举是 M2 的公共产物，M4 的 writer 依赖它。
  - 验收：接口能表达 frida relocator/reader/writer 读取的全部字段与指令 id（对照 PLAN §2.3/§3.1）。
- **T2.2** ⛓(T2.1) x86/x64 解码器 `hx_disasm_x86.c`
  - 内容：移植 Detours 的 opcode/ModRM 表与解码循环（摊平类→struct+函数指针表）；
    正确计算长度（含 REX/66/67/VEX/EVEX/XOP/APX 前缀、ModRM/SIB/disp）；识别分支族
    (CALL/JMP/Jcc/JECXZ/JRCXZ/RET) 并给助记符 id + 绝对目标；填 x86 编码偏移供 RIP 修复。
    **文件头保留 Detours MIT 声明。**
  - 验收：能对任意字节流逐条给出正确长度与分类。
- **T2.3** ⛓(T2.2) 差分验证工具 + 冒烟
  - 内容：`tests/disasm/` 用 capstone 作 oracle，对大批随机/语料指令对拍 `length` 与
    关键字段；覆盖 SSE/AVX/VEX/EVEX 及常见序言模式。
  - 说明：capstone oracle 需一个**仅测试用**的本地构建配方（用 frida 的 meson 或最小
    CMake 建 X86-only 静态库；**不进版本库、不进发布物**）。
  - **专项：寄存器读写语义对拍**（`hx_insn_reg_read/write()`）。x86 RIP 相对重写靠它选
    scratch 寄存器（`gumx86relocator.c:625/694`），语义错会生成污染寄存器的 trampoline。
    对 `MOV`/`CMPXCHG`/`PUSH`/RIP-relative 等常见指令与 capstone 的 `cs_reg_read/write` 对拍。
  - 验收：差分测试通过；无长度/分类/寄存器读写偏差。

## M3 · 底层原语（Windows x64）

> 提取自 gum/ 与 backend-windows / backend-x86，切断 glib、改用 `hx_*`、
> 只保留 Windows x64 代码路径（`HAVE_WINDOWS`/`HAVE_I386`+64 位）。

- **T3.1** ⛓(M1) `gumdefs.h` 去 glib 化
  - 动作：移除 `#include <glib.h>` 与 `gumenumtypes.h`，改依赖 `hx_types.h`；
    保留 `GumCpuContext`、`GumAddress`、`GUM_API` 等。
  - 验收：单独 include 编译通过。
- **T3.2** `gumlibc.{c,h}`（自洽，几乎原样）。验收：编译通过。
- **T3.3** ⛓(T1.4) `gumspinlock.{c,h}`。验收：编译 + 基本加解锁测试。
- **T3.4** `gumtls-windows.c`（`gum_tls_key_*`）。验收：TLS set/get 单测通过。
- **T3.5** ⛓(M1) `gummemory.{c,h}` + `gummemory-windows.c`
  - 动作：保留页查询/`gum_mprotect`/`gum_memory_patch_code`(+`_pages`)/`gum_memory_allocate_near`
    (Windows 的 `VirtualAlloc` 双向近分配)/alloc/free/page 分配器；`gum_internal_malloc`
    **重定向到系统 malloc**（移除 dlmalloc）。
  - **从 header 和实现同时删除 scan/pattern API**（`gum_memory_scan`/`find_pointers`/
    `GumMatchPattern`/`gum_match_pattern_*`）——**不是留桩**：`GumMatchPattern` 内嵌
    `GRegex *`（`gummemory.c:99`），还牵出 `GThreadPool`/boxed type，留桩会把这些依赖带回。
    只保留 hook 所需的页保护、alloc、patch code。
  - 验收：分配可执行页、改页保护、patch code 冒烟测试通过；编译产物无 GRegex/GThreadPool 引用。
- **T3.6** `gumcodesegment.{c,h}`（非 Apple 通用 stub：`is_supported()`→FALSE）。
  验收：符号解析、编译通过。
- **T3.7** `hoox_process-windows.c` + 最小模块 shim（**分层**，不引入整个 gumprocess.c/gummodule.c）
  - 已按源码逐行核实（见 PLAN §2.1 ⚠️）。**首切片 hook 关键路径**只需：
    - `gum_process_get_current_thread_id`、`gum_thread_get/set_system_error`（保 `GetLastError`）、
      `gum_process_get_code_signing_policy`→`OPTIONAL`。
  - **链接项（首切片运行时不触达，但须能编译链接）**：`_gum_process_enumerate_threads` +
    `gum_thread_suspend/resume`（`gum_memory_patch_code` 的挂起分支仅在 `rwx_supported==FALSE`
    即 Apple arm64 才执行；Windows x64 走 RWX 路径）。可给 Toolhelp 实现或最小实现。
  - **最小 `GumModule`/range shim**（`src/core` 内，非完整 GObject）：满足 `gumprocess.h` 类型面
    （`GumFoundModuleFunc`/`GumModule *`）与**测试工具** `gum_process_get_main_module` +
    `gum_module_get_range` + `gum_process_enumerate_modules`（用 `EnumProcessModules`/
    `GetModuleInformation`，链接 `psapi`）。
  - **不做**：`gum_code_deflector` 的 code-cave 模块探测（`gumcodeallocator.c:571`，Darwin/ELF32
    专属，Windows x64 不编译）。
  - 验收：system-error 往返正确、code-signing=OPTIONAL；模块 shim 能给出主模块与范围（供测试）；
    线程枚举/挂起符号可链接；上述均**不阻塞** Windows x64 的 attach/replace。
- **T3.8** `gumcloak-stub.c` + `gumunwindbroker` 处置（**粒度明确**）
  - `gumcloak`：`add_range`/`remove_range` no-op 桩。
  - `gumunwindbroker`：`guminterceptor.c` 持有 `GumUnwindBroker * unwind_broker` 字段，
    `obtain` 时 `gum_unwind_broker_obtain()` 创建、`dispose` 时 `g_clear_object` 清理
    （`guminterceptor.c:82/425/486`）。**二选一**：(a) 提供 header + 类型 + `obtain/ref/unref`
    的 no-op 实现使生命周期成立；或 (b) **直接从 interceptor 去掉该字段与相关调用**。
    首切片推荐 (b)（更干净，去 GObject 后不留半截对象语义）。
  - 验收：编译通过；interceptor 生命周期无悬挂引用/泄漏。
- **T3.9** ⛓(M1) `gummetalarray.{c,h}` + `gummetalhash.{c,h}`
  - 动作：其内部分配器指向系统 malloc / 页分配器（不依赖 dlmalloc）。
  - 验收：metal array/hash 基本操作单测通过。

## M4 · arch-x86 代码生成

- **T4.1** ⛓(T2.1,M3) `gumx86writer.{c,h}`
  - 动作：改用 `hx_*`（label 表用 metal hash/array）、去 glib。
  - **capstone 清理（已核实）**：`gumx86writer.h` `#include <capstone.h>` 且公共签名用 `x86_insn`
    枚举，`gumx86writer.c` `switch (X86_INS_*)` —— 改用 T2.1 的 `hx_x86_insn` 枚举。故 writer
    **依赖 T2.1（解码器接口/枚举）**，非"完全不依赖 M2"。另处理 `gum-init.h` 的 `#include <capstone.h>`。
  - 验收：编译通过；writer 头无 capstone 依赖。
- **T4.2** ⛓(T4.1,M2) `gumx86reader.{c,h}`：把 capstone 消费改为 `hx_disasm`
  （`cs_disasm_iter`→`hx_disasm_one`，`insn->id`/detail 字段→`hx_insn` 对应字段）。
  验收：编译通过。
- **T4.3** ⛓(T4.2) `gumx86relocator.{c,h}`：同上改用 `hx_disasm`；重写/代码生成逻辑保留。
  验收：编译通过。
- **T4.4** ⛓(M1.5) 移植 `tests/core/arch-x86/x86writer(.c+fixture)`
  - 验收：全部 writer 用例在 Windows x64 全绿。
- **T4.5** ⛓(M1.5,T4.3) 移植 `x86relocator(.c+fixture)`。验收：全部 relocator 用例全绿。

## M5 · Interceptor 核心

- **T5.1** ⛓(M1) `guminvocationlistener.{c,h}`：GObject interface → C vtable
  （`struct { on_enter; on_leave; }` + `hoox_*` 构造器 `hoox_make_call_listener` 等）。
  **C listener 需自带引用计数（ref/unref）+ weak 单例语义**，以通过 `listener_ref_count` 用例。
  验收：编译通过 + 简单 listener 回调测试 + refcount 语义正确。
- **T5.2** `guminvocationcontext.c`（几乎原样，改类型）。验收：编译通过。
- **T5.3** ⛓(M3) `gumcodeallocator.{c,h}`：改 `hx_*`；cloak 调用走 stub。
  验收：能分配/回收可执行 slice。
- **T5.4** ⛓(T5.1..3,M4) `guminterceptor.c/.h/-priv.h` + `backend-x86/guminterceptor-x86.c`
  + `backend-x86/gumcpucontext-x86.c`
  - 动作（最关键）：`GHashTable/GArray/GPtrArray/GQueue`→`hx_*`；
    `g_atomic_*` COW→`hx_atomic`；单例/refcount/dispose→显式生命周期；
    grafted-trampoline 路径返回 NULL；只留 x86 后端。
    **`guminterceptor-x86.c` 直接调用的 `cs_disasm_iter`（detect_hook_size 内）改为
    `hx_disasm`**（capstone 迁移点不止 relocator/reader）。
  - 验收：编译通过，`gum_interceptor_obtain`/attach/replace 可运行。
- **T5.5** ⛓(T5.4) `gum.{c,h}` 精简 init/deinit（去 GObject 类型注册、去 capstone
  自定义分配器 hook 或改为可选）。验收：`hoox` 初始化/反初始化无泄漏。
- **T5.6** ⛓(T5.*) `src/facade/hoox.c` + `include/hoox.h`
  - 内容：`hoox_interceptor_*`、`hoox_invocation_*`、listener 构造等公共 API
    映射到内部 `gum_*`。**重塑 `gum_interceptor_detect_hook_size(..., csh, cs_insn *)`
    的签名**（去掉 capstone 类型泄漏，改用 `hx_disasm` 类型或内部化）——公共头不得出现
    capstone 类型。
  - 验收：仅 include `hoox.h` 即可写出 attach/replace 示例并运行；公共头无 capstone 依赖。

## M6 · 拦截器测试（Windows x64）

> 测试骨架 + testutil/lowlevelhelpers 已在 **M1.5（T1H.1/T1H.2）** 完成。

- **T6.3** ⛓ 靶函数 `tests/targetfunctions/`（Windows：直接编进测试二进制）。
  验收：`gum_test_target_function` 等符号存在且不被内联。
- **T6.4** ⛓(M1.5,T6.3,M5) 移植 `tests/core/interceptor.c`（+ fixture + 两个 listener 辅助类
  的 C 改写）
  - ⚠️ **heap API 依赖**：`interceptor-fixture.c` 用 `gum_heap_api_list_get_nth()` 找
    `malloc/free` 作 hook 目标（`interceptor-fixture.c:296`，属已丢弃的 heap 模块）。→ 提供一个
    **最小 heap-api provider**（仅返回 CRT `malloc/free/realloc` 地址），或把相关用例改为直接
    hook CRT `malloc/free`。`attach_to_heap_api` 等用例据此调整。
  - 验收：Windows x64 下 `interceptor` 全部（平台适用）用例**全绿**，
    含 `attach_detach_torture`、`replace_*`、`*_fast`、`ignore_*_thread`、`attach_to_heap_api`。
- **T6.5** CI 接入：Windows x64 job 运行 M4 + M6 测试。验收：CI 全绿。

## M7 · 单文件合并（amalgamation）

- **T7.1** `tools/amalgamate.py`
  - 内容：按依赖序内联 `src/compat` + `src/disasm` + `src/core` + `src/arch/x86` +
    `src/backend/*` + `src/facade`，去重 include，产出 `hoox.c` + 单一 `hoox.h`；
    头部汇总许可证（含 hx_disasm 中派生自 Detours 部分的 MIT 声明）。
  - 验收：产物可独立 `cc hoox.c` 编译。
- **T7.2** ⛓(T7.1) 用 amalgamation 产物重跑 M6 测试
  - 验收：合并版与分文件版测试结果一致、全绿。

## M8 · 横向平台铺开（x86_64 起）

- **[x] T8.1** Linux x86 / x64（**完成，x86 与 x86_64 全测试全绿**）：新增
  `src/backend/posix/hooxmemory-posix.c`（mmap 分配/near-alloc via free-range 枚举）、
  `src/backend/posix/hooxtls-posix.c`（pthread key）、
  `src/backend/linux/hooxmemory-linux.c`（`mprotect`/`clear_cache`/页保护查询，`/proc/self/maps`
  解析 + `_hoox_memory_query_protections`）、`src/backend/linux/hoox_process-linux.c`
  （`gettid`、`errno` 存取、`tgkill(SIGSTOP/SIGCONT)` 挂起/恢复、`/proc/self/task` 线程枚举、
  `/proc/self/maps` range 枚举、code-signing=OPTIONAL、最小 `HooxModule`/range shim）、
  共享的 `src/backend/linux/hooxlinux-priv.h`（增量式 `/proc/*/maps` 迭代器）。
  说明：Linux 走 RWX 路径（`hoox_query_rwx_support()==HOOX_RWX_FULL`），故线程枚举/挂起为
  **链接项**（运行时不触达）；靶函数直接编进测试二进制（无需 `dlopen`），故未引入 POSIX fork
  处理。CMake：非 WIN32 且 `Linux` 时编译上述源、定义 `HAVE_LINUX`、链接 `Threads::Threads`。
  8 个测试套件（compat/harness/memory/interceptor_smoke/interceptor(10)/disasm_diff/amalgam/
  arch_x86）在 **x86_64 与 x86（32 位，`-DCMAKE_C_FLAGS="-m32"`，需 `gcc-multilib`）** 均全绿；
  amalgamation 单文件产物亦全绿。同一份 backend 覆盖两种位宽，x86 无需改动代码。CI 待接入。
  macOS/darwin backend 见 T8.2。
- **T8.2** macOS x64/arm64：`gummemory-darwin.c`、`gumtls-darwin.c`、
  **真实** `gumcodesegment-darwin.c`、`hoox_process-darwin.c` shim；
  ptrauth 相关（arm64e）按需。验收：macOS 测试全绿 + CI。
- **T8.3** Windows x86（32 位）路径打通。验收：Win x86 测试全绿。

## M9 · 横向架构铺开（每架构含自研解码器）

- **T9.1** ARM64：`hx_disasm_arm64.c`（参考 Detours ARM64，定长位域解码 + 差分验证）、
  `arch-arm64/gumarm64writer|relocator|reader`（改用 hx_disasm）、`backend-arm64/
  guminterceptor-arm64(.c+glue.S)`；移植 arm64 writer/relocator/interceptor 测试。
  验收：arm64 测试全绿。
- **T9.2** ARM/Thumb：`hx_disasm_arm.c`（arm+thumb，参考 Detours Thumb-2）、
  `arch-arm/*`（含 `gumarmreg`）、`backend-arm/*`；移植 arm/thumb writer/relocator +
  `interceptor-arm` 测试。验收：全绿。
- **T9.3** Android（linux backend + `gumandroid.c`）、FreeBSD backend。
  验收：各平台构建 + 可用测试通过。（QNX 已从目标移除：工具链专有、无 CI 可验证。）

## M10 · 收尾

- **T10.1** 全平台/架构 CI 矩阵（交叉编译 + 可运行者跑测试）。
- **T10.2** 文档：`ARCHITECTURE.md`、`PORTING.md`（新增平台指南）、API 参考、
  使用示例（attach / replace / listener）。
- **T10.3** 许可证合规终检；发布物（源码 + amalgamation `hoox.c/.h`）。
- **T10.4** “去除清单”复核：确认无 Stalker/Backtracer/JS/GLib 外部依赖等被排除项残留。

---

## 关键提取清单（Windows x64 首切片，速查）

**从 gum/ 提取**：`gumdefs.h` `gumlibc` `gumspinlock` `gummemory(+win)`
`gumcodesegment(stub)` `gumcodeallocator` `guminterceptor(+priv)`
`guminvocationlistener` `guminvocationcontext` `gummetalarray` `gummetalhash`
`gum.c(精简)` + `arch-x86/gumx86writer|relocator|reader`（relocator/reader 改用
`hx_disasm`）+ `backend-x86/guminterceptor-x86|gumcpucontext-x86` +
`backend-windows/gummemory-windows|gumtls-windows`。
**新写**：`src/disasm/hx_disasm.h`(含 `hx_x86_insn` 枚举) + `hx_disasm_x86.c`（参考 Detours）、
`hoox_process-windows.c` + **最小 `GumModule`/range shim**、`gumcloak-stub.c`、
`src/facade/hoox.c`、`include/hoox.h`、全部 `src/compat/hx_*`、（可选）最小 heap-api provider。
**改造去 capstone 头**：`gumx86writer.h`、`guminterceptor.h`、`gum-init.h`（均 include 或暴露 capstone）。
**丢弃**：**capstone（不链接/不分发，仅 _refs 作差分 oracle）**、dlmalloc、
cloak/unwind 真实实现（unwindbroker 字段建议直接移除）、gummemory 的 scan/pattern API、
所有非 x86 arch、所有 backend-非win、
stalker/backtracer/cfg/apiresolver/symbolutil/exceptor/JS/gumpp、
完整 `gummodule*`（仅留最小 range shim）、`gum-heap*`/`gum-prof*`。
