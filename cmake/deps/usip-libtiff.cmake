# ==============================================================================
# usip-libtiff.cmake — libtiff(vcpkg,动态库)
#
# 动态理由:C ABI 稳定;依赖链长(zlib/jpeg/lzma/zstd),动态链接时传递 DLL
# 由 vcpkg applocal 统一处理。
# 注意:vcpkg 的 tiff 端口不发布 cmake config 文件,官方用法是
# vcpkg-cmake-wrapper + CMake 内置 FindTIFF 模块 —— 必须保持模块模式,
# 切勿加 CONFIG 关键字。
#
# 提供:3rdparty::tiff
# ==============================================================================
include_guard(GLOBAL)

find_package(TIFF REQUIRED)

if(NOT TARGET 3rdparty_tiff)
    add_library(3rdparty_tiff INTERFACE)
    target_link_libraries(3rdparty_tiff INTERFACE TIFF::TIFF)
    add_library(3rdparty::tiff ALIAS 3rdparty_tiff)
endif()

usip_log_dep("libtiff" "动态 · vcpkg" "")
