# ==============================================================================
# usip-config.cmake — 全局构建配置(所有模块的基石,必须最先 include)
#
# 提供:
#   usip::options    INTERFACE 目标 —— C++23、警告、公共宏(替代全局 add_compile_options)
#   usip_log_dep()   统一依赖日志格式
#
# 选项:
#   USIP_WARNINGS_AS_ERRORS  OFF  警告视为错误
#   USIP_ENABLE_IPO          OFF  Release/RelWithDebInfo 启用 LTO/IPO
#
# 要求:CMake >= 3.26(copy_directory_if_different / preset schema v6 / OUTPUT 生成器表达式)
# ==============================================================================
include_guard(GLOBAL)

if(CMAKE_VERSION VERSION_LESS "3.26")
    message(FATAL_ERROR "[usip] 需要 CMake >= 3.26,当前为 ${CMAKE_VERSION}")
endif()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "[usip] 不支持源码内构建,请使用 cmake -B build")
endif()

# ---- 构建类型:单配置生成器给默认值 ------------------------------------------
get_property(_usip_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _usip_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---- 输出目录:统一携带 $<CONFIG> -------------------------------------------
# 单/多配置生成器布局一致(bin/Debug、bin/Release …),杜绝 Debug/Release DLL 混放
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")
set(CMAKE_PDB_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")

# ---- 全局默认值(仅初始化此后创建的 target 属性,不做强制) -------------------
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)                 # 暂不使用 C++20 modules,免去扫描开销
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_DEBUG_POSTFIX "d")                        # Debug 库产物加 d 后缀
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# MSVC 动态 CRT,与 vcpkg x64-windows 三元组一致(CMP0091 在 3.26 下恒为 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

option(USIP_WARNINGS_AS_ERRORS "将编译警告视为错误" OFF)
set(CMAKE_COMPILE_WARNING_AS_ERROR ${USIP_WARNINGS_AS_ERRORS})

option(USIP_ENABLE_IPO "Release/RelWithDebInfo 启用 LTO/IPO" OFF)
if(USIP_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _usip_ipo_ok OUTPUT _usip_ipo_msg LANGUAGES CXX)
    if(_usip_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
        message(STATUS "[usip] IPO/LTO 已启用(Release/RelWithDebInfo)")
    else()
        message(WARNING "[usip] 当前工具链不支持 IPO/LTO:${_usip_ipo_msg}")
    endif()
endif()

# ---- 统一编译选项:INTERFACE 目标,链接即继承 ---------------------------------
add_library(usip_build_options INTERFACE)
add_library(usip::options ALIAS usip_build_options)

target_compile_features(usip_build_options INTERFACE cxx_std_23)

target_compile_options(usip_build_options INTERFACE
    "$<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive-;/utf-8;/MP;/EHsc;/Zc:__cplusplus;/external:anglebrackets;/external:W0;/arch:AVX2>"
    "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic;-mavx2;-mfma;-mf16c>"
)

target_compile_definitions(usip_build_options INTERFACE
    "$<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS;NOMINMAX;WIN32_LEAN_AND_MEAN>"
)

# ---- 统一依赖日志 -------------------------------------------------------------
function(usip_log_dep NAME KIND DETAIL)
    if(DETAIL)
        message(STATUS "[usip] ${NAME} [${KIND}] ${DETAIL}")
    else()
        message(STATUS "[usip] ${NAME} [${KIND}]")
    endif()
endfunction()

if(_usip_multi_config)
    message(STATUS "[usip] 多配置生成器:${CMAKE_GENERATOR}")
else()
    message(STATUS "[usip] 构建类型:${CMAKE_BUILD_TYPE}")
endif()
message(STATUS "[usip] 产物目录:${CMAKE_BINARY_DIR}/bin/<Config>")
