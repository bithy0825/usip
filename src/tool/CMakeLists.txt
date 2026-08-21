# ==============================================================================
# tool — 画布工具(静态库):算法侧,无总线/无控件;canvas 编排调用
# ==============================================================================
add_library(usip_tool STATIC)
add_library(usip::tool ALIAS usip_tool)

target_sources(usip_tool
    PRIVATE
        src/threshold_tool.cpp
        src/annotation_tool.cpp
        src/roi_tool.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS inc
        FILES
            inc/tool.hpp
            inc/threshold_tool.hpp
            inc/annotation_tool.hpp
            inc/roi_tool.hpp
)

target_include_directories(usip_tool
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(usip_tool
    PUBLIC
        usip::options
        usip::common
        usip::core
        3rdparty::qt6 # QImage 与 core::mask(document.hpp 通信基准)
    PRIVATE
        3rdparty::highway # 阈值核 SIMD(实现细节,不入接口)
)

# PCH 组合:标准库 + 通用第三方(不引入 Qt,保持依赖卫生)
usip_apply_pch(usip_tool)

set_target_properties(usip_tool PROPERTIES FOLDER "usip/libs")
