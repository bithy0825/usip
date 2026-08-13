# ==============================================================================
# usip-version.cmake — 版本信息单一事实来源 + 版本头生成
#
# 版本取自 project(... VERSION x.y.z)(大厂规范:project() 是唯一来源);
# 未声明 VERSION 时回落 0.1.0。
#
# 提供:
#   USIP_VERSION / USIP_VERSION_MAJOR / USIP_VERSION_MINOR / USIP_VERSION_PATCH
#   USIP_GIT_HASH                   配置期 Git 短哈希快照(非 git 仓库为 "unknown")
#   usip_add_version_header(<target>)  生成并附加 <usip/version.hpp>
# ==============================================================================
include_guard(GLOBAL)

if(PROJECT_VERSION)
    set(USIP_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
    set(USIP_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
    set(USIP_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
else()
    set(USIP_VERSION_MAJOR 0)
    set(USIP_VERSION_MINOR 1)
    set(USIP_VERSION_PATCH 0)
endif()
set(USIP_VERSION "${USIP_VERSION_MAJOR}.${USIP_VERSION_MINOR}.${USIP_VERSION_PATCH}")

find_package(Git QUIET)
set(USIP_GIT_HASH "unknown")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _usip_git_hash
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(_usip_git_hash)
        set(USIP_GIT_HASH "${_usip_git_hash}")
    endif()
endif()

function(usip_add_version_header TARGET)
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[usip] usip_add_version_header:目标 ${TARGET} 不存在")
    endif()
    if(NOT TARGET usip_version_header)
        set(_usip_gen_dir "${CMAKE_BINARY_DIR}/generated")
        configure_file(
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/usip-version.hpp.in"
            "${_usip_gen_dir}/usip/version.hpp"
            @ONLY
        )
        add_library(usip_version_header INTERFACE)
        target_include_directories(usip_version_header SYSTEM INTERFACE "${_usip_gen_dir}")
    endif()
    target_link_libraries(${TARGET} PRIVATE usip_version_header)
endfunction()

message(STATUS "[usip] 版本:${USIP_VERSION} (${USIP_GIT_HASH})")
