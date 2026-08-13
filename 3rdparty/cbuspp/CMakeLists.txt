# ==============================================================================
# cbuspp — 自研事件/数据总线(纯头文件,C++23)
#
# 提供:3rdparty::cbuspp
# 包含方式:#include <cbuspp/cbuspp.hpp>
# ==============================================================================
add_library(3rdparty_cbuspp INTERFACE)
add_library(3rdparty::cbuspp ALIAS 3rdparty_cbuspp)

target_include_directories(3rdparty_cbuspp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(3rdparty_cbuspp INTERFACE cxx_std_23)

message(STATUS "[usip] cbuspp [纯头文件 · 自研] ${CMAKE_CURRENT_SOURCE_DIR}")
