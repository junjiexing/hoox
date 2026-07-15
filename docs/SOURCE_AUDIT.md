# hoox 源码审计 —— 审阅与清理工作表

> 用于帮助决定删除哪些代码。**当前尚未删除任何东西。** 判定为数据驱动（是否进入构建 + 交叉引用计数），再逐函数阅读后标注。

## 如何阅读本文

- **是否编译** —— 该文件是否进入当前目标（Windows x86/x64）的构建。以 `src/CMakeLists.txt` 为准：只编译 **x86** 架构族 + **Windows** backend，其余（ARM/ARM64）一律不编译。
- 每个函数的 **状态**：
  - **公共API** —— 在 `include/hoox.h` 中声明（对外发布的接口面）。
  - **使用中** —— 被已编译库内部调用。
  - **仅测试** —— 库内不调用，只有 `tests/` 使用。
  - **死代码** —— 在已编译库和测试中均无人调用（只出现其自身定义），最强的删除候选。
- 方法：函数引用数在**所有已编译 `.c` 正文**中统计（仅头文件声明不算“被使用”），并单独统计 `tests/` 中的引用。函数指针 / 宏用法已逐一手工抽查复核。

## 关键数字

- 已编译 `.c` 文件：**30** 个（15,778 行）。未编译 `.c` 文件：**15** 个（11,295 行，475 个函数）—— 见 *未编译* 一节。
- 已编译文件中的函数：**736** 个 —— 使用中 **478**、公共API **50**、仅测试 **46**、**死代码 162**。
- 最大的单项收益：约 11,295 行未编译的 ARM/ARM64 代码，外加已编译文件里的 **162** 个死函数（以 x86 writer 未使用的指令发射器和 metal-hash 迭代器为首）。

## 各文件汇总（已编译集合）

| 文件 | 行数 | 函数数 | 使用中 | 仅测试 | 死代码 | 公共 |
|---|--:|--:|--:|--:|--:|--:|
| `src/arch/x86/hooxx86reader.c` | 171 | 8 | 5 | 0 | 3 | 0 |
| `src/arch/x86/hooxx86relocator.c` | 781 | 29 | 20 | 3 | 6 | 0 |
| `src/arch/x86/hooxx86writer.c` | 3301 | 151 | 75 | 25 | 51 | 0 |
| `src/backend/windows/hoox_msvc_intrinsics.c` | 43 | 2 | 2 | 0 | 0 | 0 |
| `src/backend/windows/hoox_process-windows.c` | 212 | 14 | 9 | 0 | 5 | 0 |
| `src/backend/windows/hooxmemory-windows.c` | 474 | 27 | 22 | 0 | 5 | 0 |
| `src/backend/windows/hooxtls-windows.c` | 278 | 14 | 14 | 0 | 0 | 0 |
| `src/backend/x86/hooxcpu-x86.c` | 98 | 3 | 3 | 0 | 0 | 0 |
| `src/backend/x86/hooxcpucontext-x86.c` | 93 | 4 | 4 | 0 | 0 | 0 |
| `src/backend/x86/hooxinterceptor-x86.c` | 628 | 18 | 18 | 0 | 0 | 0 |
| `src/compat/hxarray.c` | 634 | 38 | 20 | 3 | 15 | 0 |
| `src/compat/hxhash.c` | 572 | 35 | 20 | 2 | 13 | 0 |
| `src/compat/hxlist.c` | 630 | 46 | 25 | 4 | 17 | 0 |
| `src/compat/hxmem.c` | 137 | 13 | 9 | 0 | 4 | 0 |
| `src/compat/hxstrfuncs.c` | 187 | 10 | 3 | 2 | 5 | 0 |
| `src/compat/hxstring.c` | 180 | 12 | 6 | 5 | 1 | 0 |
| `src/compat/hxthread.c` | 439 | 38 | 36 | 0 | 2 | 0 |
| `src/core/hoox.c` | 81 | 8 | 0 | 0 | 0 | 8 |
| `src/core/hooxcloak-stub.c` | 29 | 4 | 4 | 0 | 0 | 0 |
| `src/core/hooxcodeallocator.c` | 901 | 21 | 18 | 0 | 3 | 0 |
| `src/core/hooxcodesegment.c` | 73 | 9 | 8 | 0 | 1 | 0 |
| `src/core/hooxinterceptor.c` | 2558 | 85 | 61 | 0 | 1 | 23 |
| `src/core/hooxinvocationcontext.c` | 91 | 12 | 0 | 0 | 0 | 12 |
| `src/core/hooxinvocationlistener.c` | 178 | 12 | 5 | 0 | 0 | 7 |
| `src/core/hooxlibc.c` | 60 | 3 | 3 | 0 | 0 | 0 |
| `src/core/hooxmemory.c` | 1092 | 48 | 41 | 2 | 5 | 0 |
| `src/core/hooxmetalarray.c` | 137 | 10 | 7 | 0 | 3 | 0 |
| `src/core/hooxmetalhash.c` | 829 | 39 | 19 | 0 | 20 | 0 |
| `src/core/hooxspinlock.c` | 53 | 4 | 2 | 0 | 2 | 0 |
| `src/disasm/hx_disasm_x86.c` | 838 | 19 | 19 | 0 | 0 | 0 |

## 死代码函数索引（已编译文件）

已定义但在已编译库和测试中均无引用的函数。按文件分组；每个函数的作用见下方各子树明细。

- **`src/arch/x86/hooxx86reader.c`** (3): `hoox_x86_reader_insn_length`, `hoox_x86_reader_find_next_call_target`, `hoox_x86_reader_try_get_relative_call_target`
- **`src/arch/x86/hooxx86relocator.c`** (6): `hoox_x86_relocator_new`, `hoox_x86_relocator_ref`, `hoox_x86_relocator_unref`, `hoox_x86_relocator_skip_one_no_label`, `hoox_x86_relocator_write_one_no_label`, `hoox_x86_relocator_relocate`
- **`src/arch/x86/hooxx86writer.c`** (51): `hoox_x86_writer_new`, `hoox_x86_writer_cur`, `hoox_x86_writer_get_cpu_register_for_nth_argument`, `hoox_x86_writer_can_branch_directly_between`, `hoox_x86_writer_put_call_address_with_arguments_array`, `hoox_x86_writer_put_call_address_with_aligned_arguments_array`, `hoox_x86_writer_put_call_reg_with_arguments_array`, `hoox_x86_writer_put_call_reg_with_aligned_arguments`, `hoox_x86_writer_put_call_reg_with_aligned_arguments_array`, `hoox_x86_writer_put_call_reg_offset_ptr_with_arguments_array`, `hoox_x86_writer_put_call_reg_offset_ptr_with_aligned_arguments`, `hoox_x86_writer_put_call_reg_offset_ptr_with_aligned_arguments_array`, `hoox_x86_writer_put_leave`, `hoox_x86_writer_put_ret_imm`, `hoox_x86_writer_put_jmp_near_label`, `hoox_x86_writer_put_add_reg_near_ptr`, `hoox_x86_writer_put_sub_reg_reg`, `hoox_x86_writer_put_sub_reg_near_ptr`, `hoox_x86_writer_put_inc_reg_ptr`, `hoox_x86_writer_put_dec_reg_ptr`, `hoox_x86_writer_put_lock_cmpxchg_reg_ptr_reg`, `hoox_x86_writer_put_shr_reg_u8`, `hoox_x86_writer_put_xor_reg_reg`, `hoox_x86_writer_put_mov_reg_ptr_u32`, `hoox_x86_writer_put_mov_reg_base_index_scale_offset_ptr`, `hoox_x86_writer_put_mov_fs_u32_ptr_reg`, `hoox_x86_writer_put_mov_reg_fs_u32_ptr`, `hoox_x86_writer_put_mov_fs_reg_ptr_reg`, `hoox_x86_writer_put_mov_reg_fs_reg_ptr`, `hoox_x86_writer_put_mov_gs_u32_ptr_reg`, `hoox_x86_writer_put_mov_reg_gs_u32_ptr`, `hoox_x86_writer_put_mov_gs_reg_ptr_reg`, `hoox_x86_writer_put_mov_reg_gs_reg_ptr`, `hoox_x86_writer_put_movq_xmm0_esp_offset_ptr`, `hoox_x86_writer_put_movq_eax_offset_ptr_xmm0`, `hoox_x86_writer_put_movdqu_xmm0_esp_offset_ptr`, `hoox_x86_writer_put_movdqu_eax_offset_ptr_xmm0`, `hoox_x86_writer_put_push_imm_ptr`, `hoox_x86_writer_put_sahf`, `hoox_x86_writer_put_lahf`, `hoox_x86_writer_put_test_reg_u32`, `hoox_x86_writer_put_cmp_imm_ptr_imm_u32`, `hoox_x86_writer_put_cmp_reg_reg`, `hoox_x86_writer_put_clc`, `hoox_x86_writer_put_stc`, `hoox_x86_writer_put_std`, `hoox_x86_writer_put_cpuid`, `hoox_x86_writer_put_lfence`, `hoox_x86_writer_put_rdtsc`, `hoox_x86_writer_put_pause`, `hoox_x86_writer_put_padding`
- **`src/backend/windows/hoox_process-windows.c`** (5): `hoox_process_set_code_signing_policy`, `hoox_process_get_main_module`, `hoox_module_get_name`, `hoox_module_get_path`, `hoox_module_find_export_by_name`
- **`src/backend/windows/hooxmemory-windows.c`** (5): `hoox_memory_is_readable`, `hoox_memory_read`, `hoox_memory_write`, `hoox_memory_discard`, `hoox_memory_decommit`
- **`src/compat/hxarray.c`** (15): `hx_array_set_clear_func`, `hx_array_get_element_size`, `hx_array_ref`, `hx_array_append_vals`, `hx_array_prepend_vals`, `hx_array_remove_range`, `hx_array_steal`, `hx_ptr_array_set_free_func`, `hx_ptr_array_ref`, `hx_ptr_array_set_size`, `hx_ptr_array_remove`, `hx_ptr_array_remove_fast`, `hx_ptr_array_foreach`, `hx_ptr_array_find`, `hx_ptr_array_steal`
- **`src/compat/hxhash.c`** (13): `hx_hash_table_replace`, `hx_hash_table_lookup_extended`, `hx_hash_table_steal`, `hx_hash_table_ref`, `hx_hash_table_foreach`, `hx_hash_table_foreach_remove`, `hx_hash_table_find`, `hx_int_hash`, `hx_int_equal`, `hx_int64_hash`, `hx_int64_equal`, `hx_str_hash`, `hx_str_equal`
- **`src/compat/hxlist.c`** (17): `hx_slist_insert`, `hx_slist_reverse`, `hx_slist_free_1`, `hx_slist_free_full`, `hx_list_first`, `hx_list_insert`, `hx_list_remove`, `hx_list_nth_data`, `hx_list_length`, `hx_list_reverse`, `hx_list_free_full`, `hx_queue_clear`, `hx_queue_get_length`, `hx_queue_peek_head`, `hx_queue_peek_tail`, `hx_queue_foreach`, `hx_queue_remove`
- **`src/compat/hxmem.c`** (4): `hx_try_malloc`, `hx_try_malloc0`, `hx_slice_alloc0`, `hx_slice_copy`
- **`src/compat/hxstrfuncs.c`** (5): `hx_strndup`, `hx_strcmp0`, `hx_str_has_prefix`, `hx_str_has_suffix`, `hx_strlcat`
- **`src/compat/hxstring.c`** (1): `hx_string_assign`
- **`src/compat/hxthread.c`** (2): `hx_once_init_enter_impl`, `hx_once_init_leave_impl`
- **`src/core/hooxcodeallocator.c`** (3): `hoox_code_slice_ref`, `hoox_code_allocator_alloc_deflector`, `hoox_code_deflector_ref`
- **`src/core/hooxcodesegment.c`** (1): `hoox_code_segment_get_size`
- **`src/core/hooxinterceptor.c`** (1): `_hoox_interceptor_translate_top_return_address`
- **`src/core/hooxmemory.c`** (5): `hoox_sign_code_address`, `hoox_strip_code_address`, `hoox_query_ptrauth_support`, `hoox_memory_range_copy`, `hoox_memory_range_free`
- **`src/core/hooxmetalarray.c`** (3): `hoox_metal_array_insert_at`, `hoox_metal_array_remove_at`, `hoox_metal_array_get_extents`
- **`src/core/hooxmetalhash.c`** (20): `hoox_metal_hash_table_iter_init`, `hoox_metal_hash_table_iter_next`, `hoox_metal_hash_table_iter_get_hash_table`, `hoox_metal_hash_table_iter_remove`, `hoox_metal_hash_table_iter_replace`, `hoox_metal_hash_table_iter_steal`, `hoox_metal_hash_table_ref`, `hoox_metal_hash_table_destroy`, `hoox_metal_hash_table_lookup_extended`, `hoox_metal_hash_table_replace`, `hoox_metal_hash_table_add`, `hoox_metal_hash_table_contains`, `hoox_metal_hash_table_remove`, `hoox_metal_hash_table_steal`, `hoox_metal_hash_table_steal_all`, `hoox_metal_hash_table_foreach_remove`, `hoox_metal_hash_table_foreach_steal`, `hoox_metal_hash_table_foreach`, `hoox_metal_hash_table_find`, `hoox_metal_hash_table_size`
- **`src/core/hooxspinlock.c`** (2): `hoox_spinlock_init`, `hoox_spinlock_try_acquire`

> **关于死代码计数的说明。** 头部的 `162` 是*机械规则*的结果（在已编译 `.c` 中只出现一次、且测试中不出现）。下方各明细小节对其做了人工修正：有些函数只经**头文件宏**到达（例如 `hx_slice_new0` → `hx_slice_alloc0`），或被**尚未编译的 ARM/ARM64 backend 取地址**——这些被标为 *使用中* 或 *仅ARM* 而非真正死代码。请以逐函数的 **状态** 列为准，而非这个原始数字。

## 建议的清理分档（由你拍板）

**第 1 档 —— 立即删除，对 x86 构建零风险**
- `src/core/hooxreturnaddress.c` + `hooxreturnaddress.h` —— 未编译，且实际上*无法编译*（include 了已不存在的 `hooxsymbolutil.h`）。回溯/符号化功能已被放弃，可安全删除。
- `hooxmemory.c` 中残留的**内存扫描 / match-pattern** 痕迹（`HooxPointerScan*` 结构体 + SIMD 宏，没有函数），以及 `hoox_memory_range_copy/_free`（无任何引用）。
- `hoox_interceptor_detect_hook_size` —— 泄漏了 capstone 风格的签名，且公共头已不暴露它。

**第 2 档 —— 精简保留组件里的死表面（安全，缩小 amalgam）**
- **x86 writer**（`hooxx86writer.c`）：151 个里有 51 个死代码 + 25 个仅测试的指令发射器。hook 引擎只需要少数几个，其余是从 frida 搬来的完整汇编器。单文件最大收益。
- **metal-hash**（`hooxmetalhash.c`）：20 个死代码 —— 整套迭代器 / replace / steal / foreach API 都不被 hook 路径使用。
- **nano-glib 容器**：大量未使用的 GLib 操作（`hxlist` 17、`hxarray` 15、`hxhash` 13 个死代码）—— 裁剪到引擎实际调用的部分。
- **ptrauth 三件套**（`hoox_sign/strip_code_address`、`query_ptrauth_support`）：x86 上是空操作，仅被 ARM64 源码维持存活。
- 注意：*仅测试* 的函数也可以删除，但要连同使用它们的测试用例一起删。

**第 3 档 —— 策略决策：是否多架构**
- 那 14 个 ARM/ARM64 文件（约 11.3k 行，在 *未编译* 一节）是最大的一块。若 `docs/PLAN.md` 里的多架构路线仍然成立就保留；若 hoox 定为仅 x86/x64 就删除（或移到独立分支）。少数在 x86 下为死代码的函数（如 `hoox_code_allocator_alloc_deflector`、`hoox_metal_array_remove_at`）*仅*用于服务这些文件 —— 激活 ARM 时会复活，应与它们一并处理。

---

# 逐函数明细表
## compat/ — nano-glib（GLib 兼容层）

`src/compat/` 是 hoox 对 GLib 的仓库内替代实现（决策 D1）：一个无依赖、纯 C 的重写，覆盖机械提取出的 frida-gum hook 引擎所依赖的那部分 GLib 类型、宏、内存/原子/线程原语以及容器。提取的源码使用 `hxglib.h` 替代 `<glib.h>`；所有符号都在 `hx_` 前缀下沿用 GLib 的名称，使引擎在逻辑不变的情况下即可编译。这些符号都不属于公共 `hoox_*` API（全部 `public==false`），因此状态为 使用中 / 仅测试 / 死代码——绝不会是「公共API」。由于该层是从 GLib 整体移植而来，大部分接口虽被携带进来，却从未被提取出的引擎调用。

状态图例：**使用中** = 在库内被调用；**仅测试** = 仅被 `tests/` 使用；**死代码** = 在库或测试中均未被引用。仅通过头文件宏或函数指针到达的函数计为已使用（经 grep 核实），即使其原始引用计数为 1。

### `src/compat/hxmem.c` — 已编译 ✅ · 137 loc · 13 funcs
_malloc/free 家族 + hx_slice_*（映射到普通 malloc/free）+ memdup；OOM 时中止。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hx_oom` | 报告内存耗尽并中止 | 使用中 |
| `hx_malloc` | 分配 n 字节，失败时中止 | 使用中 |
| `hx_malloc0` | 清零分配，失败时中止 | 使用中 |
| `hx_realloc` | 调整分配大小（为 0 则释放） | 使用中 |
| `hx_try_malloc` | 分配但失败时不中止 | 死代码 |
| `hx_try_malloc0` | 清零分配但不中止 | 死代码 |
| `hx_free` | 释放内存 | 使用中 |
| `hx_memdup` | 复制缓冲区（32 位大小） | 使用中 |
| `hx_memdup2` | 复制缓冲区（size_t 大小） | 使用中 |
| `hx_slice_alloc` | slice 分配 → malloc（经 `hx_slice_new`） | 使用中 |
| `hx_slice_alloc0` | 清零 slice 分配（经 `hx_slice_new0`） | 使用中 |
| `hx_slice_copy` | 复制块（经 `hx_slice_dup`） | 使用中 |
| `hx_slice_free1` | slice 释放 → free（经 `hx_slice_free`） | 使用中 |

### `src/compat/hxthread.c` — 已编译 ✅ · 439 loc · 38 funcs
_HxMutex/HxRecMutex/HxPrivate + 一次性初始化；Win32 SRWLOCK/FLS 或 POSIX pthread。_

备注：15 个 mutex/private 操作各有两份定义（一个 `_WIN32` 分支与一个 POSIX 分支），因此审计计为 38 个，而实际有 23 个不同函数（下方各列出一次）；平台专用的静态辅助函数为单分支。由于双重定义会抬高原始引用计数，此处的状态反映的是经核实的调用点，而非机械的 `refs<=1` 判定。

| 函数 | 作用 | 状态 |
|---|---|---|
| `hx_once_init_enter_impl` | 开始一次性初始化（持有全局锁） | 使用中 |
| `hx_once_init_leave_impl` | 发布结果，释放初始化锁 | 使用中 |
| `hx_thread_self` | 当前线程 id 作为不透明指针 | 仅测试 |
| `hx_usleep` | 睡眠微秒 | 仅测试 |
| `hx_mutex_init` | 初始化 mutex | 仅测试 |
| `hx_mutex_clear` | 销毁 mutex | 仅测试 |
| `hx_mutex_lock` | 获取 mutex | 使用中 |
| `hx_mutex_trylock` | 尝试获取 mutex | 死代码 |
| `hx_mutex_unlock` | 释放 mutex | 使用中 |
| `hx_rec_mutex_init` | 初始化递归 mutex | 使用中 |
| `hx_rec_mutex_clear` | 销毁递归 mutex | 使用中 |
| `hx_rec_mutex_lock` | 获取递归 mutex | 使用中 |
| `hx_rec_mutex_trylock` | 尝试获取递归 mutex | 使用中 |
| `hx_rec_mutex_unlock` | 释放递归 mutex | 使用中 |
| `hx_private_get` | 读取线程局部值 | 使用中 |
| `hx_private_set` | 写入线程局部值 | 使用中 |
| `hx_private_replace` | 替换线程局部值，通知旧值 | 死代码 |
| `hx_private_fls_cb` | FLS 每线程析构器（Win32 静态） | 使用中 |
| `hx_private_index` | 惰性分配 FLS 槽（Win32 静态） | 使用中 |
| `hx_private_slot` | 获取/创建每线程槽（Win32 静态） | 使用中 |
| `hx_mutex_get` | 惰性初始化底层 pthread mutex（POSIX 静态） | 使用中 |
| `hx_rec_get` | 惰性初始化递归 pthread mutex（POSIX 静态） | 使用中 |
| `hx_private_key` | 惰性初始化 pthread key（POSIX 静态） | 使用中 |

### `src/compat/hxarray.c` — 已编译 ✅ · 634 loc · 38 funcs
_HxArray（带类型的字节数组）与 HxPtrArray（指针数组），布局与 GLib 兼容。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hx_array_maybe_expand` | 扩展数组容量（静态） | 使用中 |
| `hx_array_zero_terminate` | 将尾部元素清零（静态） | 使用中 |
| `hx_array_sized_new` | 创建带预留大小的数组 | 使用中 |
| `hx_array_new` | 创建数组 | 使用中 |
| `hx_array_set_clear_func` | 设置每元素清理回调 | 死代码 |
| `hx_array_get_element_size` | 返回元素大小 | 死代码 |
| `hx_array_call_clear` | 在范围上调用清理函数（静态） | 使用中 |
| `hx_array_free` | 释放数组，可选保留数据 | 使用中 |
| `hx_array_ref` | 增加引用计数 | 死代码 |
| `hx_array_unref` | 减少引用计数，为零时释放 | 使用中 |
| `hx_array_append_vals` | 追加元素（经 `hx_array_append_val`） | 使用中 |
| `hx_array_prepend_vals` | 前置元素 | 死代码 |
| `hx_array_insert_vals` | 在索引处插入元素 | 使用中 |
| `hx_array_set_size` | 增长/缩小元素数 | 使用中 |
| `hx_array_remove_index` | 移除元素，向下移位 | 仅测试 |
| `hx_array_remove_index_fast` | 通过与末尾交换移除元素 | 仅测试 |
| `hx_array_remove_range` | 移除元素范围 | 死代码 |
| `hx_array_steal` | 分离并返回底层缓冲区 | 死代码 |
| `hx_ptr_array_maybe_expand` | 扩展指针数组（静态） | 使用中 |
| `hx_ptr_array_sized_new` | 创建带预留的指针数组 | 使用中 |
| `hx_ptr_array_new` | 创建指针数组 | 使用中 |
| `hx_ptr_array_new_with_free_func` | 创建带元素释放器的指针数组 | 仅测试 |
| `hx_ptr_array_new_full` | 创建指针数组，预留 + 释放器 | 使用中 |
| `hx_ptr_array_set_free_func` | 设置元素释放回调 | 死代码 |
| `hx_ptr_array_free` | 释放指针数组，可选保留数据 | 使用中 |
| `hx_ptr_array_ref` | 增加引用计数 | 死代码 |
| `hx_ptr_array_unref` | 减少引用计数，为零时释放 | 使用中 |
| `hx_ptr_array_add` | 追加指针 | 使用中 |
| `hx_ptr_array_set_size` | 调整大小，释放被移除的尾部 | 死代码 |
| `hx_ptr_array_remove_index` | 移除指针，向下移位 | 使用中 |
| `hx_ptr_array_remove_index_fast` | 通过与末尾交换移除指针 | 使用中 |
| `hx_ptr_array_remove` | 查找并移除指针 | 死代码 |
| `hx_ptr_array_remove_fast` | 查找并快速移除指针 | 死代码 |
| `hx_ptr_array_foreach` | 对每个指针调用函数 | 死代码 |
| `hx_ptr_sort_thunk` | qsort 比较器适配器（静态） | 使用中 |
| `hx_ptr_array_sort` | 通过比较器排序指针 | 使用中 |
| `hx_ptr_array_find` | 定位指针，返回索引 | 死代码 |
| `hx_ptr_array_steal` | 分离并返回底层缓冲区 | 死代码 |

### `src/compat/hxhash.c` — 已编译 ✅ · 572 loc · 35 funcs
_HxHashTable：采用分离链接法的哈希映射，API 与 GLib 兼容，含迭代器、hash/equal 辅助函数。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hx_hash_key` | 计算键的哈希（静态） | 使用中 |
| `hx_keys_equal` | 比较键（静态） | 使用中 |
| `hx_hash_table_new` | 创建表 | 使用中 |
| `hx_hash_table_new_full` | 创建带销毁函数的表 | 使用中 |
| `hx_hash_resize` | 桶数翻倍，重新哈希（静态） | 使用中 |
| `hx_hash_find` | 定位键对应的节点（静态） | 使用中 |
| `hx_hash_insert` | 插入/替换节点（静态） | 使用中 |
| `hx_hash_table_insert` | 插入（保留已有键） | 使用中 |
| `hx_hash_table_replace` | 插入（替换键） | 死代码 |
| `hx_hash_table_add` | 将键作为键+值添加 | 使用中 |
| `hx_hash_table_lookup` | 按键查找值 | 使用中 |
| `hx_hash_table_lookup_extended` | 查找键与值 | 死代码 |
| `hx_hash_table_contains` | 测试键是否存在 | 使用中 |
| `hx_hash_remove_internal` | 移除节点，可选通知（静态） | 使用中 |
| `hx_hash_table_remove` | 移除键，运行销毁函数 | 使用中 |
| `hx_hash_table_steal` | 移除键但不通知 | 死代码 |
| `hx_hash_table_remove_all` | 清空所有条目 | 使用中 |
| `hx_hash_table_ref` | 增加引用计数 | 死代码 |
| `hx_hash_table_unref` | 减少引用计数，为零时释放 | 使用中 |
| `hx_hash_table_destroy` | 清空后 unref 表 | 仅测试 |
| `hx_hash_table_foreach` | 对每个条目调用函数 | 死代码 |
| `hx_hash_table_foreach_remove` | 移除谓词为真的条目 | 死代码 |
| `hx_hash_table_find` | 返回首个匹配谓词的值 | 死代码 |
| `hx_hash_table_size` | 条目数量 | 使用中 |
| `hx_hash_table_iter_init` | 初始化迭代器 | 使用中 |
| `hx_hash_table_iter_next` | 推进迭代器 | 使用中 |
| `hx_hash_table_iter_remove` | 移除当前迭代器条目 | 使用中 |
| `hx_direct_hash` | 指针身份哈希 | 使用中 |
| `hx_direct_equal` | 指针身份相等判定 | 仅测试 |
| `hx_int_hash` | 对 int 键哈希 | 死代码 |
| `hx_int_equal` | 比较 int 键 | 死代码 |
| `hx_int64_hash` | 对 64 位键哈希 | 死代码 |
| `hx_int64_equal` | 比较 64 位键 | 死代码 |
| `hx_str_hash` | djb2 字符串哈希 | 死代码 |
| `hx_str_equal` | 比较字符串 | 死代码 |

### `src/compat/hxlist.c` — 已编译 ✅ · 630 loc · 46 funcs
_HxSList（单向链表）、HxList（双向链表），以及基于 HxList 节点的 HxQueue。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hx_slist_append` | 向单向链表追加节点 | 使用中 |
| `hx_slist_prepend` | 向单向链表前置节点 | 使用中 |
| `hx_slist_insert` | 在指定位置插入 | 死代码 |
| `hx_slist_remove` | 移除首个含该数据的节点 | 使用中 |
| `hx_slist_remove_link` | 断开节点但不释放 | 使用中 |
| `hx_slist_delete_link` | 断开并释放节点 | 使用中 |
| `hx_slist_find` | 按数据查找节点 | 使用中 |
| `hx_slist_last` | 返回末尾节点 | 使用中 |
| `hx_slist_nth` | 返回第 n 个节点 | 使用中 |
| `hx_slist_nth_data` | 返回第 n 个节点的数据 | 仅测试 |
| `hx_slist_length` | 计数节点 | 仅测试 |
| `hx_slist_reverse` | 原地反转链表 | 死代码 |
| `hx_slist_foreach` | 对每个节点调用函数 | 使用中 |
| `hx_slist_free_1` | 释放单个节点 | 死代码 |
| `hx_slist_free` | 释放整个链表 | 使用中 |
| `hx_slist_free_full` | 释放链表，对数据调用释放器 | 死代码 |
| `hx_list_last` | 返回末尾节点 | 使用中 |
| `hx_list_first` | 返回首个节点 | 死代码 |
| `hx_list_append` | 向双向链表追加节点 | 使用中 |
| `hx_list_prepend` | 向双向链表前置节点 | 使用中 |
| `hx_list_insert` | 在指定位置插入 | 死代码 |
| `hx_list_remove_link` | 断开节点但不释放 | 使用中 |
| `hx_list_delete_link` | 断开并释放节点 | 使用中 |
| `hx_list_remove` | 移除首个含该数据的节点 | 死代码 |
| `hx_list_find` | 按数据查找节点 | 使用中 |
| `hx_list_nth` | 返回第 n 个节点 | 使用中 |
| `hx_list_nth_data` | 返回第 n 个节点的数据 | 死代码 |
| `hx_list_length` | 计数节点 | 死代码 |
| `hx_list_reverse` | 原地反转链表 | 死代码 |
| `hx_list_foreach` | 对每个节点调用函数 | 使用中 |
| `hx_list_free` | 释放整个链表 | 使用中 |
| `hx_list_free_full` | 释放链表，对数据调用释放器 | 死代码 |
| `hx_queue_new` | 分配空队列 | 使用中 |
| `hx_queue_init` | 原地初始化队列 | 使用中 |
| `hx_queue_clear` | 释放节点，重置队列 | 死代码 |
| `hx_queue_free` | 释放队列及节点 | 使用中 |
| `hx_queue_is_empty` | 测试是否为空 | 使用中 |
| `hx_queue_get_length` | 返回元素数 | 死代码 |
| `hx_queue_push_head` | 压入队首 | 仅测试 |
| `hx_queue_push_tail` | 压入队尾 | 使用中 |
| `hx_queue_pop_head` | 从队首弹出 | 使用中 |
| `hx_queue_pop_tail` | 从队尾弹出 | 仅测试 |
| `hx_queue_peek_head` | 查看队首数据 | 死代码 |
| `hx_queue_peek_tail` | 查看队尾数据 | 死代码 |
| `hx_queue_foreach` | 对每个节点调用函数 | 死代码 |
| `hx_queue_remove` | 移除含该数据的节点 | 死代码 |

### `src/compat/hxstring.c` — 已编译 ✅ · 180 loc · 12 funcs
_HxString：可增长、始终以 NUL 结尾的字符串缓冲区。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hx_string_maybe_expand` | 扩展缓冲区容量（静态） | 使用中 |
| `hx_string_sized_new` | 以初始容量创建 | 使用中 |
| `hx_string_new` | 从 C 字符串创建 | 仅测试 |
| `hx_string_free` | 释放，可选返回段 | 仅测试 |
| `hx_string_truncate` | 截短到指定长度 | 使用中 |
| `hx_string_assign` | 用 C 字符串替换内容 | 死代码 |
| `hx_string_append_len` | 追加 n 字节 | 使用中 |
| `hx_string_append` | 追加 C 字符串 | 使用中 |
| `hx_string_append_c` | 追加一个字符 | 仅测试 |
| `hx_string_prepend` | 前置 C 字符串 | 仅测试 |
| `hx_string_append_vprintf` | 追加格式化内容（va_list） | 使用中 |
| `hx_string_append_printf` | 追加格式化内容（varargs） | 仅测试 |

### `src/compat/hxstrfuncs.c` — 已编译 ✅ · 187 loc · 10 funcs
_hx_strdup 家族 + 字符串谓词及有界复制/拼接。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hx_strdup` | 复制 C 字符串 | 使用中 |
| `hx_strndup` | 复制最多 n 字节 | 死代码 |
| `hx_strdup_vprintf` | 分配格式化字符串（va_list） | 使用中 |
| `hx_strdup_printf` | 分配格式化字符串（varargs） | 仅测试 |
| `hx_strconcat` | 拼接以 NULL 结尾的参数 | 仅测试 |
| `hx_strcmp0` | NULL 安全的 strcmp | 死代码 |
| `hx_str_has_prefix` | 测试字符串前缀 | 死代码 |
| `hx_str_has_suffix` | 测试字符串后缀 | 死代码 |
| `hx_strlcpy` | 有界复制，返回 src 长度 | 使用中 |
| `hx_strlcat` | 有界拼接，返回总长度 | 死代码 |

### 头文件

| 头文件 | 作用 |
|---|---|
| `hxglib.h` | 总括头文件；被提取源码用以替代 `<glib.h>` |
| `hxdefs.h` | 基础类型与工具宏（`hx_int`、`hx_pointer`、`HX_BEGIN_DECLS`、MIN/MAX 等） |
| `hxmessages.h` | 断言/日志（`hx_assert`、`hx_return_*`、`hx_error`）；致命路径中止，无 GLib 日志域 |
| `hxmem.h` | 内存 API + `hx_new`/`hx_new0`/`hx_slice_*` 宏 |
| `hxatomic.h` | 基于 `__atomic`/`_Interlocked*` 的内联原子操作（`hx_atomic_int_*`/`hx_atomic_pointer_*`） |
| `hxthread.h` | HxMutex/HxRecMutex/HxPrivate 类型 + `hx_once_init_*` 宏 |
| `hxarray.h` | HxArray/HxPtrArray 类型，布局与 GLib 兼容，`hx_array_*_val`/index 宏 |
| `hxhash.h` | HxHashTable 与 HxHashTableIter 声明 |
| `hxlist.h` | HxSList/HxList/HxQueue 节点结构体与遍历宏 |
| `hxstring.h` | HxString 结构体与缓冲区 API |
| `hxstrfuncs.h` | 字符串函数声明 + `hx_snprintf` 宏 |
## core/ — 引擎支撑（容器、分配器、监听器、初始化）

这些文件是 hook 引擎中非 interceptor 的支撑骨架：库的启动引导
（`hoox.c`）、面向回调的监听器/调用上下文 API、可执行 trampoline
分配器、免 malloc 的侵入式容器，以及若干独立的小型原语（自旋锁、mem*、
cloak 桩）。它们大多是从 frida-gum 机械提取而来，因此有些文件带有完整的
glib 风格 API 表面，即便 Windows-x64 的 hook 路径只用到其中一小部分——
这也是 `hooxmetalhash.c` 中出现大量死代码行的原因。少数函数被判为死代码，
仅仅是因为其唯一调用方位于尚未编译的 ARM/ARM64 源文件中；这些均已在行内标注。

_状态图例：_ **公共API**（位于 `include/hoox.h`）· **使用中**（在已编译库中被调用）
· **死代码**（非公共、在已编译构建中未被调用、无测试引用）· **仅测试**
（未被调用但被测试引用）。

### `src/core/hoox.c` — 已编译 ✅ · 81 loc · 8 funcs
_库初始化/反初始化：启动堆、TLS 和 interceptor 全局状态。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_init` | 幂等启动：堆引用、TLS、interceptor 初始化 | 公共API |
| `hoox_deinit` | 最后一个引用释放时拆除 interceptor、TLS、堆 | 公共API |
| `hoox_init_embedded` | 嵌入模式初始化；转发到 `hoox_init` | 公共API |
| `hoox_deinit_embedded` | 嵌入模式反初始化；转发到 `hoox_deinit` | 公共API |
| `hoox_shutdown` | 空操作关闭钩子（frida ABI 兼容） | 公共API |
| `hoox_prepare_to_fork` | 冻结 interceptor 全局锁、实例锁与线程上下文表 | 公共API |
| `hoox_recover_from_fork_in_parent` | fork 后在父进程释放准备阶段持有的锁 | 公共API |
| `hoox_recover_from_fork_in_child` | 清理消失线程的上下文/使用计数并释放继承锁 | 公共API |

### `src/core/hooxcloak-stub.c` — 已编译 ✅ · 29 loc · 4 funcs
_frida 线程/范围 cloak 的空操作桩（不隐藏内存分配）。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `_hoox_cloak_init` | 空操作 cloak 初始化桩 | 使用中 |
| `_hoox_cloak_deinit` | 空操作 cloak 反初始化桩 | 使用中 |
| `hoox_cloak_add_range` | 空操作；上游用它对扫描隐藏内存分配 | 使用中 |
| `hoox_cloak_remove_range` | 空操作范围取消隐藏桩 | 使用中 |

### `src/core/hooxcodeallocator.c` — 已编译 ✅ · 901 loc · 21 funcs
_可执行 trampoline 切片分配器；deflector/code-cave 子系统面向 ARM/Darwin。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_code_allocator_init` | 配置切片/批次尺寸；初始化页记账 | 使用中 |
| `hoox_code_allocator_free` | 释放 dispatcher、页、哈希表 | 使用中 |
| `hoox_code_allocator_alloc_slice` | 分配一个 trampoline 切片（无局部性要求） | 使用中 |
| `hoox_code_allocator_try_alloc_slice_near` | 在地址附近按对齐查找/分配切片 | 使用中 |
| `hoox_code_allocator_commit` | 实现/保护页为 RX，对脏页刷新 icache | 使用中 |
| `hoox_code_allocator_try_alloc_batch_near` | 分配页批次，切分成切片 | 使用中 |
| `hoox_code_pages_unref` | 页引用计数；最后一个切片释放时释放/解映射 | 使用中 |
| `hoox_code_slice_ref` | 切片引用计数；从不被调用（仅使用 slice_unref） | 死代码 |
| `hoox_code_slice_unref` | 释放切片；回收到空闲列表或对页 unref | 使用中 |
| `hoox_code_slice_is_near` | 测试切片是否在规范的最大距离内 | 使用中 |
| `hoox_code_slice_is_aligned` | 测试切片 pc 对齐 | 使用中 |
| `hoox_code_allocator_alloc_deflector` | 分配 code-cave deflector；仅 ARM/ARM64 后端（未编译）调用 | 死代码 |
| `hoox_code_deflector_ref` | deflector 引用计数；从不被调用 | 死代码 |
| `hoox_code_deflector_unref` | 释放 deflector；调用方消失时释放 dispatcher | 使用中 |
| `hoox_code_deflector_dispatcher_new` | 构建 code-cave dispatcher+thunk（仅 Darwin/ELF32） | 使用中 |
| `hoox_code_deflector_dispatcher_free` | 恢复 cave，释放 thunk 和调用方 | 使用中 |
| `hoox_insert_deflector` | 向 code cave 写入分支（ARM/ARM64） | 使用中 |
| `hoox_write_thunk` | 发射保存寄存器的 thunk，调用 dispatcher 查找 | 使用中 |
| `hoox_remove_deflector` | 恢复原始 cave 字节 | 使用中 |
| `hoox_code_deflector_dispatcher_lookup` | 将返回地址映射到每个调用方的目标 | 使用中 |
| `hoox_probe_module_for_code_cave` | 扫描模块头以寻找可用的空 code cave | 使用中 |

### `src/core/hooxcodesegment.c` — 已编译 ✅ · 73 loc · 9 funcs
_W^X 代码页抽象；此构建编译的是不支持（非越狱）的桩。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_code_segment_is_supported` | 报告代码段支持情况（本平台为 FALSE） | 使用中 |
| `hoox_code_segment_new` | 桩；返回 NULL | 使用中 |
| `hoox_code_segment_free` | 空操作释放桩 | 使用中 |
| `hoox_code_segment_get_address` | 桩；返回 NULL | 使用中 |
| `hoox_code_segment_get_size` | 桩尺寸取值器；本平台从不被调用 | 死代码 |
| `hoox_code_segment_get_virtual_size` | 桩；返回 0 | 使用中 |
| `hoox_code_segment_realize` | 空操作 realize 桩 | 使用中 |
| `hoox_code_segment_map` | 空操作 map 桩 | 使用中 |
| `hoox_code_segment_mark` | 桩；设置 NOT_SUPPORTED 错误 | 使用中 |

### `src/core/hooxinvocationcontext.c` — 已编译 ✅ · 91 loc · 12 funcs
_对每次调用的调用上下文的公共访问器（对后端/CPU 上下文的薄委托）。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_invocation_context_get_point_cut` | 经后端返回 enter/leave 切点 | 公共API |
| `hoox_invocation_context_get_nth_argument` | 从 CPU 上下文读取第 n 个调用参数 | 公共API |
| `hoox_invocation_context_replace_nth_argument` | 覆写第 n 个调用参数 | 公共API |
| `hoox_invocation_context_get_return_value` | 从 CPU 上下文读取返回值 | 公共API |
| `hoox_invocation_context_replace_return_value` | 覆写返回值 | 公共API |
| `hoox_invocation_context_get_return_address` | 返回栈顶调用方保存的返回地址 | 公共API |
| `hoox_invocation_context_get_thread_id` | 经后端返回当前线程 id | 公共API |
| `hoox_invocation_context_get_depth` | 经后端返回嵌套深度 | 公共API |
| `hoox_invocation_context_get_listener_thread_data` | 获取/分配每线程监听器暂存区 | 公共API |
| `hoox_invocation_context_get_listener_function_data` | 返回每函数监听器数据 | 公共API |
| `hoox_invocation_context_get_listener_invocation_data` | 获取/分配每次调用的监听器数据 | 公共API |
| `hoox_invocation_context_get_replacement_data` | 返回 replace 模式数据 | 公共API |

### `src/core/hooxinvocationlistener.c` — 已编译 ✅ · 178 loc · 12 funcs
_面向回调的监听器接口，加上内建的 call/probe 监听器工厂。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_invocation_listener_init` | 初始化监听器 vtable、引用计数、终结器 | 公共API |
| `hoox_invocation_listener_ref` | 递增监听器引用计数 | 公共API |
| `hoox_invocation_listener_unref` | 递减引用计数；归零时终结+释放 | 公共API |
| `hoox_invocation_listener_on_enter` | 经接口分发 on_enter | 公共API |
| `hoox_invocation_listener_on_leave` | 经接口分发 on_leave | 公共API |
| `hoox_call_listener_on_enter` | 将 on_enter 转发给 call 回调的适配器 | 使用中 |
| `hoox_call_listener_on_leave` | 将 on_leave 转发给回调的适配器 | 使用中 |
| `hoox_call_listener_finalize` | 销毁 call 监听器用户数据 | 使用中 |
| `hoox_make_call_listener` | 创建基于 enter/leave 回调的监听器 | 公共API |
| `hoox_probe_listener_on_enter` | 将命中转发给 probe 回调的适配器 | 使用中 |
| `hoox_probe_listener_finalize` | 销毁 probe 监听器用户数据 | 使用中 |
| `hoox_make_probe_listener` | 创建单回调 probe 监听器 | 公共API |

### `src/core/hooxlibc.c` — 已编译 ✅ · 60 loc · 3 funcs
_在须避免 CRT 处使用的独立 libc 片段。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_memset` | 独立 memset（无 libc 依赖） | 使用中 |
| `hoox_memcpy` | 独立 memcpy | 使用中 |
| `hoox_memmove` | 独立的重叠安全 memmove | 使用中 |

### `src/core/hooxmetalarray.c` — 已编译 ✅ · 137 loc · 10 funcs
_免 malloc、以页为后备的动态数组（侵入式容器）。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_metal_array_init` | 初始化页后备数组；容量取自页大小 | 使用中 |
| `hoox_metal_array_free` | 释放后备页，重置字段 | 使用中 |
| `hoox_metal_array_element_at` | 返回索引处元素的指针 | 使用中 |
| `hoox_metal_array_insert_at` | 移位插入；hook 路径只追加，从不插入 | 死代码 |
| `hoox_metal_array_remove_at` | 移位删除；仅 ARM thumbwriter（未编译）调用 | 死代码 |
| `hoox_metal_array_remove_all` | 将长度重置为零 | 使用中 |
| `hoox_metal_array_append` | 增长并返回新元素的槽位 | 使用中 |
| `hoox_metal_array_get_extents` | 报告页分配范围；hook 路径未使用 | 死代码 |
| `hoox_metal_array_ensure_capacity` | 增长后备页，复制现有元素 | 使用中 |
| `hoox_round_up_to_page_size` | 将尺寸向上取整到页边界 | 使用中 |

### `src/core/hooxmetalhash.c` — 已编译 ✅ · 829 loc · 39 funcs
_免 malloc 的开放寻址哈希表（glib GHashTable 移植）。已编译的 hook 路径（x86 writer）只用到 new/insert/lookup/remove_all/unref；其余 glib API 表面都是死重。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_metal_hash_table_set_shift` | 由 shift 设置表大小/模/掩码 | 使用中 |
| `hoox_metal_hash_table_find_closest_shift` | 计算可覆盖 n 个条目的 shift | 使用中 |
| `hoox_metal_hash_table_set_shift_from_size` | 由期望大小推导 shift | 使用中 |
| `hoox_metal_hash_table_lookup_node` | 对键的槽位做开放寻址探测 | 使用中 |
| `hoox_metal_hash_table_remove_node` | 将槽位标记为墓碑，运行销毁通知 | 使用中 |
| `hoox_metal_hash_table_remove_all_nodes` | 清空所有槽位，可选地发出通知 | 使用中 |
| `hoox_metal_hash_table_resize` | 重哈希到调整大小后的数组 | 使用中 |
| `hoox_metal_hash_table_maybe_resize` | 越过负载因子阈值时调整大小 | 使用中 |
| `hoox_metal_hash_table_new` | 创建表（hash+equal 函数） | 使用中 |
| `hoox_metal_hash_table_new_full` | 创建带销毁通知器的表 | 使用中 |
| `hoox_metal_hash_table_iter_init` | glib 风格迭代器初始化；hook 路径从不迭代 | 死代码 |
| `hoox_metal_hash_table_iter_next` | 迭代器前进；未使用 | 死代码 |
| `hoox_metal_hash_table_iter_get_hash_table` | 迭代器的表访问器；未使用 | 死代码 |
| `iter_remove_or_steal` | 删除迭代器位置处的节点 | 使用中 |
| `hoox_metal_hash_table_iter_remove` | 迭代器删除；未使用 | 死代码 |
| `hoox_metal_hash_table_insert_node` | 在槽位放置/替换键值 | 使用中 |
| `hoox_metal_hash_table_iter_replace` | 迭代器替换值；未使用 | 死代码 |
| `hoox_metal_hash_table_iter_steal` | 迭代器窃取（无通知删除）；未使用 | 死代码 |
| `hoox_metal_hash_table_ref` | 引用计数递增；从不被调用 | 死代码 |
| `hoox_metal_hash_table_unref` | 递减引用计数；归零时释放表 | 使用中 |
| `hoox_metal_hash_table_destroy` | 清空+unref 便捷函数；未使用 | 死代码 |
| `hoox_metal_hash_table_lookup` | 返回键对应的值（或 NULL） | 使用中 |
| `hoox_metal_hash_table_lookup_extended` | 查找并返回原始键+值；未使用 | 死代码 |
| `hoox_metal_hash_table_insert_internal` | 查找槽位并插入节点 | 使用中 |
| `hoox_metal_hash_table_insert` | 插入/覆写键值对 | 使用中 |
| `hoox_metal_hash_table_replace` | 保留新键的插入；未使用 | 死代码 |
| `hoox_metal_hash_table_add` | 将键作为其自身值加入；未使用 | 死代码 |
| `hoox_metal_hash_table_contains` | 成员测试；未使用 | 死代码 |
| `hoox_metal_hash_table_remove_internal` | 查找槽位并删除节点 | 使用中 |
| `hoox_metal_hash_table_remove` | 单键删除；未使用（使用的是 remove_all） | 死代码 |
| `hoox_metal_hash_table_steal` | 无通知的单键删除；未使用 | 死代码 |
| `hoox_metal_hash_table_remove_all` | 删除每个条目，可能收缩 | 使用中 |
| `hoox_metal_hash_table_steal_all` | 无通知清空所有；未使用 | 死代码 |
| `hoox_metal_hash_table_foreach_remove_or_steal` | 删除谓词匹配的条目 | 使用中 |
| `hoox_metal_hash_table_foreach_remove` | 基于谓词的批量删除；未使用 | 死代码 |
| `hoox_metal_hash_table_foreach_steal` | 基于谓词的批量窃取；未使用 | 死代码 |
| `hoox_metal_hash_table_foreach` | 访问每个条目；未使用 | 死代码 |
| `hoox_metal_hash_table_find` | 谓词搜索；未使用 | 死代码 |
| `hoox_metal_hash_table_size` | 条目计数；未使用 | 死代码 |

### `src/core/hooxspinlock.c` — 已编译 ✅ · 53 loc · 4 funcs
_基于一个原子 int 的最小 CAS 自旋锁。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_spinlock_init` | 零初始化自旋锁；未使用（改用静态初始化器） | 死代码 |
| `hoox_spinlock_acquire` | 自旋直到 CAS 获取锁 | 使用中 |
| `hoox_spinlock_try_acquire` | 非阻塞获取尝试；从不被调用 | 死代码 |
| `hoox_spinlock_release` | 原子地释放锁 | 使用中 |

### 头文件

上述文件所拥有的头文件（interceptor/memory 头文件属于其它章节，此处省略）：

| 头文件 | 作用 |
|---|---|
| `src/core/hoox.h` | 核心总括头：init/deinit + interceptor/listener/context/memory 的 include |
| `src/core/hooxcloak.h` | 公共 cloak API（添加/删除范围） |
| `src/core/hooxcloak-priv.h` | 内部 cloak 初始化/反初始化声明 |
| `src/core/hooxcodeallocator.h` | 可执行切片/deflector 分配器 API + 结构体 |
| `src/core/hooxcodesegment.h` | W^X 代码段抽象 API |
| `src/core/hooxinvocationcontext.h` | 调用上下文访问器 + 结构体/后端 vtable |
| `src/core/hooxinvocationlistener.h` | 调用监听器接口 + 工厂 API |
| `src/core/hooxlibc.h` | 独立 mem* 声明 |
| `src/core/hooxmetalarray.h` | 页后备动态数组 API |
| `src/core/hooxmetalhash.h` | 免 malloc 的 GHashTable 风格容器 API |
| `src/core/hooxspinlock.h` | 自旋锁类型 + API |

_备注：_ `hoox_code_allocator_alloc_deflector`、`hoox_metal_array_remove_at`
被严格依据已编译构建规则标记为死代码——它们唯一的调用点位于
`src/backend/arm{,64}/hooxinterceptor-arm*.c` 和
`src/arch/arm/hooxthumbwriter.c` 中，而这些在 Windows-x64 切片中未被编译。
一旦启用那些架构，它们将变为使用中。
## core/ — hook 引擎与内存

状态源自 `_audit.json`（`refs` = 在**已编译**的 .c 主体中出现的次数，
因此 `refs<=1` = 在编译/x86 集合中已定义但从未被调用；`tref` = 测试中
出现的次数；`public` = 存在于 `include/hoox.h` facade 中）。

> **跨架构注意事项。** 本审计是在已编译（Windows/x86）的 TU 集合上进行的，
> 因此 `src/arch/arm*`、`src/arch/arm64/*` 和 `src/backend/arm*` **未被**
> 计入。若干按规则被标记为死代码的行，实际上是被那些未编译的后端取地址/调用的
> （已通过 Grep 验证）——已逐行注明。只有两个
> 函数是真正的死代码，*没有*任何地方引用。

### `src/core/hooxinterceptor.c` — 已编译 ✅ · 2558 loc · 85 funcs
_Inline-hook 引擎：attach/replace/probe、事务、每线程调用栈、trampoline 进入/离开分发。_

| 函数 | 作用 | 状态 |
|---|---|---|
| `_hoox_interceptor_init` | 在库初始化时分配线程上下文表 + guard TLS key | 使用中 |
| `_hoox_interceptor_deinit` | 释放 guard key + 线程上下文表 | 使用中 |
| `hoox_interceptor_init` | 初始化实例：互斥锁、函数表、代码分配器、选项 | 使用中 |
| `hoox_interceptor_new` | 分配 + 初始化一个 interceptor | 使用中 |
| `hoox_interceptor_ref` | 引用计数++ | 公共API |
| `hoox_interceptor_unref` | 引用计数--；归零时 finalize | 公共API |
| `hoox_interceptor_finalize` | 拆除实例，清空单例指针 | 使用中 |
| `hoox_interceptor_obtain` | 获取/创建进程级单例 | 公共API |
| `hoox_interceptor_set_default_options` | 设置默认插桩选项 | 公共API |
| `hoox_interceptor_attach` | 将 enter/leave 监听器（或 probe）附加到目标 | 公共API |
| `hoox_interceptor_detach` | 从每个函数上分离监听器 | 公共API |
| `hoox_interceptor_replace` | 替换函数；原函数可通过 self-call/out-ptr 访问 | 公共API |
| `hoox_interceptor_replace_fast` | 通过直接分支替换，无 trampoline | 公共API |
| `hoox_interceptor_replace_with_type` | 共享的 replace 实现（默认/fast） | 使用中 |
| `hoox_interceptor_revert` | 撤销一次 replace | 公共API |
| `hoox_interceptor_begin_transaction` | 开始延迟激活（可嵌套） | 公共API |
| `hoox_interceptor_end_transaction` | 结束批次；最外层应用所有更改 | 公共API |
| `hoox_interceptor_flush` | 强制执行待处理的 teardown pass；报告是否已排空 | 公共API |
| `hoox_interceptor_flush_function` | 对某个函数的插桩执行 flush 检查 | 使用中（已导出，不在 facade 中） |
| `hoox_interceptor_flush_listener` | 对某个监听器执行 flush 检查 | 使用中（已导出，不在 facade 中） |
| `hoox_interceptor_get_current_invocation` | 当前线程的栈顶调用上下文 | 公共API |
| `hoox_interceptor_get_live_replacement_invocation` | 给定活跃 replacement 的调用上下文 | 使用中（已导出，不在 facade 中） |
| `hoox_interceptor_get_current_stack` | 当前线程的调用栈 | 公共API |
| `hoox_interceptor_ignore_current_thread` | 抑制本线程的监听器（可嵌套） | 公共API |
| `hoox_interceptor_unignore_current_thread` | 撤销一次 ignore | 公共API |
| `hoox_interceptor_maybe_unignore_current_thread` | 仅在当前处于 ignore 状态时撤销一次 ignore | 使用中 |
| `hoox_interceptor_ignore_other_threads` | 将拦截限制在调用线程 | 公共API |
| `hoox_interceptor_unignore_other_threads` | 恢复对所有线程的拦截 | 公共API |
| `hoox_invocation_stack_translate` | 将被劫持的返回地址翻译 → 真实调用者地址 | 公共API |
| `hoox_interceptor_save` | 保存当前调用深度（longjmp 之前） | 公共API |
| `hoox_interceptor_restore` | 将栈回退到保存的深度 | 公共API |
| `hoox_interceptor_with_lock_held` | 持有 interceptor 锁运行回调 | 公共API |
| `hoox_interceptor_is_locked` | interceptor 锁是否被持有 | 公共API |
| `hoox_interceptor_detect_hook_size` | **capstone 遗留**：hook-size 探测（`hx_csh`/`hx_insn` 签名）；委托给后端，无已编译的调用者 | 使用中（已导出；capstone 签名泄漏点，实际未使用） |
| `_hoox_interceptor_peek_top_caller_return_address` | 栈顶条目的调用者返回地址 | 使用中 |
| `_hoox_interceptor_translate_top_return_address` | 翻译栈顶返回地址；**仅被 arm/arm64 后端取地址**（x86 上不编译） | 死代码（x86 构建）；跨架构使用 |
| `hoox_interceptor_instrument` | 查找/创建函数上下文 + 构建 trampoline | 使用中 |
| `hoox_interceptor_activate` | 激活某个函数的 trampoline | 使用中 |
| `hoox_interceptor_deactivate` | 停用某个函数的 trampoline | 使用中 |
| `hoox_interceptor_transaction_init` | 初始化事务（队列/表） | 使用中 |
| `hoox_interceptor_transaction_destroy` | 排空 + 释放事务 | 使用中 |
| `hoox_interceptor_transaction_begin` | level++ | 使用中 |
| `hoox_interceptor_transaction_end` | 最外层：patch 页面，运行 destroy 任务 | 使用中 |
| `hoox_apply_updates` | 将计划的更新应用到重映射的页面上 | 使用中 |
| `hoox_interceptor_transaction_schedule_destroy` | 将延迟的 destroy/notify 入队 | 使用中 |
| `hoox_interceptor_transaction_schedule_update` | 将每页 trampoline 更新入队 | 使用中 |
| `hoox_function_context_new` | 分配一个每函数上下文 | 使用中 |
| `hoox_function_context_finalize` | 释放函数上下文内存 | 使用中 |
| `hoox_function_context_destroy` | 标记为已销毁；调度 deactivate+destroy | 使用中 |
| `hoox_function_context_perform_destroy` | 先销毁 trampoline 再 finalize | 使用中 |
| `hoox_function_context_is_empty` | 无 replacement 且无监听器 | 使用中 |
| `hoox_function_context_add_listener` | 写时复制追加一个监听器条目 | 使用中 |
| `listener_entry_free` | 释放一个 ListenerEntry | 使用中 |
| `hoox_function_context_remove_listener` | 将某个监听器槽置空；重新计算标志 | 使用中 |
| `hoox_function_context_has_listener` | 是否已附加监听器 | 使用中 |
| `hoox_function_context_find_listener` | 查找某个监听器的槽 | 使用中 |
| `hoox_function_context_find_taken_listener_slot` | 第一个非空监听器槽 | 使用中 |
| `_hoox_function_context_begin_invocation` | 来自 trampoline 的 on-enter 分发；压栈、运行监听器、选择下一跳 | 使用中 |
| `_hoox_function_context_end_invocation` | on-leave 分发；运行监听器、出栈、恢复返回地址 | 使用中 |
| `hoox_function_context_fixup_cpu_context` | 在保存的 cpu 上下文中设置 PC 字段 | 使用中 |
| `get_interceptor_thread_context` | 获取/创建每线程 interceptor 上下文 | 使用中 |
| `release_interceptor_thread_context` | 从注册表中移除线程上下文 | 使用中 |
| `hoox_interceptor_invocation_get_listener_point_cut` | 后端 vtable：enter/leave 切入点 | 使用中（vtable） |
| `hoox_interceptor_invocation_get_replacement_point_cut` | 后端 vtable：replacement 始终为 ENTER | 使用中（vtable） |
| `hoox_interceptor_invocation_get_thread_id` | 后端 vtable：当前线程 id | 使用中（vtable） |
| `hoox_interceptor_invocation_get_depth` | 后端 vtable：调用深度 | 使用中（vtable） |
| `hoox_interceptor_invocation_get_listener_thread_data` | 后端 vtable：每监听器线程数据 | 使用中（vtable） |
| `hoox_interceptor_invocation_get_listener_function_data` | 后端 vtable：每监听器函数数据 | 使用中（vtable） |
| `hoox_interceptor_invocation_get_listener_invocation_data` | 后端 vtable：每次调用的临时数据 | 使用中（vtable） |
| `hoox_interceptor_invocation_get_replacement_data` | 后端 vtable：replacement 用户数据 | 使用中（vtable） |
| `interceptor_thread_context_new` | 分配线程上下文（后端、栈、数据槽） | 使用中 |
| `interceptor_thread_context_destroy` | 释放线程上下文 + 释放 trampoline | 使用中 |
| `interceptor_thread_context_get_listener_data` | 获取/分配每线程监听器数据槽 | 使用中 |
| `interceptor_thread_context_forget_listener_data` | 释放某个监听器的线程数据槽 | 使用中 |
| `hoox_invocation_stack_push` | 压入一个调用栈条目 | 使用中 |
| `hoox_invocation_stack_pop` | 弹出条目；返回调用者返回地址 | 使用中 |
| `hoox_invocation_stack_reap_unwound` | 回收已被展开越过的条目（按 SP） | 使用中 |
| `hoox_invocation_stack_reap_unwound_above` | 回收返回帧之上的条目 | 使用中 |
| `hoox_invocation_stack_entry_was_unwound_past` | 比较条目 SP 与活跃 SP | 使用中 |
| `hoox_invocation_stack_entry_release_trampoline` | 递减 trampoline 使用计数 | 使用中 |
| `hoox_invocation_stack_peek_top` | 栈顶条目或 NULL | 使用中 |
| `hoox_interceptor_resolve` | 跟随重定向跳转到真实目标地址 | 使用中 |
| `hoox_interceptor_has` | 某地址是否已被插桩 | 使用中 |
| `hoox_page_address_from_pointer` | 将指针向下取整到页基址 | 使用中 |
| `hoox_page_address_compare` | 页地址的排序比较器 | 使用中 |

**Interceptor 总计：** 死代码 1（`_hoox_interceptor_translate_top_return_address` —— 但被 arm/arm64 后端使用），仅测试 0。`hoox_interceptor_detect_hook_size` 是 capstone 签名遗留，无已编译的调用者（属签名清理候选，而非删除候选）。

### `src/core/hooxmemory.c` — 已编译 ✅ · 1092 loc · 48 funcs
_内存查询（页大小 / RWX / ptrauth）、安全的代码打补丁（page-remap / mprotect / code-segment）、堆分配器、页分配。带有 ptrauth（Apple）和 Android softening 代码路径。_

> **残留的内存 SCAN / match-pattern 脚手架：** 该文件声明了
> `HooxScanVec` + SIMD 宏（`HOOX_SCAN_VEC_*`）、`HOOX_POINTER_SCAN_*`
> 常量，以及 `HooxPointerScan` / `HooxPointerScanTile` /
> `HooxPointerScanTask` 结构体，外加一段孤立的 `hoox_memory_scan` 文档注释
> （错误地挂在 `hoox_ensure_code_readable` 上方）。**实际上没有定义任何 scan
> 函数** —— 这是 frida 遗留的死脚手架，可以整体移除。

> **dlmalloc 与 libc 分支：** 分配器函数在审计中出现了**两次**，
> 因为它们有两个互斥的 `#ifdef HOOX_USE_DLMALLOC`
> 定义（dlmalloc 分支 L849–947，libc 分支 L951–1043）。在 Windows/x86 上
> `HOOX_USE_DLMALLOC` 关闭，因此**编译的是 libc 分支**，dlmalloc
> 分支处于惰性状态。

| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_internal_heap_ref` | 首次引用：初始化内存后端、页大小、cloak、mspaces | 使用中 |
| `hoox_internal_heap_unref` | 最后一次 unref：销毁 mspaces、cloak、后端 | 使用中 |
| `hoox_sign_code_pointer` | **ptrauth**：签名代码指针（无 PTRAUTH 时为 no-op） | 使用中（被 arm64 使用；x86 上为 no-op） |
| `hoox_strip_code_pointer` | **ptrauth**：从指针剥离 PAC（x86 上为 no-op） | 使用中（被 arm64 使用；x86 上为 no-op） |
| `hoox_sign_code_address` | **ptrauth**：签名代码地址；仅被 arm64 writer/backend 使用 | 死代码（x86 构建）；ptrauth，跨架构使用 |
| `hoox_strip_code_address` | **ptrauth**：从地址剥离 PAC；无 x86 调用者 | 死代码（x86 构建）；ptrauth，跨架构使用 |
| `hoox_query_ptrauth_support` | **ptrauth**：报告支持情况；仅被 arm64 writer 使用 | 死代码（x86 构建）；ptrauth，跨架构使用 |
| `hoox_query_page_size` | 返回缓存的页大小 | 使用中 |
| `hoox_query_is_rwx_supported` | RWX 是否 == FULL | 使用中 |
| `hoox_query_rwx_support` | RWX 级别（Darwin-非i386 = NONE，其余 = FULL） | 使用中 |
| `hoox_memory_patch_code` | 在某地址安全修改代码字节（经由页面） | 使用中 |
| `hoox_apply_patch_code` | 适配器：在页偏移处应用用户 patch | 使用中 |
| `hoox_memory_patch_code_pages` | 打补丁代码页（remap-writable / mprotect+suspend / code-segment） | 使用中 |
| `hoox_maybe_suspend_thread` | 挂起一个非当前线程（仅非-RWX 路径；Windows 上仅链接） | 使用中 |
| `hoox_memory_mark_code` | 将页标记为可执行（code-segment 或 RX） | 仅测试 |
| `hoox_ensure_code_readable` | 软化不可读代码页（**仅 Android**；其他平台为 no-op） | 使用中（Android 遗留；Windows/x86 上为 no-op） |
| `hoox_mprotect` | mprotect 封装；失败时 abort | 使用中 |
| `hoox_peek_private_memory_usage` (dlmalloc) | 汇总 mspace 用量 | dlmalloc 分支 —— x86 上不编译 |
| `hoox_malloc` (dlmalloc) | mspace malloc | dlmalloc 分支 —— x86 上不编译 |
| `hoox_malloc0` (dlmalloc) | mspace 清零 malloc | dlmalloc 分支 —— x86 上不编译 |
| `hoox_malloc_usable_size` (dlmalloc) | mspace 可用大小 | dlmalloc 分支 —— x86 上不编译 |
| `hoox_calloc` (dlmalloc) | mspace calloc | dlmalloc 分支 —— x86 上不编译 |
| `hoox_realloc` (dlmalloc) | mspace realloc | dlmalloc 分支 —— x86 上不编译 |
| `hoox_memalign` (dlmalloc) | mspace memalign | dlmalloc 分支 —— x86 上不编译 |
| `hoox_memdup` (dlmalloc) | mspace malloc + 复制 | dlmalloc 分支 —— x86 上不编译 |
| `hoox_free` (dlmalloc) | mspace free | dlmalloc 分支 —— x86 上不编译 |
| `hoox_internal_malloc` (dlmalloc) | internal-mspace malloc | dlmalloc 分支 —— x86 上不编译 |
| `hoox_internal_calloc` (dlmalloc) | internal-mspace calloc | dlmalloc 分支 —— x86 上不编译 |
| `hoox_internal_realloc` (dlmalloc) | internal-mspace realloc | dlmalloc 分支 —— x86 上不编译 |
| `hoox_internal_free` (dlmalloc) | internal-mspace free | dlmalloc 分支 —— x86 上不编译 |
| `hoox_peek_private_memory_usage` (libc) | 返回 0（不跟踪） | 使用中 |
| `hoox_malloc` (libc) | libc malloc | 使用中 |
| `hoox_malloc0` (libc) | libc calloc(1,size) | 使用中 |
| `hoox_malloc_usable_size` (libc) | 返回 0 | 使用中 |
| `hoox_calloc` (libc) | libc calloc | 使用中 |
| `hoox_realloc` (libc) | libc realloc | 使用中 |
| `hoox_memalign` (libc) | 未实现（assert_not_reached） | 使用中（桩） |
| `hoox_memdup` (libc) | malloc + 复制 | 使用中 |
| `hoox_free` (libc) | libc free | 使用中 |
| `hoox_internal_malloc` (libc) | → hoox_malloc | 使用中 |
| `hoox_internal_calloc` (libc) | → hoox_calloc | 使用中 |
| `hoox_internal_realloc` (libc) | → hoox_realloc | 使用中 |
| `hoox_internal_free` (libc) | → hoox_free | 使用中 |
| `hoox_alloc_n_pages` | 分配 N 页（断言非 NULL） | 使用中 |
| `hoox_alloc_n_pages_near` | 在某地址规格附近分配 N 页 | 仅测试 |
| `hoox_address_spec_is_satisfied_by` | 地址是否在规格距离内 | 使用中 |
| `hoox_memory_range_copy` | HooxMemoryRange 的 boxed-type 复制；**任何地方都无调用者** | 死代码（可移除 —— frida boxed-type 遗留） |
| `hoox_memory_range_free` | 释放复制出的 HooxMemoryRange；**任何地方都无调用者** | 死代码（可移除 —— frida boxed-type 遗留） |

**Memory 总计：** 按规则死代码 5（`hoox_sign_code_address`、`hoox_strip_code_address`、
`hoox_query_ptrauth_support` —— ptrauth，被 arm64 跨架构使用；`hoox_memory_range_copy`、
`hoox_memory_range_free` —— 真正可移除），仅测试 2（`hoox_memory_mark_code`、
`hoox_alloc_n_pages_near`）。

### 发现的可移除功能簇

- **内存 SCAN / match-pattern：** 此处完全残留 —— 只有结构体/宏
  脚手架（`HooxPointerScan*`、`HooxScanVec`、`HOOX_POINTER_SCAN_*`、SIMD
  宏）和一段孤立的文档注释；**没有定义任何 scan 函数**。可安全删除。
- **ptrauth (Apple arm64)：** `hoox_sign/strip_code_pointer`、
  `hoox_sign/strip_code_address`、`hoox_query_ptrauth_support` —— 在
  Windows/x86 上是 no-op；`_address` 三元组 + `query_ptrauth_support` 无 x86 调用者
  （仅由未编译的 arm64 源码保持存活）。对于仅 x86 的构建可移除。
- **Android softening：** `hoox_ensure_code_readable` 的主体完全处于
  `HAVE_ANDROID` 之下 —— 在 Windows/x86 上是 no-op。
- **capstone 签名泄漏：** `hoox_interceptor_detect_hook_size` 仍在其公共签名中暴露
  `hx_csh`/`hx_insn`，且无已编译的调用者。
- **真正的死代码（在 src/ 或 tests/ 中任何地方都无引用）：**
  `hoox_memory_range_copy`、`hoox_memory_range_free`。
## disasm + arch/x86 — 解码器、reader、relocator、writer

这四个文件是 x86/x64 机器码层。`hx_disasm_x86.c` 是一个紧凑的、表驱动的长度/控制流/重定位解码器（从 Microsoft Detours 移植而来），用于替代 capstone：它向调用栈的其余部分提供指令大小、控制流/ALU 分类、ModRM/位移编码以及隐式寄存器读写集合。**reader** 检查单条指令（长度、jcc 判定、分支/调用目标提取）；**relocator** 遍历被移位的指令并在新地址处重新发射它们（重写 near/short 分支、条件分支以及 RIP 相对内存操作数）；**writer** 是一个大型机器码发射器（约 151 个函数），供 relocator 和 interceptor 后端用于组装 trampoline。这些符号都不属于公共 `hoox_*` facade——它们全部是内部引擎函数。

状态图例：**死代码** = 非公共，`refs<=1`，`tref==0`（已定义但从未在任何地方被调用）。**仅测试** = 非公共，`refs<=1`，仅被测试使用。**使用中** = 在已编译的库代码中被调用。

### `src/disasm/hx_disasm_x86.c` — 已编译 ✅ · 838 loc · 19 funcs
| 函数 | 作用 | 状态 |
|---|---|---|
| `gpr64` | 将 0–15 索引映射为 64 位 GPR 枚举 | 使用中 |
| `imm_z_size` | 根据操作数大小前缀确定立即数 "z" 大小（2 或 4） | 使用中 |
| `decode_modrm` | 解码 ModRM+SIB+位移，填充第一个操作数 + 编码偏移 | 使用中 |
| `skip_imm` | 将游标前移越过 N 字节的立即数 | 使用中 |
| `set_imm_target` | 将计算出的分支目标存为 operand[0] IMM | 使用中 |
| `classify_dp_1byte` | 尽力分类单字节 ALU/MOV/等操作码 | 使用中 |
| `note_implicit_regs` | 填充隐式寄存器读写集合（MUL/DIV/shift-CL/CMPXCHG） | 使用中 |
| `hx_decode` | 核心解码器：前缀/VEX/转义 → 长度 + id + 操作数 | 使用中 |
| `hx_open` | capstone 垫片：为 arch/mode 分配解码器句柄 | 使用中 |
| `hx_close` | capstone 垫片：释放解码器句柄 | 使用中 |
| `hx_option` | capstone 垫片：在句柄上设置 DETAIL/MODE 选项 | 使用中 |
| `hx_insn_alloc` | capstone 垫片：分配一个 insn + detail 结构体 | 使用中 |
| `hx_insn_free` | capstone 垫片：释放 insn 数组 + detail | 使用中 |
| `hx_disasm_iter` | capstone 垫片：解码一条 insn，前移 code/size/address | 使用中 |
| `hx_disasm` | capstone 垫片：将最多 `count` 条 insn 解码进数组 | 使用中 |
| `reg_in` | 在寄存器 id 数组上做成员测试 | 使用中 |
| `hx_reg_read` | capstone 垫片：insn 是否读取寄存器？ | 使用中 |
| `hx_reg_write` | capstone 垫片：insn 是否写入寄存器？ | 使用中 |
| `hx_arch_register_x86` | capstone 垫片：空操作的 arch 注册钩子 | 使用中 |

无死代码或仅测试函数——每个解码器函数都可从已编译的 reader/relocator 到达。

### `src/arch/x86/hooxx86reader.c` — 已编译 ✅ · 171 loc · 8 funcs
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_reader_insn_length` | 解码一条 insn，返回其字节长度 | 死代码 |
| `hoox_x86_reader_insn_is_jcc` | 指令是否为条件跳转（Jcc）？ | 使用中 |
| `hoox_x86_reader_find_next_call_target` | 向前扫描第一个 CALL，返回其目标 | 死代码 |
| `hoox_x86_reader_try_get_relative_call_target` | 提取相对 CALL 的目标 | 死代码 |
| `hoox_x86_reader_try_get_relative_jump_target` | 提取相对 JMP 的目标 | 使用中 |
| `hoox_x86_reader_try_get_indirect_jump_target` | 解析 RIP 相对/绝对间接 JMP 目标 | 使用中 |
| `try_get_relative_call_or_jump_target` | 相对 CALL/JMP 目标提取的共享辅助函数 | 使用中 |
| `hoox_x86_reader_disassemble_instruction_at` | 解码某地址处的单条指令 | 使用中 |

死代码：3 · 仅测试：0

### `src/arch/x86/hooxx86relocator.c` — 已编译 ✅ · 781 loc · 29 funcs
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_relocator_new` | 分配 + 初始化一个 relocator | 死代码 |
| `hoox_x86_relocator_ref` | 递增引用计数 | 死代码 |
| `hoox_x86_relocator_unref` | 递减引用计数，归零时清理 + 释放 | 死代码 |
| `hoox_x86_relocator_init` | 初始化 relocator（打开解码器，分配 insn 环形缓冲） | 使用中 |
| `hoox_x86_relocator_clear` | 释放 insn 环形缓冲 + 关闭解码器 | 使用中 |
| `hoox_x86_relocator_reset` | 重置输入/输出游标及 eob/eoi 标志 | 使用中 |
| `hoox_x86_relocator_inpos` | 输入环形缓冲索引（对容量取模） | 使用中 |
| `hoox_x86_relocator_outpos` | 输出环形缓冲索引（对容量取模） | 使用中 |
| `hoox_x86_relocator_increment_inpos` | 前移输入位置 | 使用中 |
| `hoox_x86_relocator_increment_outpos` | 前移输出位置 | 使用中 |
| `hoox_x86_relocator_read_one` | 解码下一条输入 insn，设置 eob/eoi | 使用中 |
| `hoox_x86_relocator_peek_next_write_insn` | 窥视下一条待写入的 insn | 使用中 |
| `hoox_x86_relocator_peek_next_write_source` | 下一条待处理 insn 的源地址 | 仅测试 |
| `hoox_x86_relocator_skip_one` | 跳过一条 insn，为其发射一个标签 | 仅测试 |
| `hoox_x86_relocator_skip_one_no_label` | 跳过一条 insn，不发射标签 | 死代码 |
| `hoox_x86_relocator_write_one` | 发射下一条 insn，带标签 | 使用中 |
| `hoox_x86_relocator_write_one_no_label` | 发射下一条 insn，不带标签 | 死代码 |
| `hoox_x86_relocator_write_one_instruction` | 核心：重定位/重新发射一条 insn（按 id 分派） | 使用中 |
| `hoox_x86_relocator_write_all` | 发射所有待处理的 insn | 使用中 |
| `hoox_x86_relocator_eob` | 是否到达块尾？ | 仅测试 |
| `hoox_x86_relocator_eoi` | 是否到达输入尾？ | 使用中 |
| `hoox_x86_relocator_put_label_for` | 发射一个绑定到某 insn 源地址的标签 | 使用中 |
| `hoox_x86_relocator_can_relocate` | 探测可安全重定位多少字节 | 使用中 |
| `hoox_x86_relocator_relocate` | 一次性：将 `min_bytes` 从 from 重定位到 to | 死代码 |
| `hoox_x86_relocator_rewrite_unconditional_branch` | 重写 CALL/JMP（含 get-pc-thunk、call-to-next） | 使用中 |
| `hoox_x86_relocator_rewrite_conditional_branch` | 以 near/short/trampoline 形式重写 Jcc/JECXZ | 使用中 |
| `hoox_x86_relocator_rewrite_if_rip_relative` | 通过 scratch 寄存器重写 RIP 相对内存操作数 | 使用中 |
| `hoox_x86_call_is_to_next_instruction` | 检测 `CALL next`（PC 物化惯用法） | 使用中 |
| `hoox_x86_call_try_parse_get_pc_thunk` | 检测 `__x86.get_pc_thunk` 调用 | 使用中 |

死代码：6 · 仅测试：3

### `src/arch/x86/hooxx86writer.c` — 已编译 ✅ · 3301 loc · 151 funcs

机器码发射器。最大的清理目标：51 个死代码，25 个仅测试。下面按函数分组。

#### 生命周期 / writer 状态
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_new` | 分配 + 初始化一个 writer | 死代码 |
| `hoox_x86_writer_ref` | 递增引用计数 | 使用中 |
| `hoox_x86_writer_unref` | 递减引用计数，归零时释放 | 使用中 |
| `hoox_x86_writer_init` | 在输出缓冲区上初始化 writer | 使用中 |
| `hoox_x86_writer_has_label_defs` | 是否有待处理的标签定义？ | 使用中 |
| `hoox_x86_writer_has_label_refs` | 是否有未解析的标签引用？ | 使用中 |
| `hoox_x86_writer_clear` | 刷新 + 释放 writer 状态 | 使用中 |
| `hoox_x86_writer_reset` | 将 writer 重新指向新的输出缓冲区 | 使用中 |
| `hoox_x86_writer_set_target_cpu` | 选择 IA32/AMD64 目标 | 仅测试 |
| `hoox_x86_writer_set_target_abi` | 选择 Unix/Windows ABI | 仅测试 |
| `hoox_x86_writer_cur` | 当前输出指针 | 死代码 |
| `hoox_x86_writer_offset` | 目前已写入的字节数 | 使用中 |
| `hoox_x86_writer_commit` | 将输出游标前移 N 字节 | 使用中 |
| `hoox_x86_writer_flush` | 解析标签引用 + 提交字面量 | 使用中 |
| `hoox_x86_writer_get_cpu_register_for_nth_argument` | ABI 参数寄存器查找 | 死代码 |

#### 标签 / 分支可行性
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_label` | 在当前位置定义一个标签 | 使用中 |
| `hoox_x86_writer_add_label_reference_here` | 记录一个待修正的标签引用 | 使用中 |
| `hoox_x86_writer_can_branch_directly_between` | 直接分支是否在可达范围内？ | 死代码 |

#### 带参数的 CALL（编排包装器）
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_call_address_with_arguments` | CALL abs addr，可变参数 | 仅测试 |
| `hoox_x86_writer_put_call_address_with_arguments_array` | CALL abs addr，数组参数 | 死代码 |
| `hoox_x86_writer_put_call_address_with_aligned_arguments` | CALL abs addr，栈对齐可变参数 | 使用中 |
| `hoox_x86_writer_put_call_address_with_aligned_arguments_array` | CALL abs addr，栈对齐数组参数 | 死代码 |
| `hoox_x86_writer_put_call_reg_with_arguments` | CALL reg，可变参数 | 仅测试 |
| `hoox_x86_writer_put_call_reg_with_arguments_array` | CALL reg，数组参数 | 死代码 |
| `hoox_x86_writer_put_call_reg_with_aligned_arguments` | CALL reg，对齐可变参数 | 死代码 |
| `hoox_x86_writer_put_call_reg_with_aligned_arguments_array` | CALL reg，对齐数组参数 | 死代码 |
| `hoox_x86_writer_put_call_reg_offset_ptr_with_arguments` | CALL [reg+off]，可变参数 | 仅测试 |
| `hoox_x86_writer_put_call_reg_offset_ptr_with_arguments_array` | CALL [reg+off]，数组参数 | 死代码 |
| `hoox_x86_writer_put_call_reg_offset_ptr_with_aligned_arguments` | CALL [reg+off]，对齐可变参数 | 死代码 |
| `hoox_x86_writer_put_call_reg_offset_ptr_with_aligned_arguments_array` | CALL [reg+off]，对齐数组参数 | 死代码 |

#### 参数列表 建立 / 拆除
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_argument_list_setup` | 压入/编排调用参数 | 使用中 |
| `hoox_x86_writer_put_argument_list_setup_va` | setup 的 va_list 形式 | 使用中 |
| `hoox_x86_writer_put_argument_list_teardown` | 调用后恢复栈 | 使用中 |
| `hoox_x86_writer_put_aligned_argument_list_setup` | 栈对齐参数建立 | 使用中 |
| `hoox_x86_writer_put_aligned_argument_list_setup_va` | va_list 对齐建立 | 使用中 |
| `hoox_x86_writer_put_aligned_argument_list_teardown` | 对齐拆除 | 使用中 |
| `hoox_x86_writer_get_needed_alignment_correction` | 计算栈对齐填充 | 使用中 |

#### CALL / JMP 核心
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_call_address` | 发射 CALL 到绝对地址 | 使用中 |
| `hoox_x86_writer_put_call_reg` | 发射 `CALL reg` | 使用中 |
| `hoox_x86_writer_put_call_reg_offset_ptr` | 发射 `CALL [reg+off]` | 使用中 |
| `hoox_x86_writer_put_call_indirect` | 发射经由间接指针的 CALL | 使用中 |
| `hoox_x86_writer_put_call_indirect_label` | 发射到标签的间接 CALL | 使用中 |
| `hoox_x86_writer_put_call_near_label` | 发射到标签的 near CALL | 仅测试 |
| `hoox_x86_writer_put_jmp_address` | 发射 JMP 到绝对地址 | 使用中 |
| `hoox_x86_writer_put_short_jmp` | 发射 short（rel8）JMP | 使用中 |
| `hoox_x86_writer_put_near_jmp` | 发射 near（rel32）JMP | 使用中 |
| `hoox_x86_writer_put_jmp_short_label` | 发射到标签的 short JMP | 使用中 |
| `hoox_x86_writer_put_jmp_near_label` | 发射到标签的 near JMP | 死代码 |
| `hoox_x86_writer_put_jmp_reg` | 发射 `JMP reg` | 使用中 |
| `hoox_x86_writer_put_jmp_reg_ptr` | 发射 `JMP [reg]` | 仅测试 |
| `hoox_x86_writer_put_jmp_reg_offset_ptr` | 发射 `JMP [reg+off]` | 使用中 |
| `hoox_x86_writer_put_jmp_near_ptr` | 发射 `JMP [abs ptr]` | 仅测试 |

#### RET / LEAVE / UD2
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_leave` | 发射 LEAVE | 死代码 |
| `hoox_x86_writer_put_ret` | 发射 RET | 使用中 |
| `hoox_x86_writer_put_ret_imm` | 发射 `RET imm16` | 死代码 |
| `hoox_x86_writer_put_ud2` | 发射 UD2（陷阱） | 使用中 |

#### Jcc（条件跳转）
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_jcc_short` | 发射 short Jcc | 使用中 |
| `hoox_x86_writer_put_jcc_near` | 发射 near Jcc | 使用中 |
| `hoox_x86_writer_put_jcc_short_label` | 发射到标签的 short Jcc | 使用中 |
| `hoox_x86_writer_put_jcc_near_label` | 发射到标签的 near Jcc | 仅测试 |

#### 算术（ADD/SUB/INC/DEC）
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_add_or_sub_reg_imm` | 共享的 ADD/SUB `reg, imm` | 使用中 |
| `hoox_x86_writer_put_add_reg_imm` | 发射 `ADD reg, imm` | 使用中 |
| `hoox_x86_writer_put_add_reg_reg` | 发射 `ADD reg, reg` | 仅测试 |
| `hoox_x86_writer_put_add_reg_near_ptr` | 发射 `ADD reg, [abs ptr]` | 死代码 |
| `hoox_x86_writer_put_sub_reg_imm` | 发射 `SUB reg, imm` | 使用中 |
| `hoox_x86_writer_put_sub_reg_reg` | 发射 `SUB reg, reg` | 死代码 |
| `hoox_x86_writer_put_sub_reg_near_ptr` | 发射 `SUB reg, [abs ptr]` | 死代码 |
| `hoox_x86_writer_put_inc_reg` | 发射 `INC reg` | 仅测试 |
| `hoox_x86_writer_put_dec_reg` | 发射 `DEC reg` | 仅测试 |
| `hoox_x86_writer_put_inc_or_dec_reg_ptr` | 共享的 INC/DEC `[reg]` | 使用中 |
| `hoox_x86_writer_put_inc_reg_ptr` | 发射 `INC [reg]` | 死代码 |
| `hoox_x86_writer_put_dec_reg_ptr` | 发射 `DEC [reg]` | 死代码 |

#### LOCK / 原子操作
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_lock_xadd_reg_ptr_reg` | 发射 `LOCK XADD [reg], reg` | 仅测试 |
| `hoox_x86_writer_put_lock_cmpxchg_reg_ptr_reg` | 发射 `LOCK CMPXCHG [reg], reg` | 死代码 |
| `hoox_x86_writer_put_lock_inc_or_dec_imm32_ptr` | 共享的 LOCK INC/DEC `[abs32]` | 使用中 |
| `hoox_x86_writer_put_lock_inc_imm32_ptr` | 发射 `LOCK INC [abs32]` | 仅测试 |
| `hoox_x86_writer_put_lock_dec_imm32_ptr` | 发射 `LOCK DEC [abs32]` | 仅测试 |

#### 逻辑 / 移位
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_and_reg_reg` | 发射 `AND reg, reg` | 仅测试 |
| `hoox_x86_writer_put_and_reg_u32` | 发射 `AND reg, imm32` | 使用中 |
| `hoox_x86_writer_put_shl_reg_u8` | 发射 `SHL reg, imm8` | 仅测试 |
| `hoox_x86_writer_put_shr_reg_u8` | 发射 `SHR reg, imm8` | 死代码 |
| `hoox_x86_writer_put_xor_reg_reg` | 发射 `XOR reg, reg` | 死代码 |

#### MOV
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_mov_reg_reg` | 发射 `MOV reg, reg` | 使用中 |
| `hoox_x86_writer_put_mov_reg_u32` | 发射 `MOV reg, imm32` | 使用中 |
| `hoox_x86_writer_put_mov_reg_u64` | 发射 `MOV reg, imm64` | 使用中 |
| `hoox_x86_writer_put_mov_reg_address` | 发射 `MOV reg, addr`（带大小） | 使用中 |
| `hoox_x86_writer_put_mov_reg_ptr_u32` | 发射 `MOV [reg], imm32` | 死代码 |
| `hoox_x86_writer_put_mov_reg_offset_ptr_u32` | 发射 `MOV [reg+off], imm32` | 使用中 |
| `hoox_x86_writer_put_mov_reg_ptr_reg` | 发射 `MOV [reg], reg` | 使用中 |
| `hoox_x86_writer_put_mov_reg_offset_ptr_reg` | 发射 `MOV [reg+off], reg` | 使用中 |
| `hoox_x86_writer_put_mov_reg_reg_ptr` | 发射 `MOV reg, [reg]` | 使用中 |
| `hoox_x86_writer_put_mov_reg_reg_offset_ptr` | 发射 `MOV reg, [reg+off]` | 使用中 |
| `hoox_x86_writer_put_mov_reg_base_index_scale_offset_ptr` | 发射 `MOV reg, [base+index*scale+off]` | 死代码 |
| `hoox_x86_writer_put_mov_reg_near_ptr` | 发射 `MOV reg, [abs ptr]` | 仅测试 |
| `hoox_x86_writer_put_mov_near_ptr_reg` | 发射 `MOV [abs ptr], reg` | 仅测试 |
| `hoox_x86_writer_put_mov_reg_imm_ptr` | 发射 `MOV reg, [imm ptr]` | 使用中 |
| `hoox_x86_writer_put_mov_imm_ptr_reg` | 发射 `MOV [imm ptr], reg` | 使用中 |

#### 经由 FS/GS 段的 MOV
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_mov_fs_u32_ptr_reg` | 发射 `MOV fs:[imm32], reg` | 死代码 |
| `hoox_x86_writer_put_mov_reg_fs_u32_ptr` | 发射 `MOV reg, fs:[imm32]` | 死代码 |
| `hoox_x86_writer_put_mov_fs_reg_ptr_reg` | 发射 `MOV fs:[reg], reg` | 死代码 |
| `hoox_x86_writer_put_mov_reg_fs_reg_ptr` | 发射 `MOV reg, fs:[reg]` | 死代码 |
| `hoox_x86_writer_put_mov_gs_u32_ptr_reg` | 发射 `MOV gs:[imm32], reg` | 死代码 |
| `hoox_x86_writer_put_mov_reg_gs_u32_ptr` | 发射 `MOV reg, gs:[imm32]` | 死代码 |
| `hoox_x86_writer_put_mov_gs_reg_ptr_reg` | 发射 `MOV gs:[reg], reg` | 死代码 |
| `hoox_x86_writer_put_mov_reg_gs_reg_ptr` | 发射 `MOV reg, gs:[reg]` | 死代码 |

#### SIMD（MOVQ / MOVDQU）
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_movq_xmm0_esp_offset_ptr` | 发射 `MOVQ xmm0, [esp+off]` | 死代码 |
| `hoox_x86_writer_put_movq_eax_offset_ptr_xmm0` | 发射 `MOVQ [eax+off], xmm0` | 死代码 |
| `hoox_x86_writer_put_movdqu_xmm0_esp_offset_ptr` | 发射 `MOVDQU xmm0, [esp+off]` | 死代码 |
| `hoox_x86_writer_put_movdqu_eax_offset_ptr_xmm0` | 发射 `MOVDQU [eax+off], xmm0` | 死代码 |

#### LEA / XCHG
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_lea_reg_reg_offset` | 发射 `LEA reg, [reg+off]` | 使用中 |
| `hoox_x86_writer_put_xchg_reg_reg_ptr` | 发射 `XCHG reg, [reg]` | 使用中 |

#### 栈（PUSH / POP）
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_push_u32` | 发射 `PUSH imm32` | 使用中 |
| `hoox_x86_writer_put_push_near_ptr` | 发射 `PUSH [abs ptr]` | 使用中 |
| `hoox_x86_writer_put_push_reg` | 发射 `PUSH reg` | 使用中 |
| `hoox_x86_writer_put_pop_reg` | 发射 `POP reg` | 使用中 |
| `hoox_x86_writer_put_push_imm_ptr` | 发射 `PUSH [imm ptr]` | 死代码 |
| `hoox_x86_writer_put_pushax` | 发射 PUSHA/PUSHAD 等价指令（保存 GPR） | 使用中 |
| `hoox_x86_writer_put_popax` | 发射 POPA/POPAD 等价指令（恢复 GPR） | 使用中 |
| `hoox_x86_writer_put_pushfx` | 发射 PUSHF/PUSHFQ | 使用中 |
| `hoox_x86_writer_put_popfx` | 发射 POPF/POPFQ | 使用中 |

#### 标志 / TEST / CMP
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_sahf` | 发射 SAHF | 死代码 |
| `hoox_x86_writer_put_lahf` | 发射 LAHF | 死代码 |
| `hoox_x86_writer_put_test_reg_reg` | 发射 `TEST reg, reg` | 使用中 |
| `hoox_x86_writer_put_test_reg_u32` | 发射 `TEST reg, imm32` | 死代码 |
| `hoox_x86_writer_put_cmp_reg_i32` | 发射 `CMP reg, imm32` | 仅测试 |
| `hoox_x86_writer_put_cmp_reg_offset_ptr_reg` | 发射 `CMP [reg+off], reg` | 仅测试 |
| `hoox_x86_writer_put_cmp_imm_ptr_imm_u32` | 发射 `CMP dword [imm ptr], imm32` | 死代码 |
| `hoox_x86_writer_put_cmp_reg_reg` | 发射 `CMP reg, reg` | 死代码 |
| `hoox_x86_writer_put_clc` | 发射 CLC（清进位标志） | 死代码 |
| `hoox_x86_writer_put_stc` | 发射 STC（置进位标志） | 死代码 |
| `hoox_x86_writer_put_cld` | 发射 CLD（清方向标志） | 使用中 |
| `hoox_x86_writer_put_std` | 发射 STD（置方向标志） | 死代码 |

#### 杂项系统 / 填充 / FPU 保存
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_cpuid` | 发射 CPUID | 死代码 |
| `hoox_x86_writer_put_lfence` | 发射 LFENCE | 死代码 |
| `hoox_x86_writer_put_rdtsc` | 发射 RDTSC | 死代码 |
| `hoox_x86_writer_put_pause` | 发射 PAUSE | 死代码 |
| `hoox_x86_writer_put_nop` | 发射单字节 NOP | 仅测试 |
| `hoox_x86_writer_put_breakpoint` | 发射 INT3 断点 | 仅测试 |
| `hoox_x86_writer_put_padding` | 发射 N 字节的断点填充 | 死代码 |
| `hoox_x86_writer_put_nop_padding` | 发射多字节 NOP 填充 | 使用中 |
| `hoox_x86_writer_put_fxsave_reg_ptr` | 发射 `FXSAVE [reg]` | 仅测试 |
| `hoox_x86_writer_put_fxrstor_reg_ptr` | 发射 `FXRSTOR [reg]` | 仅测试 |
| `hoox_x86_writer_put_fx_save_or_restore_reg_ptr` | 共享的 FXSAVE/FXRSTOR 发射器 | 使用中 |

#### 原始发射 + 编码辅助
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_x86_writer_put_u8` | 写入一个原始字节 | 使用中 |
| `hoox_x86_writer_put_s8` | 写入一个原始有符号字节 | 使用中 |
| `hoox_x86_writer_put_bytes` | 写入一段原始字节缓冲区 | 使用中 |
| `hoox_x86_writer_describe_cpu_reg` | 解码 reg 枚举 → 宽度/索引/元信息 | 使用中 |
| `hoox_meta_reg_from_cpu_reg` | 将 CPU reg 映射为 meta（与大小无关的）reg | 使用中 |
| `hoox_x86_writer_put_prefix_for_reg_info` | 为单个 reg 发射 REX/操作数大小前缀 | 使用中 |
| `hoox_x86_writer_put_prefix_for_registers` | 为一组 reg 发射 REX/操作数大小前缀 | 使用中 |
| `hoox_get_jcc_opcode` | 将 insn id 映射为 Jcc 操作码字节 | 使用中 |

死代码：51 · 仅测试：25

## backend/ — Windows 平台 + x86 架构

本层承载 hook 引擎中与操作系统相关和与 CPU 相关的两半。`backend/windows/*` 封装 Win32 原语 —— VirtualAlloc/VirtualProtect 内存管理、TEB-slot TLS，以及 psapi/Toolhelp 进程-线程-模块枚举 —— 外加供 clang/gcc 使用的段存储垫片。`backend/x86/*` 提供架构相关的拦截器：trampoline 布局与 thunk 发射、CPU-context 参数访问，以及带缓存的 CPUID 特性查询。所有函数均为内部函数（无一出现在 `include/hoox.h` 中）；标记为 DEAD 的是 `HOOX_API` 导出的入口点，当前没有任何已编译的函数体或测试调用它们。

### `src/backend/windows/hooxmemory-windows.c` — 已编译 ✅ · 474 loc · 27 funcs
_页内存 API 的 Win32（VirtualAlloc/Protect/Query）实现。_
| 函数 | 作用 | 状态 |
|---|---|---|
| `_hoox_memory_backend_init` | 后端初始化钩子（Windows 上为空操作） | 使用中 |
| `_hoox_memory_backend_deinit` | 后端反初始化钩子（空操作） | 使用中 |
| `_hoox_memory_backend_query_page_size` | 通过 GetSystemInfo 返回系统页大小 | 使用中 |
| `hoox_memory_is_readable` | 测试某区间是否可读 | 死代码 —— 导出 API，无调用者/测试 |
| `hoox_memory_query_protection` | 查询单个地址的保护属性 | 使用中 |
| `hoox_memory_read` | 通过 ReadProcessMemory 逐页拷贝目标内存 | 死代码 —— 导出 API，无调用者/测试 |
| `hoox_memory_write` | WriteProcessMemory 写入目标 | 死代码 —— 导出 API，无调用者/测试 |
| `hoox_memory_can_remap_writable` | 报告可写重映射支持情况（FALSE） | 使用中 |
| `hoox_memory_try_remap_writable_pages` | 将页重映射为可写（桩，返回 NULL） | 使用中 |
| `hoox_memory_dispose_writable_pages` | 释放重映射的页（桩） | 使用中 |
| `hoox_try_mprotect` | 通过 VirtualProtect 更改保护属性 | 使用中 |
| `hoox_clear_cache` | 对区间执行 FlushInstructionCache | 使用中 |
| `hoox_try_alloc_n_pages` | 分配 N 个页（任意位置） | 使用中 |
| `hoox_try_alloc_n_pages_near` | 在指定位置附近分配 N 个页 | 使用中 |
| `hoox_query_page_allocation_range` | 由 mem+size 填充 range 结构 | 使用中 |
| `hoox_free_pages` | 通过 VirtualFree 释放页块 | 使用中 |
| `hoox_memory_allocate` | 带重试的对齐 VirtualAlloc | 使用中 |
| `hoox_memory_allocate_near` | 在 near 地址的最大距离内分配 | 使用中 |
| `hoox_virtual_alloc` | VirtualAlloc，失败时回退到任意地址 | 使用中 |
| `hoox_memory_free` | VirtualFree MEM_RELEASE | 使用中 |
| `hoox_memory_release` | VirtualFree MEM_DECOMMIT | 使用中 |
| `hoox_memory_recommit` | 带保护属性的 VirtualAlloc MEM_COMMIT | 使用中 |
| `hoox_memory_discard` | 丢弃页（DiscardVirtualMemory/MEM_RESET） | 死代码 —— 导出 API，无调用者/测试 |
| `hoox_memory_decommit` | VirtualFree MEM_DECOMMIT | 死代码 —— 导出 API，无调用者/测试 |
| `hoox_memory_get_protection` | 查询跨页区间的保护属性 | 使用中 |
| `hoox_page_protection_from_windows` | 将 Win32 PAGE_* 映射为 HooxPageProtection | 使用中 |
| `hoox_page_protection_to_windows` | 将 HooxPageProtection 映射为 Win32 PAGE_* | 使用中 |

### `src/backend/windows/hooxtls-windows.c` — 已编译 ✅ · 278 loc · 14 funcs
_通过 TEB（fs/gs）访问 TLS 槽，并带一个临时槽回退表。_
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_tls_key_new` | 通过 TlsAlloc 分配一个 TLS key | 使用中 |
| `hoox_tls_key_free` | 通过 TlsFree 释放一个 TLS key | 使用中 |
| `_hoox_tls_init` | 清零临时 key 回退表 | 使用中 |
| `_hoox_tls_realize` | TLS realize 钩子（空操作） | 使用中 |
| `_hoox_tls_deinit` | TLS 反初始化钩子（空操作） | 使用中 |
| `hoox_tls_key_get_tmp_value` | 从临时槽回退表读取值 | 使用中 |
| `hoox_tls_key_set_tmp_value` | 将值存入临时槽回退表 | 使用中 |
| `hoox_tls_key_del_tmp_value` | 移除临时槽回退表项 | 使用中 |
| `hoox_tls_key_get_value` (32-bit) | 通过 fs 相对 TEB 读取 TLS 槽（i386） | 使用中 |
| `hoox_tls_key_set_value` (32-bit) | 通过 fs 相对 TEB 写入 TLS 槽（i386） | 使用中 |
| `hoox_tls_key_get_value` (64-bit) | 通过 gs 相对 TEB 读取 TLS 槽（x64） | 使用中 |
| `hoox_tls_key_set_value` (64-bit) | 通过 gs 相对 TEB 写入 TLS 槽（x64） | 使用中 |
| `hoox_tls_key_get_value` (generic) | TlsGetValue 回退（非 i386） | 使用中 |
| `hoox_tls_key_set_value` (generic) | TlsSetValue 回退（非 i386） | 使用中 |

_注：三对 `get_value`/`set_value` 是互斥的 `#if` 分支；每个目标恰好编译其中一对。_

### `src/backend/windows/hoox_process-windows.c` — 已编译 ✅ · 212 loc · 14 funcs
_精简的进程/线程/模块垫片（供链接的线程枚举+挂起，供测试的模块 range）。_
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_process_get_current_thread_id` | GetCurrentThreadId 封装 | 使用中 |
| `hoox_thread_get_system_error` | GetLastError 封装 | 使用中 |
| `hoox_thread_set_system_error` | SetLastError 封装 | 使用中 |
| `hoox_thread_suspend` | OpenThread + SuspendThread（仅链接路径） | 使用中 |
| `hoox_thread_resume` | OpenThread + ResumeThread（仅链接路径） | 使用中 |
| `_hoox_process_enumerate_threads` | 通过 Toolhelp 快照枚举自身线程 | 使用中 |
| `hoox_process_get_code_signing_policy` | 返回存储的代码签名策略 | 使用中 |
| `hoox_process_set_code_signing_policy` | 设置代码签名策略 | 死代码 —— 导出 API，无调用者/测试 |
| `hoox_main_module_get` | 惰性填充主模块名称/路径/range | 使用中 |
| `hoox_process_get_main_module` | 返回主模块垫片 | 死代码 —— 导出的测试支持 API，无调用者/测试 |
| `hoox_module_get_name` | 返回模块基名 | 死代码 —— 导出的测试支持 API，无调用者/测试 |
| `hoox_module_get_path` | 返回模块完整路径 | 死代码 —— 导出的测试支持 API，无调用者/测试 |
| `hoox_module_get_range` | 返回模块 base+size range | 使用中 |
| `hoox_module_find_export_by_name` | 在主模块上执行 GetProcAddress | 死代码 —— 导出 API，无调用者/测试 |

### `src/backend/windows/hoox_msvc_intrinsics.c` — 已编译 ✅ · 43 loc · 2 funcs
_段存储垫片，提供 clang/gcc 在 Windows 上省略的 MSVC intrinsics。_
| 函数 | 作用 | 状态 |
|---|---|---|
| `__writegsqword` | GS 相对 64 位存储（x64 汇编垫片） | 使用中 |
| `__writefsdword` | FS 相对 32 位存储（i386 汇编垫片） | 使用中 |

_注：互斥的 `#if` 分支 —— 每个目标架构编译其中一个。_

### `src/backend/x86/hooxcpucontext-x86.c` — 已编译 ✅ · 93 loc · 4 funcs
_ABI 感知的访问器，从已保存的 CpuContext 中读取调用参数/返回值。_
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_cpu_context_get_nth_argument` | 按 ABI 从寄存器/栈读取第 n 个参数 | 使用中 |
| `hoox_cpu_context_replace_nth_argument` | 覆写寄存器/栈中的第 n 个参数 | 使用中 |
| `hoox_cpu_context_get_return_value` | 读取返回值（eax/rax） | 使用中 |
| `hoox_cpu_context_replace_return_value` | 覆写返回值（eax/rax） | 使用中 |

### `src/backend/x86/hooxcpu-x86.c` — 已编译 ✅ · 98 loc · 3 funcs
_基于 CPUID 的带缓存查询，用于 AVX2 / CET 影子栈特性。_
| 函数 | 作用 | 状态 |
|---|---|---|
| `hoox_query_cpu_features` | 返回缓存的 CPU 特性标志（一次性初始化） | 使用中 |
| `hoox_do_query_cpu_features` | 通过 CPUID 计算 AVX2/CET-SS 标志 | 使用中 |
| `hoox_get_cpuid` | CPUID 封装（MSVC/gcc），带 level 边界检查 | 使用中 |

### `src/backend/x86/hooxinterceptor-x86.c` — 已编译 ✅ · 628 loc · 18 funcs
_拦截器的 x86 架构半部：trampoline 构建/激活 + enter/leave thunk。_
| 函数 | 作用 | 状态 |
|---|---|---|
| `_hoox_interceptor_backend_create` | 分配后端，初始化 writer/relocator，构建 thunk | 使用中 |
| `_hoox_interceptor_backend_destroy` | 拆除 thunk/writer/relocator，释放后端 | 使用中 |
| `_hoox_interceptor_backend_claim_grafted_trampoline` | 认领嫁接的 trampoline（不支持，FALSE） | 使用中 |
| `hoox_interceptor_backend_prepare_trampoline` | 分配 slice，计算 redirect 大小，检查可重定位性 | 使用中 |
| `_hoox_interceptor_backend_create_trampoline` | 发射 on-enter/leave/invoke trampoline + prologue 重定位 | 使用中 |
| `hoox_interceptor_backend_write_custom_redirect` | 将调用者提供的 redirect writer 运行进 redirect 代码 | 使用中 |
| `_hoox_interceptor_backend_destroy_trampoline` | 释放 trampoline 代码 slice | 使用中 |
| `_hoox_interceptor_backend_activate_trampoline` | 在函数 prologue 上写入 redirect jmp + NOP 填充 | 使用中 |
| `_hoox_interceptor_backend_deactivate_trampoline` | 恢复保存的原始 prologue 字节 | 使用中 |
| `_hoox_interceptor_backend_get_function_address` | 返回 context 的函数地址 | 使用中 |
| `_hoox_interceptor_backend_resolve_redirect` | 跟随相对/间接 jmp 到达 redirect 目标 | 使用中 |
| `_hoox_interceptor_backend_detect_hook_size` | 测量已有的 JMP hook + 尾随 NOP | 使用中 |
| `hoox_interceptor_backend_create_thunks` | 发射共享的 enter/leave thunk 代码 slice | 使用中 |
| `hoox_interceptor_backend_destroy_thunks` | 释放 enter/leave thunk slice | 使用中 |
| `hoox_emit_enter_thunk` | 发射 on-enter thunk（调用 begin_invocation，CET 路径） | 使用中 |
| `hoox_emit_leave_thunk` | 发射 on-leave thunk（调用 end_invocation） | 使用中 |
| `hoox_emit_prolog` | 发射 thunk prologue：保存 flags/regs/fxsave，对齐栈 | 使用中 |
| `hoox_emit_epilog` | 发射 thunk epilogue：fxrstor，恢复 regs，ret/jmp | 使用中 |
## 未参与 Windows x86/x64 编译（可删除 / 未来架构候选）

这些文件被排除在构建之外（src/CMakeLists.txt 只编译 x86 架构族 + Windows 后端）。它们是为未来多架构铺开而保留下来的 frida-gum ARM/ARM64 代码（见 docs/PLAN.md），外加一个游离的 core 文件。

| 文件 | 行数 | 函数数 | 作用 | 备注 |
|---|---|---|---|---|
| `src/arch/arm/hooxarmwriter.c` | 1279 | 81 | ARM (A32) 指令编码器（writer） | 未编译；ARM 未来架构 |
| `src/arch/arm/hooxarmrelocator.c` | 836 | 30 | ARM (A32) 指令重定位器（复制 + 修正 PC 相对） | 未编译；ARM 未来架构 |
| `src/arch/arm/hooxarmreader.c` | 139 | 4 | ARM (A32) 指令读取器 / 跳转目标探测 | 未编译；ARM 未来架构 |
| `src/arch/arm/hooxarmreg.c` | 59 | 1 | ARM 寄存器到元数据描述符的辅助函数（`hoox_arm_reg_describe`） | 未编译；ARM 未来架构 |
| `src/arch/arm/hooxthumbwriter.c` | 1817 | 95 | Thumb/Thumb-2 指令编码器（writer） | 未编译；ARM 未来架构 |
| `src/arch/arm/hooxthumbrelocator.c` | 1037 | 43 | Thumb/Thumb-2 指令重定位器（含 IT-block 处理） | 未编译；ARM 未来架构 |
| `src/arch/arm/hooxthumbreader.c` | 49 | 2 | Thumb 指令读取器 / 跳转目标探测 | 未编译；ARM 未来架构 |
| `src/arch/arm64/hooxarm64writer.c` | 2388 | 111 | ARM64 (AArch64) 指令编码器（writer） | 未编译；ARM64 未来架构 |
| `src/arch/arm64/hooxarm64relocator.c` | 798 | 29 | ARM64 指令重定位器（复制 + 修正 PC 相对） | 未编译；ARM64 未来架构 |
| `src/arch/arm64/hooxarm64reader.c` | 155 | 4 | ARM64 指令读取器 / 分支目标探测 | 未编译；ARM64 未来架构 |
| `src/backend/arm/hooxinterceptor-arm.c` | 1149 | 27 | ARM inline-hook 后端（为 A32/Thumb 构建跳板） | 未编译；ARM 未来架构 |
| `src/backend/arm/hooxcpucontext-arm.c` | 53 | 4 | ARM CpuContext 参数/返回值访问器 | 未编译；ARM 未来架构 |
| `src/backend/arm64/hooxinterceptor-arm64.c` | 1433 | 38 | ARM64 inline-hook 后端（构建跳板） | 未编译；ARM64 未来架构 |
| `src/backend/arm64/hooxcpucontext-arm64.c` | 53 | 4 | ARM64 CpuContext 参数/返回值访问器 | 未编译；ARM64 未来架构 |
| `src/core/hooxreturnaddress.c` | 50 | 2 | 返回地址符号化 + 地址数组比较辅助函数 | 未编译；游离的 core 文件（见下文） |

合计：15 个文件，11,295 行代码。

### `src/core/hooxreturnaddress.c`

该文件位于 `core/` 下（并非某个架构目录），但**不**在构建中。它提供两个辅助函数：

- `hoox_return_address_details_from_address` — 将返回地址解析为模块 / 函数 / 文件 / 行 / 列（回溯符号化）。
- `hoox_return_address_array_is_equal` — 比较两个返回地址数组。

它是一个**强烈建议删除的候选**：

- 它 `#include "hooxsymbolutil.h"` 并调用 `hoox_symbol_details_from_address` —— 但 `hooxsymbolutil.h` 在 `src/` 中**任何地方都不存在**（符号 / 调试符号解析已按 CLAUDE.md「明确不需要」被有意丢弃）。因此该文件按原样甚至无法编译。
- 回溯 / 符号化明确不在范围内（Backtracer 与符号解析都在丢弃清单上）。其头文件之所以还留着，唯一原因是 `core/hoox.h` 仍 `#include "hooxreturnaddress.h"`（第 18 行）—— 一个遗留的 facade include，其背后没有已编译的实现。

### 可能成为孤儿的头文件

尽力而为：`src/` 下仅被非编译源文件包含的 `.h` 文件（或者，对于 writer 头文件，仅被 `core/hooxcodeallocator.c` 中处于未激活的 `#ifdef HAVE_ARM`/`HAVE_ARM64` 架构守卫下的一个已编译源文件拉入 —— 对 x86/Windows 目标而言是死代码）：

- `src/arch/arm/hooxarmreg.h`
- `src/arch/arm/hooxarmreader.h`
- `src/arch/arm/hooxarmrelocator.h`
- `src/arch/arm/hooxarmwriter.h`（已编译的包含者 `hooxcodeallocator.c` 处于 `#ifdef HAVE_ARM` 守卫下）
- `src/arch/arm/hooxthumbreader.h`
- `src/arch/arm/hooxthumbrelocator.h`
- `src/arch/arm/hooxthumbwriter.h`（已编译的包含者 `hooxcodeallocator.c` 处于 `#ifdef HAVE_ARM` 守卫下）
- `src/arch/arm64/hooxarm64reader.h`
- `src/arch/arm64/hooxarm64relocator.h`
- `src/arch/arm64/hooxarm64writer.h`（已编译的包含者 `hooxcodeallocator.c` 处于 `#ifdef HAVE_ARM64` 守卫下）
- `src/core/hooxreturnaddress.h` —— 仅可通过 `core/hoox.h` 到达，但其实现（`hooxreturnaddress.c`）未被编译；与已丢弃的符号化相关联。
- `src/core/hooxsymbolutil.h` —— 被 `hooxreturnaddress.c` 引用，但**在源码树中缺失**（悬空 include，并非现存文件）。
