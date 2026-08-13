# ==============================================================================
# usip-install.cmake — 安装与打包
#
# 函数:
#   usip_install_executable(<target>)
#       安装目标,并以 RUNTIME_DEPENDENCY_SET 自动收集第三方运行时 DLL
#       (Qt/vcpkg),系统 DLL 按规则排除;同时安装资产与翻译产物。
#
# 选项:
#   USIP_ENABLE_PACKAGING  OFF  启用 CPack(默认 ZIP,可 -DCPACK_GENERATOR=NSIS)
# ==============================================================================
include_guard(GLOBAL)

include(GNUInstallDirs)

function(usip_install_executable TARGET)
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[usip] usip_install_executable:目标 ${TARGET} 不存在")
    endif()

    set(_deps_set "usip_${TARGET}_runtime_deps")

    install(TARGETS ${TARGET}
        RUNTIME_DEPENDENCY_SET "${_deps_set}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )

    # 运行时 DLL 搜索目录:Qt bin、vcpkg bin
    set(_search_dirs)
    get_property(_usip_qt_bin GLOBAL PROPERTY USIP_QT_BIN_DIR)
    foreach(_dir IN ITEMS
            "${_usip_qt_bin}"
            "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
            "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin")
        if(EXISTS "${_dir}")
            list(APPEND _search_dirs "${_dir}")
        endif()
    endforeach()

    install(RUNTIME_DEPENDENCY_SET "${_deps_set}"
        PRE_EXCLUDE_REGEXES
            [[api-ms-win-.*]]
            [[ext-ms-.*]]
            [[hvsifiletrust\.dll]]
            [[ieshims\.dll]]
        POST_EXCLUDE_REGEXES
            ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Tt][Ee][Mm]32[/\\\\].*"
            ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ss][Yy][Ss][Ww][Oo][Ww]64[/\\\\].*"
            ".*[Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\][Ww][Ii][Nn][Ss][Xx][Ss][/\\\\].*"
        DIRECTORIES ${_search_dirs}
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )

    # 资产与翻译产物(存在才安装,布局与构建树一致)
    if(EXISTS "${USIP_ASSETS_DIR}/qss")
        install(DIRECTORY "${USIP_ASSETS_DIR}/qss/"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/styles"
            FILES_MATCHING PATTERN "*.json")
    endif()
    if(EXISTS "${USIP_ASSETS_DIR}/shader")
        install(DIRECTORY "${USIP_ASSETS_DIR}/shader/"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/shaders"
            FILES_MATCHING PATTERN "*.glsl")
    endif()
    if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/usip/i18n")
        install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/usip/i18n/"
            DESTINATION "${CMAKE_INSTALL_BINDIR}/i18n"
            FILES_MATCHING PATTERN "*.qm")
    endif()
endfunction()

option(USIP_ENABLE_PACKAGING "启用 CPack 打包" OFF)
if(USIP_ENABLE_PACKAGING)
    set(CPACK_PACKAGE_NAME "usip")
    set(CPACK_PACKAGE_VENDOR "usip")
    if(DEFINED USIP_VERSION)
        set(CPACK_PACKAGE_VERSION "${USIP_VERSION}")
    endif()
    set(CPACK_GENERATOR "ZIP" CACHE STRING "CPack 打包格式(ZIP/NSIS/TGZ)")
    include(CPack)
endif()
