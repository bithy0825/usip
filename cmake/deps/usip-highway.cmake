# ==============================================================================
# usip-highway.cmake — Google Highway(vcpkg,静态库)
#
# 静态理由:SIMD 封装库,静态链接便于内联优化与按目标架构运行时分派,
# 为官方主推的集成方式。
#
# 提供:3rdparty::highway
# ==============================================================================
include_guard(GLOBAL)

find_package(hwy CONFIG REQUIRED)

if(NOT TARGET 3rdparty_highway)
    add_library(3rdparty_highway INTERFACE)
    target_link_libraries(3rdparty_highway INTERFACE hwy::hwy)
    add_library(3rdparty::highway ALIAS 3rdparty_highway)
endif()

usip_log_dep("highway" "静态 · vcpkg" "v${hwy_VERSION}")
