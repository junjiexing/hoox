# hoox API 参考

**中文** · [English](API_en.md) — 返回 [README](../README.md)

整个公共接口都在单个头文件里：

```c
#include "hoox.h"
```

所有声明都在 `extern "C"` 中，因此 C 和 C++ 都能使用。所有公共函数与类型都使用
`hoox_` / `Hoox` / `HOOX_` 前缀。

---

## 目录

- [快速上手](#快速上手)
- [构建期宏](#构建期宏)
- [标量类型](#标量类型)
- [库生命周期](#库生命周期)
- [拦截器 Interceptor](#拦截器-interceptor)
  - [获取与生命周期](#获取与生命周期)
  - [attach / detach](#attach--detach)
  - [replace / replace_fast / revert](#replace--replace_fast--revert)
  - [事务 Transaction](#事务-transaction)
  - [当前调用与调用栈](#当前调用与调用栈)
  - [线程范围控制](#线程范围控制)
  - [保存/恢复与加锁](#保存恢复与加锁)
- [调用监听器 Listener](#调用监听器-listener)
- [调用上下文 Invocation context](#调用上下文-invocation-context)
- [选项与枚举](#选项与枚举)
- [线程安全与并发](#线程安全与并发)

---

## 快速上手

```c
#include "hoox.h"

static int (*orig_open)(const char *, int);
static int my_open (const char * path, int flags) {
  return orig_open (path, flags);            /* 调用回原始函数 */
}

int main (void) {
  hoox_init ();
  HooxInterceptor * i = hoox_interceptor_obtain ();

  hoox_interceptor_replace (i, (hx_pointer) open, (hx_pointer) my_open,
      (hx_pointer *) &orig_open, NULL);
  /* ... 此时 open() 已被替换为 my_open() ... */
  hoox_interceptor_revert (i, (hx_pointer) open);

  hoox_interceptor_unref (i);
  hoox_deinit ();
}
```

覆盖每个功能、可运行的完整示例见
[`example/hook_example.c`](../example)。

---

## 构建期宏

在常见场景下，消费合并后的 `hoox.c`/`hoox.h` **不需要**任何 `-D`。只有在你想
偏离某个默认行为时才定义相应的宏：

| 宏 | 作用 |
|---|---|
| `HOOX_SHARED` | 以 Windows DLL 方式消费 hoox —— `HOOX_API` 变为 `__declspec(dllimport)`。默认是静态链接（`HOOX_API` 为空）。 |
| `HOOX_EXPORTS` | 在**构建** hoox DLL 时（配合 `HOOX_SHARED`）定义 → `__declspec(dllexport)`。 |
| `HOOX_USE_DLMALLOC` | 使用内置 dlmalloc 而非系统分配器。你需要自行在 include 路径上提供 `dlmalloc.c`。默认：系统 `malloc`。 |
| `HAVE_I386` / `HAVE_ARM` / `HAVE_ARM64`，`HAVE_WINDOWS` / `HAVE_LINUX` / `HAVE_DARWIN` | 强制指定目标架构/OS。通常会从编译器内置宏（`_M_X64`、`__aarch64__`、`_WIN32` 等）自动推导；仅当无法自动判断时才需要定义。 |

Windows 上需链接 `psapi`。

---

## 标量类型

贯穿整套 API 的轻量别名：

| 类型 | 定义 |
|---|---|
| `hx_int` | `int` |
| `hx_uint` | `unsigned int` |
| `hx_size` | `size_t` |
| `hx_boolean` | `int`（0 / 非 0） |
| `hx_pointer` | `void *` |
| `hx_constpointer` | `const void *` |
| `hx_uint8` / `hx_uint16` / `hx_uint32` / `hx_uint64` | 定宽无符号整数（`uintN_t`）——用于寄存器上下文字段 |
| `hx_float` / `hx_double` | `float` / `double`——用于向量寄存器视图 |
| `HxDestroyNotify` | `void (*)(hx_pointer data)` —— 清理回调 |

---

## 库生命周期

```c
void hoox_init (void);
void hoox_deinit (void);
void hoox_shutdown (void);
void hoox_init_embedded (void);
void hoox_deinit_embedded (void);
void hoox_prepare_to_fork (void);
void hoox_recover_from_fork_in_parent (void);
void hoox_recover_from_fork_in_child (void);
```

- **`hoox_init`** / **`hoox_deinit`** —— 启动 / 关闭库。使用任何其它 API 前先调
  一次 `hoox_init`；退出时调 `hoox_deinit`。
- **`hoox_shutdown`** —— 在 `hoox_deinit` 之前请求有序关闭内部工作线程（很少需要
  直接调用）。
- **`hoox_init_embedded`** / **`hoox_deinit_embedded`** —— 当 hoox 被嵌入到另一个
  管理进程级全局状态的运行时中时使用的初始化变体。
- **`hoox_prepare_to_fork`** / **`hoox_recover_from_fork_in_parent`** /
  **`..._in_child`** —— 包住一次 `fork()`，使内部锁/状态在父子进程中都保持一致
  （POSIX）。

---

## 拦截器 Interceptor

`HooxInterceptor` 是负责安装/移除 hook 的对象，是进程级单例，用
`hoox_interceptor_obtain` 获取。

### 获取与生命周期

```c
HooxInterceptor * hoox_interceptor_obtain (void);
HooxInterceptor * hoox_interceptor_ref (HooxInterceptor * self);
void hoox_interceptor_unref (HooxInterceptor * self);
void hoox_interceptor_set_default_options (HooxInterceptor * self,
    const HooxInterceptorOptions * options);
```

- **`hoox_interceptor_obtain`** —— 返回共享的拦截器（引用计数；首次调用时创建）。
  每次 `obtain` 都要对应一次 `unref`。
- **`hoox_interceptor_ref` / `_unref`** —— 调整引用计数；计数归零时销毁实例。
- **`hoox_interceptor_set_default_options`** —— 设置默认的
  [`HooxInterceptorOptions`](#选项与枚举)，应用于后续未覆盖它的 attach/replace 调用。

### attach / detach

```c
HooxAttachReturn hoox_interceptor_attach (HooxInterceptor * self,
    hx_pointer target, HooxInvocationListener * listener,
    const HooxAttachOptions * options);
void hoox_interceptor_detach (HooxInterceptor * self,
    HooxInvocationListener * listener);
```

- **`hoox_interceptor_attach`** —— 包裹 `target`，使 `listener` 的 `on_enter` 在
  原函数体之前触发、`on_leave` 在之后触发。原函数体仍会执行。`options` 可为
  `NULL`。返回 [`HooxAttachReturn`](#选项与枚举)（成功为 `HOOX_ATTACH_OK`）。
  - 同一目标可挂多个 listener（有一个较小的固定上限）。
  - 在回调里通过[调用上下文](#调用上下文-invocation-context)检查/修改参数、返回值等。
- **`hoox_interceptor_detach`** —— 把之前挂上的 `listener` 从它挂过的每个目标移除。

### replace / replace_fast / revert

```c
HooxReplaceReturn hoox_interceptor_replace (HooxInterceptor * self,
    hx_pointer function_address, hx_pointer replacement_function,
    hx_pointer * original_function, const HooxReplaceOptions * options);
HooxReplaceReturn hoox_interceptor_replace_fast (HooxInterceptor * self,
    hx_pointer function_address, hx_pointer replacement_function,
    hx_pointer * original_function, const HooxInterceptorOptions * options);
void hoox_interceptor_revert (HooxInterceptor * self, hx_pointer target);
```

- **`hoox_interceptor_replace`** —— 把 `function_address` 重定向到
  `replacement_function`。若 `original_function` 非 NULL，它会收到一个可调用的
  trampoline，用于回到原始函数，因此替换函数可以调用原始实现。`options` 可为
  `NULL`；通过 [`HooxReplaceOptions`](#选项与枚举) 可附带 `replacement_data`
  （在替换函数内通过 `hoox_interceptor_get_current_invocation` +
  `hoox_invocation_context_get_replacement_data` 读取）。返回
  [`HooxReplaceReturn`](#选项与枚举)。
- **`hoox_interceptor_replace_fast`** —— 更轻量的变体，用尽可能小的 trampoline
  直接重定向到 `replacement_function`，且不做调用上下文簿记。经由
  `original_function` 拿到回原实现的 trampoline，语义相同。当你在替换函数里不需要
  `get_current_invocation` 时用它。
- **`hoox_interceptor_revert`** —— 撤销对 `target` 的 `attach`/`replace`/
  `replace_fast`，还原原始字节。

> 同一目标只能被 *attach*（挂 listener）**或** *replace*，两者不可兼得。

### 事务 Transaction

```c
void hoox_interceptor_begin_transaction (HooxInterceptor * self);
void hoox_interceptor_end_transaction (HooxInterceptor * self);
hx_boolean hoox_interceptor_flush (HooxInterceptor * self);
```

- **`hoox_interceptor_begin_transaction` / `_end_transaction`** —— 把多个
  attach/detach/replace/revert 归为一组。两者之间的改动会被*暂存*，在
  `end_transaction` 时一起提交 —— 更快，且从目标角度看是原子的。事务可嵌套
  （按层计数）；最外层的 `end` 执行提交。事务之外，每个改动立即生效。
- **`hoox_interceptor_flush`** —— 强制把任何挂起的 trampoline/代码改动立即变为可见。
  返回是否有内容被刷新。

### 当前调用与调用栈

```c
HooxInvocationContext * hoox_interceptor_get_current_invocation (void);
HooxInvocationStack * hoox_interceptor_get_current_stack (void);
hx_pointer hoox_invocation_stack_translate (HooxInvocationStack * self,
    hx_pointer return_address);
```

- **`hoox_interceptor_get_current_invocation`** —— 在 listener 回调或 `replace`
  的替换函数内，返回当前的
  [`HooxInvocationContext`](#调用上下文-invocation-context)，若不在被拦截的调用内
  则返回 `NULL`。
- **`hoox_interceptor_get_current_stack`** —— 当前线程的调用栈（不透明句柄），配合
  `hoox_invocation_stack_translate` 使用。
- **`hoox_invocation_stack_translate`** —— 把落在 hoox trampoline 内的
  `return_address` 映射回真正的调用者地址（在展开/符号化经过 hook 的栈时有用）。

### 线程范围控制

```c
void hoox_interceptor_ignore_current_thread (HooxInterceptor * self);
void hoox_interceptor_unignore_current_thread (HooxInterceptor * self);
void hoox_interceptor_ignore_other_threads (HooxInterceptor * self);
void hoox_interceptor_unignore_other_threads (HooxInterceptor * self);
```

- **`ignore_current_thread` / `unignore_current_thread`** —— 处于 ignore 期间，
  当前线程执行被 hook 的函数时**不触发** listener。可嵌套。经典用途是避免你自己的
  回调调用了被你 hook 的函数而导致递归。
- **`ignore_other_threads` / `unignore_other_threads`** —— 反向：抑制**除当前线程
  之外**所有线程的 listener（例如只观察自己这条线程）。

### 保存/恢复与加锁

```c
void hoox_interceptor_save (HooxInvocationState * state);
void hoox_interceptor_restore (HooxInvocationState * state);
void hoox_interceptor_with_lock_held (HooxInterceptor * self,
    HooxInterceptorLockedFunc func, hx_pointer user_data);
hx_boolean hoox_interceptor_is_locked (HooxInterceptor * self);
```

- **`hoox_interceptor_save` / `_restore`** —— 在一段区域前后保存并恢复每线程的
  ignore 状态（以便临时修改后精确还原）。
- **`hoox_interceptor_with_lock_held`** —— 在持有拦截器内部锁的情况下运行
  `func(user_data)`（进阶用法；用于把你自己的状态与 hook 安装协调一致）。
- **`hoox_interceptor_is_locked`** —— 当前是否持有拦截器锁。

---

## 调用监听器 Listener

listener 就是 `attach` 在进入/离开时调用的对象。有两个现成构造函数，外加一个用于
自定义 listener 的 vtable 接口。

```c
typedef void (* HooxInvocationCallback) (HooxInvocationContext * context,
    hx_pointer user_data);

HooxInvocationListener * hoox_make_call_listener (
    HooxInvocationCallback on_enter, HooxInvocationCallback on_leave,
    hx_pointer data, HxDestroyNotify data_destroy);
HooxInvocationListener * hoox_make_probe_listener (
    HooxInvocationCallback on_hit, hx_pointer data, HxDestroyNotify data_destroy);

HooxInvocationListener * hoox_invocation_listener_ref (HooxInvocationListener * self);
void hoox_invocation_listener_unref (HooxInvocationListener * self);
```

- **`hoox_make_call_listener`** —— 带有独立 `on_enter` 与 `on_leave` 回调的
  listener（两者都可为 `NULL`）。`data` 会传给两个回调；`data_destroy`（可为
  `NULL`）在 listener 销毁时对它调用。
- **`hoox_make_probe_listener`** —— 更轻量的**仅进入**（enter-only）listener：
  `on_hit` 每次调用在进入时触发一次。适合计数/追踪。
- **`hoox_invocation_listener_ref` / `_unref`** —— 引用计数。`unref` 到零时会调用
  listener 的 `finalize` 并释放它。`detach` 之后，对你创建的 listener 调用 `unref`。

### 自定义 listener（vtable）

要完全掌控，把 `HooxInvocationListener` 作为你结构体的**第一个**成员，并用一个
vtable 初始化它：

```c
struct _HooxInvocationListenerInterface {
  void (* on_enter) (HooxInvocationListener * self, HooxInvocationContext * ctx);
  void (* on_leave) (HooxInvocationListener * self, HooxInvocationContext * ctx);
};

void hoox_invocation_listener_init (HooxInvocationListener * self,
    const HooxInvocationListenerInterface * iface, HxDestroyNotify finalize);

#define HOOX_INVOCATION_LISTENER(obj) ((HooxInvocationListener *) (obj))
```

- 在堆上分配你的结构体（用 `malloc`/`calloc`）；`unref` 会先 `finalize`（用于清理
  内嵌资源，可为 `NULL`）再释放它。
- 同一个函数可同时作为 `on_enter` 和 `on_leave`；用
  [`hoox_invocation_context_get_point_cut`](#调用上下文-invocation-context)分辨。
- 把 `HOOX_INVOCATION_LISTENER(你的指针)` 传给 `attach`/`detach`/`unref`。
- **`hoox_invocation_listener_on_enter` / `_on_leave`** 会分派到 vtable；通常你
  不需要自己调用它们。

---

## 调用上下文 Invocation context

`HooxInvocationContext` 会传给每个 listener 回调（在 `replace` 的替换函数内也可
通过 `hoox_interceptor_get_current_invocation` 拿到）。

公共字段：

```c
struct _HooxInvocationContext {
  hx_pointer       function;      /* 被拦截函数（或被挂钩指令）的地址 */
  HooxCpuContext * cpu_context;   /* 挂钩点的寄存器状态（见下，布局公开） */
  hx_int           system_error;  /* 本次调用的 errno / GetLastError() */
  /* ... 私有 ... */
};
```

访问器：

```c
HooxPointCut hoox_invocation_context_get_point_cut (HooxInvocationContext *);

hx_pointer hoox_invocation_context_get_nth_argument (HooxInvocationContext *, hx_uint n);
void       hoox_invocation_context_replace_nth_argument (HooxInvocationContext *, hx_uint n, hx_pointer value);
hx_pointer hoox_invocation_context_get_return_value (HooxInvocationContext *);
void       hoox_invocation_context_replace_return_value (HooxInvocationContext *, hx_pointer value);
hx_pointer hoox_invocation_context_get_return_address (HooxInvocationContext *);
hx_uint    hoox_invocation_context_get_thread_id (HooxInvocationContext *);
hx_uint    hoox_invocation_context_get_depth (HooxInvocationContext *);

hx_pointer hoox_invocation_context_get_listener_thread_data (HooxInvocationContext *, hx_size size);
hx_pointer hoox_invocation_context_get_listener_function_data (HooxInvocationContext *);
hx_pointer hoox_invocation_context_get_listener_invocation_data (HooxInvocationContext *, hx_size size);
hx_pointer hoox_invocation_context_get_replacement_data (HooxInvocationContext *);
```

- **`get_point_cut`** —— `HOOX_POINT_ENTER` 或 `HOOX_POINT_LEAVE`；让一个回调区分
  进入与离开。
- **`get_nth_argument` / `replace_nth_argument`** —— 读取 / 覆写第 `n` 个
  整型/指针参数（按调用约定）。参数以 `hx_pointer` 读出，按需强转
  （如 `(int)(intptr_t)`）。
- **`get_return_value` / `replace_return_value`** —— 读取 / 覆写返回值
  （在 `on_leave` 中有意义）。
- **`get_return_address`** —— 被拦截函数将要返回到的地址。
- **`get_thread_id`** —— 当前调用所在的 OS 线程 id。
- **`get_depth`** —— 本线程被拦截调用的嵌套深度（最外层为 0）。
- **数据槽** —— 绑定不同生命周期的暂存内存：
  - **`get_listener_function_data`** —— 你在
    `HooxAttachOptions.listener_function_data` 里设置的每次挂载的指针。
  - **`get_listener_thread_data (size)`** —— 零初始化的、每（listener，线程）一份、
    `size` 字节的缓冲；在该线程上跨调用保持。
  - **`get_listener_invocation_data (size)`** —— 零初始化的、每次调用一份、`size`
    字节的缓冲；在 `on_enter` 与 `on_leave` 中是**同一个**指针，可用它把状态从进入
    传到离开（计时、保存的参数等）。
  - **`get_replacement_data`** —— 通过 `replace` 安装的函数所对应的
    `HooxReplaceOptions.replacement_data` 指针。

### 寄存器上下文 CPU context

`context->cpu_context` 指向挂钩点处捕获的完整寄存器状态。其布局在公共头
`hoox.h` 中**公开**（`HooxCpuContext` 按目标架构展开为
`HooxArm64CpuContext` / `HooxX64CpuContext` / `HooxArmCpuContext` /
`HooxIA32CpuContext`），因此可以直接读取、修改寄存器——包括 frida 那样把 probe
挂在**任意指令地址**（不限函数入口）后打印该处的寄存器：

```c
static void on_hit (HooxInvocationContext * ic, hx_pointer u) {
  HooxCpuContext * c = ic->cpu_context;
#if defined (__aarch64__)
  printf ("pc=%llx x0=%llx sp=%llx\n",
      (unsigned long long) c->pc, (unsigned long long) c->x[0],
      (unsigned long long) c->sp);
#endif
}
/* 把探针挂在函数中间的某条指令上（arm64 定长指令，entry+8 即一条边界） */
hoox_interceptor_attach (itc,
    (hx_pointer) ((hx_uint32 *) (void *) fn + 2),
    hoox_make_probe_listener (on_hit, NULL, NULL), NULL);
```

各架构的字段（与 `hoox.h` 一致）：`HooxArm64CpuContext` 有
`pc/sp/nzcv/x[29]/fp/lr/v[32]`；`HooxX64CpuContext` 有 `rip/r8..r15/rdi/rsi/
rbp/rsp/rbx/rdx/rcx/rax`；`HooxArmCpuContext` 有 `pc/sp/cpsr/r8..r12/v[16]/
r[8]/lr`；`HooxIA32CpuContext` 有 `eip/edi/esi/ebp/esp/ebx/edx/ecx/eax`。写回
（如改 `x[0]`）会在恢复执行时生效。高层的 `get_nth_argument` /
`get_return_value` 也是读写这块上下文的便捷封装。

> **任意指令位置挂钩**：`attach` 的 `target` 只需是一个可重定位的指令边界，
> 不必是函数入口——约束与 frida 相同（被覆盖的字节可重定位、且没有其他分支跳入
> 该区间）。`hoox_make_probe_listener`（只有 `on_enter`）是任意位置探针的干净原语；
> 若用带 `on_leave` 的 call listener，其“离开”语义是“所在函数返回时”。

---

## 选项与枚举

```c
typedef struct {
  hx_int                  scratch_register;
  HooxInterceptorScenario scenario;
  HooxRelocationPolicy    relocation_policy;
  HooxWriteRedirectFunc   write_redirect;
  hx_pointer              write_redirect_data;
  hx_uint                 redirect_space_hint;
} HooxInterceptorOptions;

typedef struct {
  HooxInterceptorOptions     instrumentation;
  hx_pointer                 listener_function_data;
  HooxInvocationIgnorability ignorability;
} HooxAttachOptions;

typedef struct {
  HooxInterceptorOptions instrumentation;
  hx_pointer             replacement_data;
} HooxReplaceOptions;
```

- **`HooxInterceptorOptions`** —— attach/replace 共用的底层旋钮。
  `scratch_register` 选择 trampoline 可以破坏的寄存器（或用默认）；`scenario` 与
  `relocation_policy` 控制 prologue 重定位的保守程度；`write_redirect`/
  `write_redirect_data` 允许你提供自定义的重定向写入器；`redirect_space_hint` 限定
  prologue 扫描范围。把结构体清零即可得到合理默认值。
- **`HooxAttachOptions`** —— `listener_function_data` 会成为
  `get_listener_function_data`；`ignorability` 标记此 hook 是否可被线程 ignore 抑制。
- **`HooxReplaceOptions`** —— `replacement_data` 会成为 `get_replacement_data`。

枚举：

| 枚举 | 取值 | 含义 |
|---|---|---|
| `HooxInterceptorScenario` | `DEFAULT`、`ONLINE`、`OFFLINE` | 其它线程是否可能正在执行目标。**`ONLINE`**（默认）保守改写；**`OFFLINE`** 在目标静默时（如刚 spawn、仍挂起的进程）允许更激进的改写。 |
| `HooxInvocationIgnorability` | `IGNORABLE`、`UNIGNORABLE` | `ignore_*_thread` 是否可以抑制此 hook。 |
| `HooxRelocationPolicy` | `DEFAULT`、`CHECKED`、`UNCHECKED`、`FORCED` | prologue 重定位校验的严格程度。 |
| `HooxRedirectWriteResult` | `WRITTEN`、`DECLINED` | 自定义 `HooxWriteRedirectFunc` 的返回。 |
| `HooxAttachReturn` | `OK(0)`、`WRONG_SIGNATURE(-1)`、`ALREADY_ATTACHED(-2)`、`POLICY_VIOLATION(-3)`、`WRONG_TYPE(-4)`、`TOO_MANY_LISTENERS(-5)` | `attach` 的结果。`TOO_MANY_LISTENERS` 表示目标已挂满 `HOOX_MAX_LISTENERS_PER_FUNCTION`（默认 2）个监听器；可用 `-DHOOX_MAX_LISTENERS_PER_FUNCTION=N` 提高上限。 |
| `HooxReplaceReturn` | `OK(0)`、`WRONG_SIGNATURE(-1)`、`ALREADY_REPLACED(-2)`、`POLICY_VIOLATION(-3)`、`WRONG_TYPE(-4)` | `replace`/`replace_fast` 的结果。 |

`HooxPointCut` 为 `HOOX_POINT_ENTER` / `HOOX_POINT_LEAVE`。

---

## 线程安全与并发

- **稳态是线程安全的。** hook 一旦安装，任意数量的线程都可并发调用被 hook 的函数。
  listener 分派使用无锁的写时复制（COW）listener 集合和每线程调用栈。
- **可重入。** 一个自身又调用了被 hook 函数的 listener 回调，不会在该线程上再次递归
  进入 listener —— 如需显式控制，用
  `hoox_interceptor_ignore_current_thread` / `_unignore_current_thread` 包住这类调用。
- **安装/移除对正在运行的目标并非完全原子。** hoox（与 frida-gum 一样）在
  Windows/Linux 上**不**挂起其它线程。它把 trampoline 在旁边建好、只重定位最少量的
  prologue，并在默认的 `SCENARIO_ONLINE` 下拒绝越过某条 `CALL` 去重定位——因为可能有
  别的线程正等着从那里返回到被补丁的区域。仍存在一个极小的窗口：某线程恰在补丁写入
  的瞬间执行正被覆盖的那几个字节时可能出错。这与 Microsoft Detours 不同——Detours 会
  挂起所有线程并改写它们的指令指针。实践建议：在静默时机安装/移除 hook；或者当你需要
  Detours 级别的保证时，自行在事务前后挂起其它线程。
- **事务**让*一批*改动一起提交，是安装大量 hook 的高效方式，但它不额外提供线程挂起。
