#include "executor.hpp"

namespace usip::common {

executor::executor(std::size_t thread_count)
    : arena_ { static_cast<int>(thread_count == 0
                       ? tbb::info::default_concurrency()
                       : thread_count)
        + 1 }
    // +1:task_arena 为创建者线程预留 master 槽位,
    // 纯 enqueue 场景可用 worker = max_concurrency - 1,补偿以兑现请求的线程数
    , workers_ { thread_count == 0
            ? static_cast<std::size_t>(tbb::info::default_concurrency())
            : thread_count }
{
}

executor::~executor()
{
    tasks_.wait(); // 排空已入队任务后再销毁
}

auto executor::thread_count() const noexcept -> std::size_t
{
    return workers_;
}

void executor::wait_idle()
{
    tasks_.wait();
}

} // namespace usip::common
