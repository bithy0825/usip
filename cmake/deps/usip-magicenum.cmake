# ==============================================================================
# usip-magicenum.cmake — magic_enum(vcpkg,纯头文件)
#
# 库本身即 header-only,INTERFACE 是唯一合理方式。
#
# 提供:3rdparty::magic_enum
# ==============================================================================
include_guard(GLOBAL)

find_package(magic_enum CONFIG REQUIRED)

if(NOT TARGET 3rdparty_magic_enum)
    add_library(3rdparty_magic_enum INTERFACE)
    target_link_libraries(3rdparty_magic_enum INTERFACE magic_enum::magic_enum)
    add_library(3rdparty::magic_enum ALIAS 3rdparty_magic_enum)
endif()

usip_log_dep("magic_enum" "纯头文件 · vcpkg" "v${magic_enum_VERSION}")
