# ==============================================================================
# core — 基础库:日志 / 配置 / 文档 / 运行时 / 工具(静态库)
#
# 说明:
#   * 内部库一律静态,最终单 exe + 第三方 DLL,部署最干净
# ==============================================================================
add_library(usip_core STATIC)
add_library(usip::core ALIAS usip_core)

target_sources(usip_core
    PRIVATE
        src/config.cpp
        src/colormap.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS inc
        FILES
            inc/settings.hpp
            inc/config.hpp
            inc/event.hpp
            inc/document.hpp
            inc/colormap.hpp
            inc/view_mode.hpp
)

target_include_directories(usip_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(usip_core
    PUBLIC
        usip::options
        usip::common
        3rdparty::spdlog
        3rdparty::tiff
        3rdparty::tomlpp
        3rdparty::tbb
        3rdparty::cbuspp
        3rdparty::qt6 # document.hpp 的 QImage/QPointF(通信基准需要)
        3rdparty::clipper2 # roi 的矢量路径
    PRIVATE
        3rdparty::highway # colormap 的 SIMD 内核(实现细节,不入接口)
)

# PCH 组合:标准库 + 通用第三方(clipper2/Qt 仅个别头文件使用,不入 PCH)
usip_apply_pch(usip_core)

set_target_properties(usip_core PROPERTIES FOLDER "usip/libs")
