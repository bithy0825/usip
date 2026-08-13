# ==============================================================================
# usip-tbb.cmake — oneTBB(vcpkg,动态库)
#
# 动态理由:全局 task arena/调度器状态必须全进程唯一;oneTBB 2021 起官方
# 已移除静态链接支持,动态实为唯一受支持的方式。
#
# 提供:3rdparty::tbb
# ==============================================================================
include_guard(GLOBAL)

find_package(TBB CONFIG REQUIRED)

if(NOT TARGET 3rdparty_tbb)
    add_library(3rdparty_tbb INTERFACE)
    target_link_libraries(3rdparty_tbb INTERFACE TBB::tbb)
    add_library(3rdparty::tbb ALIAS 3rdparty_tbb)
endif()

usip_log_dep("oneTBB" "动态 · vcpkg" "v${TBB_VERSION}")
