add_library(usip_ui STATIC)
add_library(usip::ui ALIAS usip_ui)

target_sources(usip_ui
    PRIVATE
        src/main_window.cpp
        src/icon_registry.cpp
        src/menu_bar.cpp
        src/top_tool_bar.cpp
        src/options_tool_bar.cpp
        src/side_tool_bar.cpp
        src/index_dock.cpp
        src/info_dock.cpp
        src/hist_dock.cpp
        src/QRangeSlider.cpp
        src/canvas.cpp
        src/draw.cpp
        src/status_bar.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS inc
        FILES
            inc/main_window.hpp
            inc/ui_protocol.hpp
            inc/icon_registry.hpp
            inc/menu_bar.hpp
            inc/top_tool_bar.hpp
            inc/options_tool_bar.hpp
            inc/side_tool_bar.hpp
            inc/index_dock.hpp
            inc/info_dock.hpp
            inc/hist_dock.hpp
            inc/QRangeSlider.hpp
            inc/canvas.hpp
            inc/draw.hpp
            inc/tool.hpp
            inc/status_bar.hpp
)

# 自动收集 icon 目录下的 SVG,编译进 Qt 资源(:/icon/<name>.svg)
# 新增 SVG 文件丢进 assets/icon/ 即可,CMake 重新配置后自动收集
file(GLOB USIP_ICON_SVGS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/icon/*.svg")
qt_add_resources(usip_ui "usip_icons"
    PREFIX "/"
    BASE "${CMAKE_SOURCE_DIR}/assets"
    FILES ${USIP_ICON_SVGS}
)

target_include_directories(usip_ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(usip_ui
    PUBLIC
        usip::options
        usip::common
        usip::core
        3rdparty::qt6
    PRIVATE
        3rdparty::magic_enum # colormap 按钮的枚举遍历(实现细节,不入接口)
)

# PCH 组合:标准库 + 通用第三方(不引入 Qt,保持依赖卫生)
usip_apply_pch(usip_ui)

set_target_properties(usip_ui PROPERTIES FOLDER "usip/libs")
