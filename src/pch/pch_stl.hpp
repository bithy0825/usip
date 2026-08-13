// =============================================================================
// pch_stl.hpp — PCH 第 1 层:C++ 标准库(所有 target 的基座)
//
// 规则:
//   * 只放稳定头;项目自身的头(含 3rdparty/ 下的自研库)一律不放
//   * 增删头文件会触发全量重编,保持克制
// =============================================================================
#pragma once

// ---- 容器 -------------------------------------------------------------------
#include <array>
#include <bit>
#include <deque>
#include <flat_map>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---- 算法与工具 ---------------------------------------------------------------
#include <algorithm>
#include <concepts>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

// ---- 数值 / 时间 / 范围 ---------------------------------------------------------
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>

// ---- 字符串 / IO ---------------------------------------------------------------
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

// ---- 并发 ---------------------------------------------------------------------
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

// ---- 异常与断言 -----------------------------------------------------------------
#include <cassert>
#include <expected>
#include <source_location>
#include <stdexcept>
