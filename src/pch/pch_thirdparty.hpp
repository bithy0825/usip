// =============================================================================
// pch_thirdparty.hpp — PCH 片段:全项目通用且稳定的第三方头
//
// 全项目只有一份聚合 PCH(owner 为 app 目标,见 cmake/usip-pch.cmake),
// 本片段是其 thirdparty 部分。
//
// 注:highway 是普通头文件,可以进 PCH;需要多 ISA 动态分派的 kernel.cpp
//     (foreach_target 重入)在 CMake 中单独 SKIP_PRECOMPILE_HEADERS。
// =============================================================================
#pragma once

#include <spdlog/spdlog.h>

#include <toml++/toml.hpp>

#include <oneapi/tbb.h>

#include <cbuspp/cbuspp.hpp>

#include <hwy/highway.h>

#include <clipper2/clipper.h>
