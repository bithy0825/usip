# ==============================================================================
# usip-qt.cmake — Qt6(官方安装包,动态库)
#
# 动态理由:官方安装包仅提供动态库;静态 Qt 需自行编译且有 LGPL 合规负担。
#
# 提供:
#   3rdparty::qt6   INTERFACE 包装(Core/Gui/Widgets/OpenGLWidgets/Svg/Charts)
#
# 全局属性(供 usip-deploy.cmake / usip-install.cmake 使用):
#   USIP_QT_BIN_DIR / USIP_QT_PLUGINS_DIR
#
# 缓存变量:
#   QT_ROOT — Qt 安装前缀;缺省读环境变量 QT_ROOT,再回落默认路径
# ==============================================================================
include_guard(GLOBAL)

set(QT_ROOT "" CACHE PATH "Qt6 安装前缀(如 D:/Qt/6.11.1/msvc2022_64)")
if(NOT QT_ROOT)
    if(DEFINED ENV{QT_ROOT})
        set(QT_ROOT "$ENV{QT_ROOT}" CACHE PATH "Qt6 安装前缀" FORCE)
    else()
        set(QT_ROOT "D:/Qt/6.11.1/msvc2022_64" CACHE PATH "Qt6 安装前缀" FORCE)
    endif()
endif()

if(NOT EXISTS "${QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake")
    message(FATAL_ERROR
        "[usip] 未找到 Qt6:QT_ROOT='${QT_ROOT}'\n"
        "       请用 -DQT_ROOT=<Qt>/<版本>/<编译器> 或环境变量 QT_ROOT 指定")
endif()

list(APPEND CMAKE_PREFIX_PATH "${QT_ROOT}")

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    OpenGLWidgets
    Svg
    Charts
)

# Qt 6.3+:统一开启 AUTOMOC/AUTOUIC/AUTORCC 等工程约定
qt_standard_project_setup()

if(NOT TARGET 3rdparty_qt6)
    add_library(3rdparty_qt6 INTERFACE)
    target_link_libraries(3rdparty_qt6 INTERFACE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGLWidgets
        Qt6::Svg
        Qt6::Charts
    )
    target_compile_definitions(3rdparty_qt6 INTERFACE QT_NO_EMIT)
    add_library(3rdparty::qt6 ALIAS 3rdparty_qt6)
endif()

set_property(GLOBAL PROPERTY USIP_QT_BIN_DIR "${QT_ROOT}/bin")
set_property(GLOBAL PROPERTY USIP_QT_PLUGINS_DIR "${QT_ROOT}/plugins")

usip_log_dep("Qt6" "动态 · 官方安装包" "${Qt6_VERSION} — ${QT_ROOT}")
