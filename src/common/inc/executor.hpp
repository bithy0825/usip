#pragma once

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

#include <tbb/info.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>

namespace usip::common {

class executor {
public:
    // thread_count = 请求的 worker 线程数;0 = 全部逻辑核心
    explicit executor(std::size_t thread_count = 0);
    ~executor();

    executor(const executor&) = delete;
    auto operator=(const executor&) -> executor& = delete;

    // ── 投递(cbuspp executor_t 契约)──────────────────────────────────
    template <typename F>
    void post(F&& f)
    {
        auto task = std::make_shared<std::decay_t<F>>(std::forward<F>(f));
        // 挂到 task_group:wait_idle/析构得以等待(arena.enqueue 本身无排空语义)
        arena_.enqueue([task] { std::invoke(*task); }, tasks_);
    }

    // ── 异步:返回 future ─────────────────────────────────────────────
    template <typename F>
    [[nodiscard]] auto async(F&& f) -> std::future<std::invoke_result_t<F>>
    {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        auto fut = task->get_future();
        post([task] { (*task)(); });
        return fut;
    }

    [[nodiscard]] auto thread_count() const noexcept -> std::size_t;

    // 等待全部已投递任务完成
    void wait_idle();

private:
    // task_arena 为创建者线程预留一个槽位(master slot),纯 enqueue 场景下
    // 可用 worker = max_concurrency - 1 → 构造时 +1 补偿(见 executor.cpp)
    tbb::task_arena arena_ { };
    tbb::task_group tasks_ { };
    std::size_t workers_ { };
};

} // namespace usip::common
