# ==============================================================================
# usip_copy_runtime.cmake — 单进程增量文件同步(以 cmake -P 调用)
#
# 参数(-D 传入):
#   USIP_SRC_DIR    源目录(同步其中全部 *.dll;与 USIP_FILES 二选一)
#   USIP_FILES      显式文件列表(分号分隔)
#   USIP_DST_DIR    目标目录(必填)
#   USIP_STAMP_FILE 可选;提供后启用快速路径 —— 源文件不旧于 stamp 即跳过,
#                   只做 stat 不做内容比较;每轮结束刷新 stamp
#
# 无论是否启用 stamp,实际写盘一律 file(COPY_FILE ONLY_IF_DIFFERENT),
# 内容相同不产生任何写入。
# ==============================================================================

if(NOT DEFINED USIP_DST_DIR)
    message(FATAL_ERROR "[usip] usip_copy_runtime:缺少 USIP_DST_DIR")
endif()

if(DEFINED USIP_SRC_DIR)
    if(NOT EXISTS "${USIP_SRC_DIR}")
        message(STATUS "[usip] 跳过:${USIP_SRC_DIR} 不存在")
        return()
    endif()
    file(GLOB _usip_files "${USIP_SRC_DIR}/*.dll")
elseif(DEFINED USIP_FILES)
    set(_usip_files ${USIP_FILES})
else()
    message(FATAL_ERROR "[usip] usip_copy_runtime:需要 USIP_SRC_DIR 或 USIP_FILES")
endif()

if(NOT _usip_files)
    return()
endif()

file(MAKE_DIRECTORY "${USIP_DST_DIR}")

foreach(_usip_file IN LISTS _usip_files)
    get_filename_component(_usip_name "${_usip_file}" NAME)
    set(_usip_dst "${USIP_DST_DIR}/${_usip_name}")

    # 快速路径:目标存在且源文件不旧于 stamp → 上轮已同步,仅 stat 即跳过
    set(_usip_skip FALSE)
    if(DEFINED USIP_STAMP_FILE AND EXISTS "${USIP_STAMP_FILE}" AND EXISTS "${_usip_dst}")
        if(NOT "${_usip_file}" IS_NEWER_THAN "${USIP_STAMP_FILE}")
            set(_usip_skip TRUE)
        endif()
    endif()
    if(_usip_skip)
        continue()
    endif()

    # 慢速路径:仅内容不同才写盘(注意 COPY_FILE 的目标是完整文件路径)
    file(COPY_FILE "${_usip_file}" "${_usip_dst}" ONLY_IF_DIFFERENT)
endforeach()

if(DEFINED USIP_STAMP_FILE)
    # TOUCH 不会创建父目录(目标首次构建时可能尚不存在)
    get_filename_component(_usip_stamp_dir "${USIP_STAMP_FILE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_usip_stamp_dir}")
    file(TOUCH "${USIP_STAMP_FILE}")
endif()
