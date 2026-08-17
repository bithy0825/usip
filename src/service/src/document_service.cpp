#include "document_service.hpp"

#include <atomic>
#include <utility>

#include "event.hpp"
#include "logger.hpp"
#include "tiff.hpp"
#include "tiff_loader.hpp"

namespace usip::service {

// 会话:跨线程可变的加载状态(代际 + 当前文档),生命周期与 document_service 一致。
// 后台任务只持 weak_ptr:lock 失败 = 服务已停止,任务直接放弃。
struct document_service::session {
    explicit session(cbuspp::bus<common::executor>& b)
        : bus { b }
    {
    }

    cbuspp::bus<common::executor>& bus;

    // 加载代际:每次收到 file_selected 自增;加载完成后与之不符的结果被丢弃,
    // 防止旧文件慢加载覆盖新选择。io 无取消机制,过期只能丢弃,不能中断。
    std::atomic<std::uint64_t> generation { 0 };

    std::shared_ptr<common::loaded_tiff> current { nullptr };
};

document_service::document_service(common::executor& executor,
    cbuspp::bus<common::executor>& bus)
    : session_ { std::make_shared<session>(bus) }
    , executor_ { executor }
{
    setup_subscriptions();
}

document_service::~document_service() = default;

auto document_service::current() const noexcept -> std::shared_ptr<common::loaded_tiff>
{
    return session_->current;
}

void document_service::setup_subscriptions()
{
    session_->bus.on<core::event::file_selected>().call(*this, &document_service::on_file_selected);
}

void document_service::on_file_selected(const cbuspp::value<std::filesystem::path>& value)
{
    const auto path = *value; // 拷贝:事件值存活于同步派发栈,任务异步执行时已析构
    const auto generation = session_->generation.fetch_add(1, std::memory_order_relaxed) + 1;

    // file_selected 是同步派发(多来自 UI 线程),解码必须转到 executor
    executor_.post([weak = std::weak_ptr<session> { session_ }, path, generation] {
        const auto s = weak.lock();
        if (!s)
            return; // 服务已停止

        auto loaded = io::load_tiff(path);
        if (s->generation.load(std::memory_order_relaxed) != generation)
            return; // 已有更新的选择:丢弃过期结果

        if (!loaded) {
            auto err = loaded.error();
            common::log_error("failed to load '{}': {}", path.string(), err);
            s->bus.post<core::event::error_occurred>(cbuspp::value<common::error&> { err })
                .with_trace_id(core::event::trace_id::document_service)
                .sync(); // 注:在 executor 工作线程派发,UI 订阅方需自行切线程
            return;
        }

        s->current = std::make_shared<common::loaded_tiff>(std::move(loaded).value());
        common::log_info("document loaded: {} pages, {} bytes ('{}')",
            s->current->info.pages.size(), s->current->pixels.size(), path.string());

        s->bus.post<core::event::document_loaded>(cbuspp::value<std::shared_ptr<common::loaded_tiff>> { s->current })
            .with_trace_id(core::event::trace_id::document_service)
            .sync();
    });
}

}
