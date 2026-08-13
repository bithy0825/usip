#include <filesystem>
#include <string>

namespace usip::common {

[[nodiscard]] inline auto path_from_utf8(std::string_view utf8) -> std::filesystem::path
{
    const auto* data = reinterpret_cast<const char8_t*>(utf8.data());
    return std::filesystem::path { std::u8string_view { data, utf8.size() } };
}

[[nodiscard]] inline auto path_to_utf8(const std::filesystem::path& path) -> std::string
{
    const auto u8 = path.u8string();
    return std::string { u8.begin(), u8.end() };
}

}
