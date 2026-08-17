# ==============================================================================
# usip-pch.cmake — 全项目唯一聚合 PCH
#
# 决策(2026-08-11,替代原"按组合分档"方案):
#   全项目只编译一份 PCH,内容为全部片段的聚合(stl + thirdparty + qt),
#   owner 为 app 层目标(usip),其余目标一律 REUSE_FROM 复用。
#
#   理由:多组合各编一份既浪费(重复编译相同片段、每份数百 MB)又难管理;
#   MSVC 一个编译单元只能挂一份 PCH,无法链式叠加,聚合是唯一形态。
#
#   已知代价(显式接受):任何模块在预处理器层面都能"看见" Qt 头。
#   依赖纪律不由 PCH 保证,而由链接的 PRIVATE/PUBLIC 划分保证 ——
#   低层模块不得包含 Qt 头,这条写进 cmake/README.md 与 code review 约定。
#
#   owner 为什么是最底层的 usip_common 而不是 app:REUSE_FROM 会产生一条
#   指向 owner 的强目标依赖,owner 必须位于依赖图最底部;若 owner 是 app,
#   则 lib --(PCH)--> app --(link)--> lib 构成 CMake 禁止的依赖环。
#
# 用法(两阶段,与子目录添加顺序无关):
#   各模块 CMakeLists:        usip_apply_pch(<target>)      # 仅登记
#   src/CMakeLists.txt 末尾:  usip_pch_finalize()           # 统一落地
#
# 选项:
#   USIP_ENABLE_PCH  ON                              全局开关
#   USIP_PCH_DIR     ${CMAKE_SOURCE_DIR}/src/pch     片段目录(文件名为 pch_<层>.hpp)
#
# 片段规则:
#   * 只放稳定头;项目自身的头一律不放
#   * clipper2 等第三方头按需收录
# ==============================================================================
include_guard(GLOBAL)

option(USIP_ENABLE_PCH "启用预编译头" ON)
set(USIP_PCH_DIR "${CMAKE_SOURCE_DIR}/src/pch" CACHE PATH "PCH 片段目录(pch_<层>.hpp)")

# 登记一个 PCH 消费目标(不做任何实际动作,落地统一在 finalize)
function(usip_apply_pch TARGET)
    if(NOT USIP_ENABLE_PCH)
        return()
    endif()
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[usip] usip_apply_pch:目标 ${TARGET} 不存在")
    endif()

    get_property(_consumers GLOBAL PROPERTY USIP_PCH_CONSUMERS)
    if("${TARGET}" IN_LIST _consumers)
        return()   # 幂等:重复登记直接忽略
    endif()
    set_property(GLOBAL APPEND PROPERTY USIP_PCH_CONSUMERS ${TARGET})
endfunction()

# 统一落地:在全部子目录添加完毕后调用(src/CMakeLists.txt 末尾)
function(usip_pch_finalize)
    if(NOT USIP_ENABLE_PCH)
        return()
    endif()

    get_property(_targets GLOBAL PROPERTY USIP_PCH_CONSUMERS)
    if(NOT _targets)
        return()
    endif()

    # 聚合片段(顺序即拼接顺序)
    set(_headers)
    foreach(_layer IN ITEMS stl thirdparty qt)
        set(_header "${USIP_PCH_DIR}/pch_${_layer}.hpp")
        if(NOT EXISTS "${_header}")
            message(FATAL_ERROR "[usip] PCH 片段不存在:${_header}")
        endif()
        list(APPEND _headers "${_header}")
    endforeach()

    # owner 必须为依赖图最底部目标(usip_common):REUSE_FROM 会产生指向 owner 的
    # 强依赖,owner 若在顶部(app)则与链接边构成环
    if("usip_common" IN_LIST _targets)
        set(_owner usip_common)
    else()
        list(GET _targets 0 _owner)
    endif()

    # 所有消费目标都会被 /FI 强制包含聚合头 → 必须能解析 Qt 的 include 路径;
    # 统一在此附加(PRIVATE 接口链接对静态库仅是使用需求,不发生实际链接)
    #
    # toml++/highway 同理,且不只是 include 路径:PCH 片段直接包含它们的头,
    # 其接口宏(TOML_HEADER_ONLY/TOML_SHARED_LIB、HWY_SHARED_DEFINE/TOOLCHAIN_MISS_*)
    # 会进入 PCH 宏状态;任一消费目标缺这些 /D,MSVC 即报 C4005/C4651
    # (PCH 与消费方宏状态不一致在 dllimport 声明层面还有 ODR 风险)
    # → 全消费目标统一链接,与 PCH 宏状态对齐
    foreach(_t IN LISTS _targets)
        foreach(_dep IN ITEMS qt6 tomlpp highway)
            if(TARGET 3rdparty_${_dep})
                target_link_libraries(${_t} PRIVATE 3rdparty::${_dep})
            endif()
        endforeach()
    endforeach()

    target_precompile_headers(${_owner} PRIVATE ${_headers})
    message(STATUS "[usip] PCH owner:${_owner} ← [stl thirdparty qt](全项目唯一聚合 PCH)")

    foreach(_t IN LISTS _targets)
        if(NOT _t STREQUAL _owner)
            target_precompile_headers(${_t} REUSE_FROM ${_owner})
            message(STATUS "[usip] PCH 复用:${_t} ← ${_owner}")
        endif()
    endforeach()
endfunction()
