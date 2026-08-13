# cbuspp — 现代 C++23 事件总线库

cbuspp 是一个**仅头文件**的现代 C++23 事件总线库，提供类型安全、高性能的发布/订阅（pub/sub）消息传递机制。它支持同步和异步事件分发、延迟调度、丰富的消息过滤系统、递归发布，以及 monadic 值包装器。

## 目录

- [核心概念](#核心概念)
- [快速开始](#快速开始)
- [编译要求](#编译要求)
- [核心类型详解](#核心类型详解)
  - [value\<T\> — Monadic 值包装器](#valuet--monadic-值包装器)
  - [shared_value\<T\> — 共享值包装器](#shared_valuet--共享值包装器)
  - [event_tag — 事件标签](#event_tag--事件标签)
  - [context — 事件上下文](#context--事件上下文)
- [Bus 事件总线](#bus-事件总线)
  - [创建 Bus](#创建-bus)
  - [同步发布 (sync)](#同步发布-sync)
  - [异步发布 (async)](#异步发布-async)
  - [订阅事件 (on)](#订阅事件-on)
  - [取消订阅 (unsubscribe)](#取消订阅-unsubscribe)
  - [查询订阅状态](#查询订阅状态)
  - [手动绑定 (bind)](#手动绑定-bind)
- [Router — 上下文链式设置](#router--上下文链式设置)
- [Filter — 过滤器系统](#filter--过滤器系统)
  - [内置过滤器方法](#内置过滤器方法)
  - [自定义谓词 (where)](#自定义谓词-where)
  - [谓词组合子](#谓词组合子)
  - [谓词类型参考](#谓词类型参考)
- [回调签名重载](#回调签名重载)
  - [自由函数 / Lambda](#自由函数--lambda)
  - [成员函数指针](#成员函数指针)
- [Scheduler — 调度器](#scheduler--调度器)
- [递归发布](#递归发布)
  - [同步递归](#同步递归)
  - [异步递归](#异步递归)
  - [混合递归](#混合递归)
- [适用场景](#适用场景)
- [最佳实践](#最佳实践)
- [注意事项与限制](#注意事项与限制)
- [完整 API 参考](#完整-api-参考)

---

## 核心概念

cbuspp 围绕以下几个核心概念构建：

| 概念 | 说明 |
|------|------|
| **事件标签** (`event_tag`) | 编译时事件类型标识，包含事件名和值类型 |
| **值包装器** (`value<T>` / `shared_value<T>`) | 携带事件数据，提供 monadic 操作链 |
| **上下文** (`context`) | 事件的元数据：优先级、ID、标签位、追踪信息等 |
| **路由器** (`router`) | 发布事件时的链式配置器，设置上下文 |
| **过滤器** (`filter`) | 订阅时配置的匹配条件，控制回调是否触发 |
| **谓词** (`predicate`) | 可组合的布尔条件，作用于上下文 |
| **调度器** (`scheduler`) | 异步延迟/定时执行引擎 |

**数据流向图：**

```
Publisher                    Bus                      Subscribers
─────────                  ───────                   ────────────
post<Tag>(value)           do_sync /             ┌─► callback_1
  .with_priority(5)  ───►  do_async        ───┤──► callback_2
  .with_id(42)             iterates               └─► callback_3
  .with_tags(0xF)          applies filter              (if filter
  .sync() / .async()       to each sub)                 matches)
```

---

## 快速开始

```cpp
#include "cbuspp.hpp"
#include <cstdio>

using namespace cbuspp;

// 1. 定义事件标签
struct log_event : event_tag<"log", value<std::string>> {};

// 2. 创建一个直接执行的 executor
struct inline_executor {
    void post(std::function<void()> f) { f(); }
};

int main() {
    inline_executor ex;
    bus b(ex);

    // 3. 订阅事件
    b.on<log_event>().call([](const value<std::string>& msg, const context& ctx) {
        std::printf("[priority=%d] %s\n",
                    ctx.priority.value_or(0),
                    (*msg).c_str());
    });

    // 4. 发布事件
    b.post<log_event>(value<std::string>("你好，世界！"))
        .with_priority(5)
        .sync();

    return 0;
}
```

---

## 编译要求

- **C++23**（需要 `std::flat_map`、deducing this、`std::move_only_function` 等特性）
- 支持 C++23 的编译器：
  - GCC 14+
  - Clang 18+
  - Apple Clang 16+（部分功能可能需要 Homebrew clang）
- CMake 3.20+

**CMake 集成：**

```cmake
add_subdirectory(cbuspp)
target_link_libraries(your_target PRIVATE cbuspp::cbuspp)
```

---

## 核心类型详解

### value\<T\> — Monadic 值包装器

`value<T>` 是一个类似于 Rust `Option` 或 Haskell `Maybe` 的值包装器，用于携带事件数据。它提供 monadic 操作链，支持函数式编程风格。

#### 特化版本

| 类型 | 说明 |
|------|------|
| `value<void>` | 无值标记，等价于 `std::monostate`，始终为"有值"状态 |
| `value<T>` (T 为对象类型) | 持有具体值，提供解引用和成员访问 |
| `value<T&>` | 引用包装器，不拥有数据，可空 |

#### 构造方式

```cpp
// 默认构造（T 需支持默认构造）
value<int> v1;                    // *v1 == 0

// 值构造（注意：构造函数是 explicit）
value<int> v2(42);                // *v2 == 42
value<std::string> v3("hello");   // *v3 == "hello"

// 移动构造
value<std::string> v4(std::string("world"));

// in_place 构造（转发参数给 T 的构造函数）
value<std::string> v5(std::in_place, 5, 'x');  // "xxxxx"

// 引用包装
int x = 100;
value<int&> ref(x);               // *ref == 100
value<int&> ref2(std::ref(x));    // 等价的
```

#### 解引用

```cpp
value<int> v(42);
int x = *v;           // 42
v->some_method();     // 当 T 是类类型时可用

value<std::string> sv(std::string("test"));
sv->size();           // 4
```

#### Monadic 操作

`value<T>` 提供三个核心 monadic 操作，支持函数式链式处理：

##### `transform` — 映射转换（对应 Haskell 的 `fmap`）

```cpp
value<int> v(10);

// 值 → 值
auto r1 = v.transform([](int x) { return x * 2; });
// r1 类型: value<int>，*r1 == 20

// 值 → 无值（副作用）
bool called = false;
auto r2 = v.transform([&](int x) { called = true; });
// r2 类型: value<void>

// 链式转换
auto r3 = v
    .transform([](int x) { return x + 1; })       // value<int>(11)
    .transform([](int x) { return x * 10; })      // value<int>(110)
    .transform([](int x) { return std::to_string(x); }); // value<string>("110")
```

##### `and_then` — 平坦映射（对应 Haskell 的 `>>=`）

```cpp
value<int> v(5);

// 返回任意类型，不包装在 value 中
auto r = v.and_then([](int x) -> std::string {
    return std::to_string(x + 1);
});
// r 类型: std::string，r == "6"
```

##### `inspect` — 检查（纯副作用，返回自身）

```cpp
value<int> v(42);
int seen = 0;

auto r = v.inspect([&](int x) { seen = x; });
// seen == 42，r 仍为 value<int>(42)，链可继续
```

#### 布尔转换

```cpp
value<int> v(10);
if (v) { /* 始终为 true，非空 */ }

value<int&> ref;
if (!ref) { /* 空引用时为 false */ }
```

#### 交换

```cpp
value<int> a(1), b(2);
a.swap(b);    // *a == 2, *b == 1
```

---

### shared_value\<T\> — 共享值包装器

`shared_value<T>` 继承自 `value<T>` 的 void 特化（对于 void 类型），或使用 `std::shared_ptr<T>` 实现**写时复制（COW）**语义。适用场景：

- 需要在异步回调间共享数据
- 需要多订阅者读取、按需复制

> **重要区分：** `shared_value<T>` 只能通过 `async()` 发布，`value<T>` 只能通过 `sync()` 发布。`value<void>`（monostate）两者均可。

#### 构造

```cpp
// 默认构造（空指针）
shared_value<int> v1;        // !v1, use_count() == 0

// 值构造（explicit）
shared_value<int> v2(42);    // *v2 == 42, use_count() == 1

// in_place 构造
shared_value<std::string> v3(std::in_place, "hello");
```

#### 读/写与 COW

```cpp
shared_value<int> v1(10);
shared_value<int> v2 = v1;     // 共享底层数据
// v1.use_count() == 2, v2.use_count() == 2

// 写操作触发 COW：v1 先复制一份再修改
*v1 = 20;
// v1.read() == 20, v2.read() == 10  ← 互不影响
// v1.unique() == true （v1 现在是独立副本）
```

#### 方法

| 方法 | 说明 |
|------|------|
| `read()` | 返回 `const T&`，不触发 COW |
| `write()` | 返回 `T&`，可能触发 COW |
| `unique()` | 当前是否为唯一持有者 |
| `use_count()` | 共享计数（参考 `std::shared_ptr`） |
| `operator*` | 非 const 时触发 COW，const 时仅读取 |
| `operator->` | 同上 |
| `transform()` | 同 `value<T>`，非 const 时触发 COW |
| `modify()` | 接受 `F(T&)` 就地修改，可能触发 COW |
| `inspect()` | 只读检查 |
| `and_then()` | 平坦映射 |

---

### event_tag — 事件标签

`event_tag` 在编译时定义事件类型，包含两个要素：**名称**（字符串）和**值类型**。

```cpp
// 无值事件
struct void_event : event_tag<"void_event"> {};
// Tag::value_t = std::monostate

// 带值事件（同步发布）
struct int_event : event_tag<"int_event", value<int>> {};
// Tag::value_t = value<int>  → 只能 sync()

// 带值事件（异步发布）
struct shared_int_event : event_tag<"shared_int", shared_value<int>> {};
// Tag::value_t = shared_value<int>  → 只能 async()
```

**约束：**

| value 类型 | `sync()` | `async()` |
|-----------|----------|-----------|
| `std::monostate`（无值） | ✅ | ✅ |
| `value<T>` | ✅ | ❌ |
| `shared_value<T>` | ❌ | ✅ |

---

### context — 事件上下文

`context` 携带事件的元数据，所有字段均为 `std::optional`，未设置时不参与匹配。

```cpp
struct context {
    std::optional<std::int32_t>  priority;        // 优先级
    std::optional<duration_t>    delay_for;       // 延迟时长
    std::optional<time_point_t>  execute_at;      // 定时执行时间点
    std::optional<std::uint64_t> id;              // 事件ID
    std::optional<std::uint64_t> causation_id;    // 因果链ID
    std::optional<std::uint64_t> tags;            // 标签位掩码
    std::optional<std::string>   trace_id;        // 分布式追踪ID
    std::optional<std::uint64_t> span_id;         // Span ID
    std::optional<std::uint64_t> parent_span_id;  // 父 Span ID
};
```

---

## Bus 事件总线

`bus<Executor>` 是整个库的核心。它参数化于 `Executor` 类型，后者需满足 `executor_t` 概念。

### 创建 Bus

```cpp
// 直接执行的 executor（适用于同步测试或单线程场景）
struct inline_executor {
    void post(std::function<void()> f) { f(); }
};

inline_executor ex;
bus b(ex);
```

`executor_t` 概念要求：

```cpp
template <typename E>
concept executor_t = requires(E& e) {
    { e.post([]() { }) } -> std::same_as<void>;
};
```

### 同步发布 (sync)

同步发布会**立即**遍历所有匹配的订阅者并调用其回调，调用线程阻塞直到所有回调返回。

```cpp
// 无值事件
b.post<void_event>().sync();

// 带值事件
b.post<int_event>(value<int>(42)).sync();

// 带上下文的事件
b.post<int_event>(value<int>(99))
    .with_priority(5)
    .with_id(12345)
    .with_tags(0xABCD)
    .sync();
```

**适用场景：** 需要严格顺序执行、请求-响应模式、在同一个调用栈中完成所有处理。

### 异步发布 (async)

异步发布将回调投递到 executor，调用立即返回，回调在 executor 的上下文中执行。

```cpp
// 无值异步
b.post<void_event>().async();

// 带 shared_value 的异步
b.post<shared_int_event>(shared_value<int>(123)).async();

// 延迟执行
b.post<void_event>().delay_for(50ms).async();

// 定时执行
b.post<void_event>().execute_at(steady_clock::now() + 1s).async();
```

**适用场景：** 解耦发布者和订阅者、IO 密集型处理、需要线程池调度、延迟/定时任务。

### 订阅事件 (on)

`on<Tag>()` 返回一个 `filter<Tag>` 对象，通过链式调用配置过滤条件，最后调用 `.call()` 绑定回调。

```cpp
// 最简单的订阅
b.on<void_event>().call([] {
    // 处理事件
});

// 带过滤条件的订阅
b.on<int_event>()
    .min_priority(5)
    .require_id(100)
    .call([](const value<int>& v) {
        // 只在 priority >= 5 且 id == 100 时触发
    });
```

### 取消订阅 (unsubscribe)

```cpp
auto id = b.on<void_event>().call([] { /* ... */ });

// 取消订阅，返回被取消的 ID
b.unsubscribe(id);

// 取消不存在的 ID 返回 0
assert(b.unsubscribe(99999) == 0);

// 取消后再次发布不会触发该回调
b.post<void_event>().sync();  // 回调不会执行
```

### 查询订阅状态

```cpp
// 模板参数化于事件标签
b.subscriber_count<void_event>();   // 返回订阅者数量
b.has_subscribers<int_event>();     // 是否有订阅者
```

### 手动绑定 (bind)

当需要完全自定义 `subscriber` 结构时，可以使用 `bind`：

```cpp
auto cb = task_t([&](const void*, const context&) {
    // 处理逻辑
});

bus<decltype(ex)>::subscriber sub{
    .callback = std::make_shared<task_t>(std::move(cb)),
    .filter = always_pred{},
};

auto id = b.bind<int_event>(std::move(sub));
```

---

## Router — 上下文链式设置

`router<Tag>` 由 `post<Tag>()` 返回，用于链式设置上下文参数。所有方法返回 `*this`，支持任意顺序的链式调用。

| 方法 | 上下文字段 | 说明 |
|------|-----------|------|
| `.with_priority(n)` | `priority` | 设置事件优先级 |
| `.delay_for(d)` | `delay_for` | 设置延迟时长 |
| `.execute_at(t)` | `execute_at` | 设置定时执行时间点 |
| `.with_id(id)` | `id` | 设置事件ID |
| `.caused_by(id)` | `causation_id` | 设置因果链ID |
| `.with_tags(tags)` | `tags` | 设置标签位掩码 |
| `.with_trace_id(s)` | `trace_id` | 设置分布式追踪ID |
| `.with_span_id(id)` | `span_id` | 设置 Span ID |
| `.with_parent_span_id(id)` | `parent_span_id` | 设置父 Span ID |
| `.with_context(ctx)` | 全量覆盖 | 移动整个 context 对象 |
| `.sync()` | — | 同步发布 |
| `.async()` | — | 异步发布 |

**示例：**

```cpp
b.post<int_event>(value<int>(42))
    .with_priority(3)
    .with_id(0xDEAD)
    .caused_by(0xBEEF)
    .with_tags(0xCAFE)
    .with_trace_id("trace-1")
    .with_span_id(0xAAAA)
    .with_parent_span_id(0xBBBB)
    .sync();
```

---

## Filter — 过滤器系统

`filter<Tag>` 由 `on<Tag>()` 返回，用于链式配置过滤条件。过滤器之间是 **AND** 关系——只有全部条件满足时回调才会触发。

### 内置过滤器方法

| 方法 | 谓词类型 | 匹配逻辑 |
|------|---------|---------|
| `.min_priority(p)` | `field_pred<..., std::greater_equal<>>` | `pub >= sub` |
| `.require_id(id)` | `field_pred<..., std::equal_to<>>` | `pub == sub` |
| `.require_causation_id(id)` | `field_pred` | 精确匹配 |
| `.and_tags(mask)` | `bits_pred<bits_match::all>` | `(pub & mask) == mask` |
| `.or_tags(mask)` | `bits_pred<bits_match::any>` | `(pub & mask) != 0` |
| `.not_tags(mask)` | `bits_pred<bits_match::none>` | `(pub & mask) == 0` |
| `.require_trace_id(id)` | `field_pred` | 精确匹配 |
| `.require_span_id(id)` | `field_pred` | 精确匹配 |
| `.require_parent_span_id(id)` | `field_pred` | 精确匹配 |
| `.where(pred)` | 自定义 | 传入任意 `predicate_of<context>` |

**过滤匹配规则：**

当 `sub`（订阅方）的 `optional` 字段有值时，才与 `pub`（发布方）进行比对：

- `sub` 无值 → 始终匹配（不限条件）
- `sub` 有值但 `pub` 无值 → 不匹配
- 两者都有值 → 按比较器判断

```cpp
// 订阅方未设 priority → 任何 priority 的事件都匹配
b.on<int_event>().call(/* ... */);

// 订阅方设了 min_priority(5) → 只有 priority >= 5 才匹配
b.on<int_event>().min_priority(5).call(/* ... */);

// 发布方未设 priority → 不匹配（sub 有值，pub 无值）
b.post<int_event>(value<int>(1)).sync();  // 不触发带 min_priority 的订阅
```

### 自定义谓词 (where)

```cpp
// 自定义复杂条件
b.on<int_event>()
    .where([](const context& ctx) {
        return ctx.priority.has_value() && *ctx.priority > 50;
    })
    .call([](const value<int>& v) { /* ... */ });
```

### 谓词组合子

库提供了逻辑运算符重载，支持组合谓词：

```cpp
// AND: 两个条件同时满足
auto pred = field_pred<context, &context::id>{.expected = 10} &&
            field_pred<context, &context::priority>{.expected = 5};

// OR: 任一条件满足
auto pred = field_pred<context, &context::id>{.expected = 1} ||
            field_pred<context, &context::id>{.expected = 2};

// NOT: 条件取反
auto pred = !field_pred<context, &context::id>{.expected = 99};
```

### 谓词类型参考

#### 内置谓词

| 谓词 | 用途 |
|------|------|
| `always_pred` | 始终返回 `true`（默认） |
| `never_pred` | 始终返回 `false` |

#### field_pred

对 `context` 的某个 `optional` 字段做比较：

```cpp
field_pred<context, &context::id>{
    .expected = std::uint64_t(42),                // 订阅时期望的值
    .cmp = std::equal_to<>{}                       // 比较器（可自定义）
};
```

#### bits_pred

对 `context::tags` 位掩码做位运算匹配：

```cpp
// 精确匹配
bits_pred<context, &context::tags, bits_match::exact>{.mask = 0x7};
// 全部位匹配 (pub & mask) == mask
bits_pred<context, &context::tags, bits_match::all>{.mask = 0x3};
// 任一位匹配 (pub & mask) != 0
bits_pred<context, &context::tags, bits_match::any>{.mask = 0x6};
// 零位匹配 (pub & mask) == 0
bits_pred<context, &context::tags, bits_match::none>{.mask = 0x8};
```

#### str_pred

对 `context::trace_id` 字符串做模式匹配：

```cpp
// 精确匹配
str_pred<context, &context::trace_id, str_match::exact>{"trace-abc"};
// 前缀匹配
str_pred<context, &context::trace_id, str_match::prefix>{"prefix-"};
// 后缀匹配
str_pred<context, &context::trace_id, str_match::suffix>{"-end"};
// 包含匹配
str_pred<context, &context::trace_id, str_match::contains>{"mid"};
```

#### 组合谓词

| 类型 | 运算符 | 逻辑 |
|------|--------|------|
| `and_pred<L, R>` | `L && R` | 短路与 |
| `or_pred<L, R>` | `L \|\| R` | 短路或 |
| `xor_pred<L, R>` | 手动构造 | 异或 |
| `not_pred<P>` | `!P` | 取反 |

---

## 回调签名重载

`filter<Tag>::call()` 支持多种回调签名，通过重载决议自动选择正确的包装方式。

### 自由函数 / Lambda

| 签名 | 能获取 | 适用事件 |
|------|--------|---------|
| `(const value_t& v, const context& ctx)` | 值 + 上下文 | 有值事件 |
| `(const value_t& v)` | 仅值 | 有值事件 |
| `(const context& ctx)` | 仅上下文 | 所有事件 |
| `()` | 无 | 所有事件（特别是 void 事件） |

```cpp
// 全部信息
b.on<int_event>().call([](const value<int>& v, const context& ctx) {
    int data = *v;
    auto pri = ctx.priority;
});

// 只需要值
b.on<int_event>().call([](const value<int>& v) {
    int data = *v;
});

// 只需要上下文
b.on<int_event>().call([](const context& ctx) {
    // 不关心具体值，只关心元数据
});

// 纯通知
b.on<void_event>().call([] {
    // 事件发生了，无需额外信息
});
```

> **注意：** 对于有值事件（`value<T>`），回调参数是 `const value<T>&`，不是 `const T&`。需要使用 `*v` 解引用获取内部值。

### 成员函数指针

`call()` 支持直接绑定成员函数指针，自动处理对象生命周期（通过引用或指针捕获）：

```cpp
struct handler {
    void on_value_ctx(const value<int>& v, const context& ctx);
    void on_value(const value<int>& v);
    void on_context(const context& ctx);
    void on_void();
};

handler h;

// 通过引用（对象需在订阅期间存活）
b.on<int_event>().call(h, &handler::on_value_ctx);
b.on<int_event>().call(h, &handler::on_value);
b.on<int_event>().call(h, &handler::on_context);
b.on<void_event>().call(h, &handler::on_void);

// 通过指针
b.on<int_event>().call(&h, &handler::on_value_ctx);

// const 和 non-const 成员函数均可
```

所有成员函数指针重载（共 20 个）覆盖了：
- 按引用 / 按指针
- const / non-const 成员函数
- 有无 context 参数
- 有无 value 参数

---

## Scheduler — 调度器

调度器是 `bus` 的内部组件，管理延迟和定时任务的执行。它使用 `std::jthread` 维护后台线程，通过最小堆管理任务队列。

```cpp
// bus 构造时自动创建调度器
bus b(executor);

// 通过 router 使用调度器
b.post<void_event>().delay_for(100ms).async();       // 100ms 后执行
b.post<void_event>().execute_at(some_time).async();  // 指定时间点执行
```

**调度器生命周期：**
- 首次调度任务时惰性启动后台线程
- bus 析构时自动停止线程并等待任务完成

---

## 递归发布

cbuspp 支持在订阅回调中再次发布事件的模式，适用于工作流编排、状态机驱动等场景。

### 同步递归

#### 同一事件递归（需深度限制避免无限循环）

```cpp
int depth = 0;
b.on<int_event>().call([&](const value<int>& v, const context&) {
    depth++;
    if (depth < 5) {
        b.post<int_event>(value<int>(*v + 1)).sync();
    }
});
b.post<int_event>(value<int>(1)).sync();
// depth == 5
```

#### 跨事件链（A → B → C）

```cpp
b.on<event_a>().call([&](const value<int>& v) {
    b.post<event_b>(value<int>(*v + 1)).sync();
});
b.on<event_b>().call([&](const value<int>& v) {
    b.post<event_c>(value<int>(*v + 1)).sync();
});
b.on<event_c>().call([&](const value<int>& v) {
    // 处理最终值
});
```

#### 钻石模式（扇出再汇聚）

```cpp
// A → B, A → C, B → D, C → D
b.on<event_a>().call([&](const value<int>& v) {
    b.post<event_b>(value<int>(*v)).sync();
    b.post<event_c>(value<int>(*v)).sync();
});
b.on<event_b>().call([&](const value<int>& v) {
    b.post<event_d>(value<int>(*v * 2)).sync();
});
b.on<event_c>().call([&](const value<int>& v) {
    b.post<event_d>(value<int>(*v * 3)).sync();
});
// D 会被调用两次，分别收到 2*v 和 3*v
```

#### 递归 + 过滤器

```cpp
// 过滤器可在递归中提供门控
b.on<int_event>().min_priority(5).call([&](const value<int>& v) {
    // 重新发布低优先级版本 → 不会被自己的过滤器匹配
    b.post<int_event>(value<int>(100)).with_priority(1).sync();
});
```

#### 值累积递归

```cpp
// 用递归实现累加
int sum = 0;
b.on<int_event>().call([&](const value<int>& v) {
    sum += *v;
    if (*v > 0) {
        b.post<int_event>(value<int>(*v - 1)).sync();
    }
});
b.post<int_event>(value<int>(5)).sync();
// sum == 15 (5+4+3+2+1+0)
```

### 异步递归

```cpp
int depth = 0;
b.on<void_event>().call([&](const context&) {
    depth++;
    if (depth < 3) {
        b.post<void_event>().async();
    }
});

b.post<void_event>().async();
// 运行 executor 中所有排队任务
while (recording_executor.pending_count() > 0) {
    recording_executor.run_all();
}
// depth == 3
```

### 混合递归

```cpp
// 异步处理器中触发同步事件（同步发布立即执行，不经过 executor）
b.on<void_event>().call([&](const context&) {
    b.post<int_event>(value<int>(42)).sync();  // 立即同步执行
});
b.post<void_event>().async();
```

---

## 适用场景

| 场景 | 推荐方式 | 理由 |
|------|---------|------|
| **日志系统** | `sync()` + `min_priority` 过滤 | 按级别过滤，同步保证顺序 |
| **插件架构** | 多事件标签 | 每种插件行为独立事件 |
| **状态机** | 递归跨事件链 | A→B→C 状态转移 |
| **工作流引擎** | 递归发布 + COW | 步骤间传递共享数据 |
| **UI 事件解耦** | `async()` + 延迟 | 避免阻塞 UI 线程 |
| **分布式追踪** | `trace_id` + `span_id` | 关联跨服务调用链 |
| **消息审计** | `causation_id` + `id` | 追溯事件因果链 |
| **定时任务** | `execute_at` + `async()` | 调度器管理定时执行 |
| **请求-响应** | `sync()` | 调用方需要处理结果 |
| **模块间通信** | 不同事件标签 | 编译时类型隔离 |

---

## 最佳实践

### 1. 事件标签命名

使用有意义的名称，建议包含模块前缀：

```cpp
struct ui_click_event : event_tag<"ui.click", value<click_data>> {};
struct net_response_event : event_tag<"net.response", value<response_data>> {};
```

### 2. Executor 选择

- **单线程测试/简单场景：** 使用直接执行的 executor
- **生产环境：** 使用线程池 executor，避免回调阻塞发布线程
- **异步测试：** 使用 recording executor 精确控制执行顺序

### 3. 回调中的生命周期

```cpp
// ❌ 危险：被捕获的引用可能在回调执行时已失效
void bad_example(bus<my_executor>& b) {
    int local = 42;
    b.on<int_event>().call([&](const value<int>&) {
        use(local);  // local 可能已被销毁
    });
}

// ✅ 安全：值捕获确保生命周期
void good_example(bus<my_executor>& b) {
    auto data = std::make_shared<my_data>();
    b.on<int_event>().call([data](const value<int>&) {
        data->process();
    });
}
```

### 4. 取消订阅管理

```cpp
class subscriber_manager {
    std::vector<subscription_id_t> ids_;
    bus<my_executor>* bus_;

public:
    ~subscriber_manager() {
        for (auto id : ids_) bus_->unsubscribe(id);
    }

    template <event_tag_t Tag, typename F>
    void subscribe(F&& f) {
        ids_.push_back(bus_->template on<Tag>().call(std::forward<F>(f)));
    }
};
```

### 5. 过滤器设计

- 优先使用内置过滤器方法（类型安全、意图明确）
- 复杂条件用 `where()` 配合组合子
- 避免在 `where()` 中进行重计算——过滤器在每次发布时都会执行

### 6. 递归发布防护

同步递归必须设置退出条件，否则会栈溢出：

```cpp
// ✅ 安全递归
int depth = 0;
b.on<int_event>().call([&](const value<int>& v, const context&) {
    if (++depth >= MAX_DEPTH) return;  // 防护
    b.post<int_event>(value<int>(*v + 1)).sync();
});
```

### 7. 值的构造

`value<T>` 和 `shared_value<T>` 的构造函数是 `explicit` 的，必须显式构造：

```cpp
// ✅ 正确
b.post<int_event>(value<int>(42)).sync();

// ❌ 错误：隐式转换不可用
b.post<int_event>(42).sync();  // 编译错误
```

---

## 注意事项与限制

### 迭代中修改订阅者

**不要**在同步发布的回调中取消订阅**同一事件**的订阅者。`do_sync` 直接遍历 `std::vector`，修改会导致迭代器失效（未定义行为）。

```cpp
// ❌ 危险：在同一事件的回调中取消自己的订阅
b.on<void_event>().call([&] {
    b.unsubscribe(my_id);  // UB: 修改正在迭代的 vector
});
```

**但是**，取消**其他事件**的订阅者是安全的：

```cpp
// ✅ 安全：操作不同事件的订阅者列表
b.on<int_event>().call([&](const value<int>&) {
    b.unsubscribe(other_event_id);  // 不同 vector，安全
});
```

### 非线程安全

`bus` 本身不是线程安全的。并发访问（包括并发发布、订阅、取消订阅）需要外部同步。`do_sync` 和 `do_async` 对订阅者列表的访问未加锁。

### shared_value 的 COW 时机

只有通过**非 const** 路径访问 `shared_value` 时才会触发 COW：

```cpp
shared_value<int> v1(10);
shared_value<int> v2 = v1;

*v1 = 20;          // COW: v1 先复制再修改
v1.write() = 30;   // COW: 同上
v1.modify(...);    // COW: 同上

v1.read();         // 无 COW
const auto& r = v1;
*r;                // 无 COW（const 访问）
```

### 延迟/定时任务的精度

调度器使用 `std::chrono::steady_clock`，精度取决于操作系统。不适合硬实时场景。

---

## 完整 API 参考

### cbuspp 命名空间

#### 类型别名

```cpp
using steady_clock_t = std::chrono::steady_clock;
using duration_t    = steady_clock_t::duration;
using time_point_t  = steady_clock_t::time_point;
using subscription_id_t = std::uint64_t;
```

#### 枚举

```cpp
enum class str_match : std::uint8_t { exact, prefix, suffix, contains };
enum class bits_match : std::uint8_t { exact, all, any, none };
```

#### 模板元编程

```cpp
template <typename T> struct is_value;           // 检测是否为 value 特化
template <typename T> inline constexpr bool is_value_v;
template <typename T> concept value_t;

template <typename T> struct is_shared_value;    // 检测是否为 shared_value 特化
template <typename T> inline constexpr bool is_shared_value_v;
template <typename T> concept shared_value_t;

template <typename T> struct is_executor;        // 检测是否为 executor
template <typename E> concept executor_t;

template <typename T> concept event_tag_t;
template <typename P, typename Context> concept predicate_of;
```

### value\<T\>

```cpp
// 构造
value();                                   // 默认（T 需默认构造）
explicit value(const T& v);                 // 拷贝
explicit value(T&& v);                      // 移动
explicit value(std::in_place_t, Args&&...); // in_place

// 解引用
explicit operator bool() const;            // 是否有效
decltype(auto) operator*();                // 解引用
auto operator->();                         // 成员访问

// Monadic
auto transform(F&& f);                     // 映射
decltype(auto) and_then(F&& f);            // 平坦映射
decltype(auto) inspect(F&& f);             // 副作用检查

// 其他
void swap(value& other);
```

### shared_value\<T\>

```cpp
// 继承 value<T> 的 void 特化，或独立实现

// 访问
const T& read() const;                    // 只读，无 COW
T& write();                               // 可写，触发 COW
decltype(auto) operator*();               // const→只读，非const→COW+写
auto operator->();                        // 同上

// 查询
bool unique() const;                      // 是否独占
std::size_t use_count() const;            // 共享计数

// Monadic
auto transform(F&& f);                    // 非 const 触发 COW
decltype(auto) inspect(F&& f);            // 非 const 触发 COW
decltype(auto) modify(F&& f);             // 就地修改，触发 COW
decltype(auto) and_then(F&& f);           // 平坦映射
```

### bus\<Executor\>

```cpp
// 构造
explicit bus(Executor& executor);

// 发布
template <event_tag_t Tag>
router<Tag> post();                       // 无值事件

template <event_tag_t Tag>                // 有值事件 (const&)
router<Tag> post(const Tag::value_t& value);

template <event_tag_t Tag>                // 有值事件 (&&)
router<Tag> post(Tag::value_t&& value);

// 订阅
template <event_tag_t Tag>
filter<Tag> on();

// 绑定 / 解绑
template <event_tag_t Tag>
subscription_id_t bind(subscriber sub);

subscription_id_t unsubscribe(subscription_id_t id);

// 查询
template <event_tag_t Tag>
std::size_t subscriber_count() const;

template <event_tag_t Tag>
bool has_subscribers() const;
```

### router\<Tag\>

```cpp
router& with_priority(std::int32_t p);
router& delay_for(duration_t d);
router& execute_at(time_point_t t);
router& with_id(std::uint64_t id);
router& caused_by(std::uint64_t causation_id);
router& with_tags(std::uint64_t tags);
router& with_trace_id(std::string trace_id);
router& with_span_id(std::uint64_t span_id);
router& with_parent_span_id(std::uint64_t parent_span_id);
router& with_context(context ctx);

void sync();   // 同步发布（需 value_t 或 monostate）
void async();  // 异步发布（需 shared_value_t 或 monostate）
```

### filter\<Tag\>

```cpp
// 内置过滤器
filter& min_priority(std::uint32_t p);
filter& require_id(std::uint64_t id);
filter& require_causation_id(std::uint64_t causation_id);
filter& and_tags(std::uint64_t tags);
filter& or_tags(std::uint64_t tags);
filter& not_tags(std::uint64_t tags);
filter& require_trace_id(std::string trace_id);
filter& require_span_id(std::uint64_t span_id);
filter& require_parent_span_id(std::uint64_t parent_span_id);

// 自定义谓词
template <predicate_of<context> P>
filter& where(P&& pred);

// 订阅回调（自由函数/lambda）
template <typename F> subscription_id_t call(F&& f);
// 4 个重载: (value,ctx), (value), (ctx), ()

// 订阅回调（成员函数指针）
template <typename T> subscription_id_t call(T& obj, void (T::*method)());
template <typename T> subscription_id_t call(T* obj, void (T::*method)());
// ... 共 20 个重载覆盖各种组合
```

### 谓词

```cpp
// 基础
struct always_pred;    // operator() → true
struct never_pred;     // operator() → false

// 字段匹配
template <typename Context, auto MemberPtr, typename Compare = std::equal_to<>>
struct field_pred;

// 字符串匹配
template <typename Context, auto MemberPtr, str_match Mode = str_match::exact>
struct str_pred;

// 位掩码匹配
template <typename Context, auto MemberPtr, bits_match Mode = bits_match::exact>
struct bits_pred;

// 组合
template <typename L, typename R> struct and_pred;
template <typename L, typename R> struct or_pred;
template <typename L, typename R> struct xor_pred;
template <typename P> struct not_pred;

// 运算符
auto operator&&(L&& left, R&& right);    // → and_pred
auto operator||(L&& left, R&& right);    // → or_pred
auto operator!(P&& pred);                // → not_pred
```

### scheduler\<Executor\>

```cpp
// 内部组件，通过 bus.post().delay_for() / .execute_at() 间接使用
explicit scheduler(Executor& executor);
void schedule_after(duration_t delay, scheduled_func_t task);
void schedule_at(time_point_t time, scheduled_func_t task);
```

### fixed_string\<N\>

```cpp
template <std::size_t N>
struct fixed_string {
    constexpr fixed_string(const char (&str)[N + 1]);
    constexpr operator std::string_view() const;
    static constexpr std::size_t size();
    auto operator<=>(const fixed_string&) const = default;
};
```

---

## 测试

测试文件位于 [test.cpp](test.cpp)，包含 132 个测试用例，覆盖所有公共 API。使用 CMake 构建并运行：

```bash
cd build && cmake .. && make cbuspp_test && ./cbuspp_test
```

---

## 许可证

本项目为内部库，请遵循项目根目录的许可证文件。