# ==============================================================================
# usip-spdlog.cmake — spdlog(vcpkg,动态库)
#
# 动态理由:spdlog 默认 logger 是全局 registry 单例;若 exe 与多个内部 DLL 各自
# 静态链接,会分裂成多份 registry,日志配置互不生效。
# 使用编译版(非 header-only),避免 fmt 模板在每个编译单元重复实例化。
#
# 提供:3rdparty::spdlog
# ==============================================================================
include_guard(GLOBAL)

find_package(spdlog CONFIG REQUIRED)

if(NOT TARGET 3rdparty_spdlog)
    add_library(3rdparty_spdlog INTERFACE)
    target_link_libraries(3rdparty_spdlog INTERFACE spdlog::spdlog)
    target_compile_definitions(3rdparty_spdlog INTERFACE
        # 日志激活级别按配置区分:Debug 全开,其余保留 INFO 及以上
        "SPDLOG_ACTIVE_LEVEL=$<IF:$<CONFIG:Debug>,SPDLOG_LEVEL_DEBUG,SPDLOG_LEVEL_INFO>"
        "$<$<CXX_COMPILER_ID:MSVC>:_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING;_SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS>"
    )
    add_library(3rdparty::spdlog ALIAS 3rdparty_spdlog)
endif()

usip_log_dep("spdlog" "动态 · vcpkg" "v${spdlog_VERSION}")
