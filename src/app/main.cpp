#include "app.hpp"

#include "platform/system.hpp"

int main(int argc, char* argv[])
{
    // 独显优先:必须在任何图形上下文创建之前(机制细节封装在 platform 内,
    // 此处与平台无关 —— Windows 走驱动导出符号,Linux 走 PRIME 环境变量)
    usip::platform::system::prefer_discrete_gpu();

    usip::app::application app;

    if (auto r = app.init(argc, argv); !r) {
        // 此时日志可能尚未就绪,启动错误走原生消息框
        usip::platform::system::message_box(usip::platform::message_kind::error,
            "usip - Startup Error", r.error().to_string());
        return -1;
    }

    return app.exec();
}
