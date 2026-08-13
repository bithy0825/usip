# ==============================================================================
# usip-json.cmake — nlohmann_json(vcpkg,纯头文件)
#
# 库本身即 header-only,INTERFACE 是唯一合理方式。
#
# 提供:3rdparty::json
# ==============================================================================
include_guard(GLOBAL)

find_package(nlohmann_json CONFIG REQUIRED)

if(NOT TARGET 3rdparty_json)
    add_library(3rdparty_json INTERFACE)
    target_link_libraries(3rdparty_json INTERFACE nlohmann_json::nlohmann_json)
    add_library(3rdparty::json ALIAS 3rdparty_json)
endif()

usip_log_dep("nlohmann_json" "纯头文件 · vcpkg" "v${nlohmann_json_VERSION}")
