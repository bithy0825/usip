# ==============================================================================
# usip-clipper2.cmake — Clipper2(vcpkg,多边形布尔运算与偏移)
#
# Angus Johnson 的 Clipper2 库,用于多边形并/差/交/异或布尔运算与轮廓偏移。
# vcpkg 端口提供 CONFIG 文件,导出 Clipper2::Clipper2 目标。
#
# 提供:3rdparty::clipper2
# ==============================================================================
include_guard(GLOBAL)

find_package(Clipper2 CONFIG REQUIRED)

if(NOT TARGET 3rdparty_clipper2)
    add_library(3rdparty_clipper2 INTERFACE)
    target_link_libraries(3rdparty_clipper2 INTERFACE Clipper2::Clipper2)
    add_library(3rdparty::clipper2 ALIAS 3rdparty_clipper2)
endif()

usip_log_dep("Clipper2" "vcpkg" "v${Clipper2_VERSION}")
