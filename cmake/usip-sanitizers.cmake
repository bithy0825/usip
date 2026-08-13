# ==============================================================================
# usip-sanitizers.cmake — 消毒剂(ASan / UBSan)
#
# 选项:
#   USIP_ENABLE_ASAN   OFF  AddressSanitizer(MSVC / GCC / Clang)
#   USIP_ENABLE_UBSAN  OFF  UndefinedBehaviorSanitizer(仅 GCC / Clang)
#
# 函数:
#   usip_enable_sanitizers(<target>)  按上述选项为目标追加消毒编译/链接选项
#
# 注意:
#   * MSVC 的 ASan 需要 VS2019 16.9+,与 /RTC、增量链接冲突(已自动处理)
#   * ASan 应对全部自有代码生效 —— 建议在每个自有 target 上调用,
#     第三方库(vcpkg/Qt)保持不消毒
#   * 建议搭配 Debug 或 RelWithDebInfo 使用
# ==============================================================================
include_guard(GLOBAL)

option(USIP_ENABLE_ASAN "启用 AddressSanitizer" OFF)
option(USIP_ENABLE_UBSAN "启用 UndefinedBehaviorSanitizer(仅 GCC/Clang)" OFF)

function(usip_enable_sanitizers TARGET)
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[usip] usip_enable_sanitizers:目标 ${TARGET} 不存在")
    endif()

    if(MSVC)
        if(USIP_ENABLE_ASAN)
            target_compile_options(${TARGET} PRIVATE /fsanitize=address /Zi)
            target_link_options(${TARGET} PRIVATE /fsanitize=address /INCREMENTAL:NO)
        endif()
        if(USIP_ENABLE_UBSAN)
            message(WARNING "[usip] MSVC 不支持 UBSan,已忽略 USIP_ENABLE_UBSAN")
        endif()
    else()
        set(_usip_san_list)
        if(USIP_ENABLE_ASAN)
            list(APPEND _usip_san_list address)
        endif()
        if(USIP_ENABLE_UBSAN)
            list(APPEND _usip_san_list undefined)
        endif()
        if(_usip_san_list)
            string(JOIN "," _usip_san_flags ${_usip_san_list})
            target_compile_options(${TARGET} PRIVATE
                "-fsanitize=${_usip_san_flags}" -fno-omit-frame-pointer)
            target_link_options(${TARGET} PRIVATE
                "-fsanitize=${_usip_san_flags}")
        endif()
    endif()
endfunction()
