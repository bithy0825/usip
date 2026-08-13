# ==============================================================================
# usip-tomlpp.cmake — tomlplusplus(vcpkg,纯头文件)
#
# 库本身即 header-only,INTERFACE 是唯一合理方式。
#
# 提供:3rdparty::tomlpp
# ==============================================================================
include_guard(GLOBAL)

find_package(tomlplusplus CONFIG REQUIRED)

if(NOT TARGET 3rdparty_tomlpp)
    add_library(3rdparty_tomlpp INTERFACE)
    target_link_libraries(3rdparty_tomlpp INTERFACE tomlplusplus::tomlplusplus)
    add_library(3rdparty::tomlpp ALIAS 3rdparty_tomlpp)
endif()

usip_log_dep("tomlplusplus" "纯头文件 · vcpkg" "v${tomlplusplus_VERSION}")
