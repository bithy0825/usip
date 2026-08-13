#include "platform/system.hpp"

// system 的平台无关部分:环境变量、临时目录(两平台同实现)

#include <cstdlib>

namespace usip::platform {

auto system::env(std::string_view name) -> std::optional<std::string>
{
    const std::string nul_terminated { name };   // getenv 需要 NUL 结尾
    if (const char* v = std::getenv(nul_terminated.c_str()))
        return std::string { v };
    return std::nullopt;
}

auto system::temp_dir() -> const std::filesystem::path&
{
    static const auto dir = std::filesystem::temp_directory_path();
    return dir;
}

} // namespace usip::platform
