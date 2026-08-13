# ==============================================================================
# io — 文件 IO(TIFF 读取 + 加载策略;规则本体与设备档案在 common/tiff.hpp)
# ==============================================================================
add_library(usip_io STATIC)
add_library(usip::io ALIAS usip_io)

target_sources(usip_io
    PRIVATE
        src/tiff_loader.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS inc
        FILES
            inc/tiff_loader.hpp
)

target_include_directories(usip_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(usip_io
    PUBLIC
        usip::options
        usip::common
        3rdparty::tiff
    PRIVATE
        3rdparty::highway
)

usip_apply_pch(usip_io)

set_target_properties(usip_io PROPERTIES FOLDER "usip/libs")
