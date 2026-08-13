# ==============================================================================
# usip-deploy.cmake — 运行时部署(Qt / MSVC CRT / 资产 / 翻译)
#
# 效率设计 —— 未变化不搬运:
#   * POST_BUILD 仅在目标重新链接后触发,未重链接的增量构建零开销
#   * DLL 同步走单进程脚本(scripts/usip_copy_runtime.cmake):
#       快速路径 —— 源文件不旧于 stamp,仅 stat,免内容比较
#       慢速路径 —— file(COPY_FILE ONLY_IF_DIFFERENT),内容相同不写盘
#   * 资产/翻译进入构建依赖图(OUTPUT/DEPENDS),由生成器做文件级增量:
#     无变化时零进程,有变化时只拷贝变化的文件且可并行
#   * vcpkg 依赖(TBB/TIFF/spdlog 等)的 DLL 由 vcpkg applocal 自动增量拷贝,
#     无需也不应在此重复
#
# 函数:
#   usip_deploy_qt(TARGET)            windeployqt + Qt OpenGL DLL(自动区分 Debug/Release)
#   usip_deploy_msvc_runtime(TARGET)  VC redist CRT(InstallRequiredSystemLibraries)
#   usip_deploy_styles(TARGET)        assets/qss/*.json       → <exe>/styles/
#   usip_deploy_shaders(TARGET)       assets/shader/**/*.glsl → <exe>/shaders/(保留层级)
#   usip_deploy_i18n(TARGET)          assets/i18n/*.ts → lrelease → <exe>/i18n/*.qm
#   usip_deploy_all(TARGET)           以上全部
#
# 缓存变量:
#   USIP_ASSETS_DIR — 资产根目录,默认 ${CMAKE_SOURCE_DIR}/assets
# ==============================================================================
include_guard(GLOBAL)

set(USIP_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets" CACHE PATH "资产目录(含 qss/shader/i18n 子目录)")

# VC redist CRT 列表(含调试版,供构建树 Debug 运行与 install 共用)
if(MSVC)
    set(CMAKE_INSTALL_DEBUG_LIBRARIES ON)
    include(InstallRequiredSystemLibraries)
    set_property(GLOBAL PROPERTY USIP_MSVC_RUNTIME_LIBS "${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
endif()

# ---- 内部:注册一条"源变化才复制"的资产规则 -----------------------------------
# _usip_add_asset_rule(<target> <源文件> <目标相对路径> <输出列表变量名>)
#
# 注意:OUTPUT/BYPRODUCTS 只允许配置级生成器表达式($<CONFIG> 等),
# 目标级表达式($<TARGET_FILE_DIR:...>)仅可用于 COMMAND 参数
# (Ninja Multi-Config 等生成器会在 generate 期展开 OUTPUT)。
function(_usip_add_asset_rule TARGET SRC_FILE DST_REL_PATH OUT_VAR)
    get_target_property(_out_dir ${TARGET} RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _out_dir)
        set(_out_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    endif()
    set(_dst "${_out_dir}/${DST_REL_PATH}")
    get_filename_component(_dst_dir "${DST_REL_PATH}" DIRECTORY)
    add_custom_command(
        OUTPUT "${_dst}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_out_dir}/${_dst_dir}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${SRC_FILE}" "${_dst}"
        MAIN_DEPENDENCY "${SRC_FILE}"
        COMMENT "[usip] 部署 ${DST_REL_PATH}"
        VERBATIM
    )
    set(${OUT_VAR} ${${OUT_VAR}} "${_dst}" PARENT_SCOPE)
endfunction()

# ---- 内部:把资产输出汇总为 custom target 并挂到主目标 -------------------------
# _usip_add_asset_target(<target> <类别名> [输出...])
function(_usip_add_asset_target TARGET KIND)
    if(NOT ARGN)
        return()
    endif()
    add_custom_target(${TARGET}_deploy_${KIND} DEPENDS ${ARGN})
    set_target_properties(${TARGET}_deploy_${KIND} PROPERTIES FOLDER "usip/deploy")
    add_dependencies(${TARGET} ${TARGET}_deploy_${KIND})
endfunction()

function(usip_deploy_qt TARGET)
    if(NOT WIN32)
        return()
    endif()
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[usip] usip_deploy_qt:目标 ${TARGET} 不存在")
    endif()
    get_property(_usip_qt_bin GLOBAL PROPERTY USIP_QT_BIN_DIR)
    if(NOT _usip_qt_bin)
        message(FATAL_ERROR "[usip] usip_deploy_qt:未找到 Qt,请先 include(usip-dependencies)")
    endif()

    find_program(USIP_WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt windeployqt6
        HINTS "${_usip_qt_bin}"
    )
    if(NOT USIP_WINDEPLOYQT_EXECUTABLE)
        message(WARNING "[usip] 未找到 windeployqt,跳过 Qt 部署")
        return()
    endif()

    # windeployqt 自身会跳过已同步的文件;$<TARGET_FILE:Qt6::...> 按配置解析,
    # 自动选中 Debug(d 后缀)/Release 版本,修复手工写死 DLL 名的问题
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -D "USIP_TARGET_FILE=$<TARGET_FILE:${TARGET}>"
            -D "USIP_WINDEPLOYQT=${USIP_WINDEPLOYQT_EXECUTABLE}"
            -D "USIP_EXTRA_DLLS=$<TARGET_FILE:Qt6::OpenGL>;$<TARGET_FILE:Qt6::OpenGLWidgets>"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/usip_deploy_qt.cmake"
        COMMENT "[usip] 部署 Qt 运行时"
        VERBATIM
    )
endfunction()

function(usip_deploy_msvc_runtime TARGET)
    if(NOT WIN32 OR NOT MSVC)
        return()
    endif()
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[usip] usip_deploy_msvc_runtime:目标 ${TARGET} 不存在")
    endif()
    get_property(_usip_crt_libs GLOBAL PROPERTY USIP_MSVC_RUNTIME_LIBS)
    if(NOT _usip_crt_libs)
        message(WARNING "[usip] 未找到 VC redist 运行时,跳过 CRT 部署")
        return()
    endif()

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -D "USIP_FILES=${_usip_crt_libs}"
            -D "USIP_DST_DIR=$<TARGET_FILE_DIR:${TARGET}>"
            -D "USIP_STAMP_FILE=${CMAKE_CURRENT_BINARY_DIR}/usip/msvc_crt_$<CONFIG>.stamp"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/scripts/usip_copy_runtime.cmake"
        COMMENT "[usip] 同步 MSVC 运行时(增量)"
        VERBATIM
    )
endfunction()

function(usip_deploy_styles TARGET)
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[usip] usip_deploy_styles:目标 ${TARGET} 不存在")
    endif()
    set(_src_dir "${USIP_ASSETS_DIR}/qss")
    if(NOT EXISTS "${_src_dir}")
        return()
    endif()

    file(GLOB _files CONFIGURE_DEPENDS "${_src_dir}/*.json")
    set(_outputs)
    foreach(_file IN LISTS _files)
        get_filename_component(_name "${_file}" NAME)
        _usip_add_asset_rule(${TARGET} "${_file}" "styles/${_name}" _outputs)
    endforeach()
    _usip_add_asset_target(${TARGET} styles ${_outputs})
endfunction()

function(usip_deploy_shaders TARGET)
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[usip] usip_deploy_shaders:目标 ${TARGET} 不存在")
    endif()
    set(_src_dir "${USIP_ASSETS_DIR}/shader")
    if(NOT EXISTS "${_src_dir}")
        return()
    endif()

    file(GLOB_RECURSE _files CONFIGURE_DEPENDS "${_src_dir}/*.glsl")
    set(_outputs)
    foreach(_file IN LISTS _files)
        file(RELATIVE_PATH _rel "${_src_dir}" "${_file}")
        _usip_add_asset_rule(${TARGET} "${_file}" "shaders/${_rel}" _outputs)
    endforeach()
    _usip_add_asset_target(${TARGET} shaders ${_outputs})
endfunction()

function(usip_deploy_i18n TARGET)
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[usip] usip_deploy_i18n:目标 ${TARGET} 不存在")
    endif()
    set(_src_dir "${USIP_ASSETS_DIR}/i18n")
    if(NOT EXISTS "${_src_dir}")
        return()
    endif()

    get_property(_usip_qt_bin GLOBAL PROPERTY USIP_QT_BIN_DIR)
    find_program(USIP_LRELEASE_EXECUTABLE
        NAMES lrelease lrelease6
        HINTS "${_usip_qt_bin}"
    )
    if(NOT USIP_LRELEASE_EXECUTABLE)
        message(WARNING "[usip] 未找到 lrelease,跳过翻译编译")
        return()
    endif()

    file(GLOB _ts_files CONFIGURE_DEPENDS "${_src_dir}/*.ts")
    set(_outputs)
    foreach(_ts IN LISTS _ts_files)
        get_filename_component(_lang "${_ts}" NAME_WE)
        set(_qm "${CMAKE_CURRENT_BINARY_DIR}/usip/i18n/${_lang}.qm")
        # ts → qm:进入依赖图,ts 未变不重编译
        add_custom_command(
            OUTPUT "${_qm}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/usip/i18n"
            COMMAND "${USIP_LRELEASE_EXECUTABLE}" "${_ts}" -qm "${_qm}"
            MAIN_DEPENDENCY "${_ts}"
            COMMENT "[usip] 编译翻译 ${_lang}.ts → ${_lang}.qm"
            VERBATIM
        )
        # qm → <exe>/i18n:同上,未变不拷贝
        _usip_add_asset_rule(${TARGET} "${_qm}" "i18n/${_lang}.qm" _outputs)
    endforeach()
    _usip_add_asset_target(${TARGET} i18n ${_outputs})
endfunction()

function(usip_deploy_all TARGET)
    usip_deploy_qt(${TARGET})
    usip_deploy_msvc_runtime(${TARGET})
    usip_deploy_styles(${TARGET})
    usip_deploy_shaders(${TARGET})
    usip_deploy_i18n(${TARGET})
endfunction()
