# ==============================================================================
# usip_deploy_qt.cmake — windeployqt 包装(以 cmake -P 调用,POST_BUILD 触发)
#
# 参数(-D 传入):
#   USIP_TARGET_FILE  可执行文件路径
#   USIP_WINDEPLOYQT  windeployqt.exe 路径
#   USIP_EXTRA_DLLS   需额外同步的 Qt DLL(以 $<TARGET_FILE:Qt6::...> 传入,
#                     已按配置解析,Debug 自动带 d 后缀)
#
# POST_BUILD 语义保证仅在重链接后调用;windeployqt 自身会跳过已是最新的文件,
# 额外 DLL 用 ONLY_IF_DIFFERENT 兜底,不产生重复写盘。
# ==============================================================================

if(NOT DEFINED USIP_TARGET_FILE OR NOT DEFINED USIP_WINDEPLOYQT)
    message(FATAL_ERROR "[usip] usip_deploy_qt:缺少 USIP_TARGET_FILE 或 USIP_WINDEPLOYQT")
endif()

get_filename_component(_usip_dst_dir "${USIP_TARGET_FILE}" DIRECTORY)

execute_process(
    COMMAND "${USIP_WINDEPLOYQT}"
        --no-translations
        --no-system-d3d-compiler
        --no-compiler-runtime
        --no-opengl-sw
        "${USIP_TARGET_FILE}"
    WORKING_DIRECTORY "${_usip_dst_dir}"
    RESULT_VARIABLE _usip_rc
    OUTPUT_VARIABLE _usip_out
    ERROR_VARIABLE  _usip_err
)
if(NOT _usip_rc EQUAL 0)
    # 目标暂未使用 Qt(如 UI 层未接入的过渡期):静默跳过,不算构建失败
    if(_usip_out MATCHES "does not seem to be a Qt" OR _usip_err MATCHES "does not seem to be a Qt")
        message(STATUS "[usip] 目标未引用 Qt,跳过 Qt 运行时部署")
        return()
    endif()
    message(FATAL_ERROR "[usip] windeployqt 失败(退出码 ${_usip_rc}):\n${_usip_out}\n${_usip_err}")
endif()

# Qt OpenGL DLL(windeployqt 不主动包含);内容相同不写盘
foreach(_usip_dll IN LISTS USIP_EXTRA_DLLS)
    if(EXISTS "${_usip_dll}")
        get_filename_component(_usip_name "${_usip_dll}" NAME)
        file(COPY_FILE "${_usip_dll}" "${_usip_dst_dir}/${_usip_name}" ONLY_IF_DIFFERENT)
    endif()
endforeach()

message(STATUS "[usip] Qt 运行时部署完成 → ${_usip_dst_dir}")
