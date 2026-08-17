add_library(usip_common STATIC)
add_library(usip::common ALIAS usip_common)

target_sources(usip_common
    PRIVATE
        src/executor.cpp
        src/logger.cpp
        src/tiff.cpp
        src/utility.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS inc
        FILES
            inc/error.hpp
            inc/executor.hpp
            inc/logger.hpp
            inc/utility.hpp
            inc/tiff.hpp
)

target_include_directories(usip_common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(usip_common
    PUBLIC
        usip::options
        3rdparty::spdlog
        3rdparty::tbb
        3rdparty::cbuspp
        3rdparty::cuuidpp
    PRIVATE
        3rdparty::highway
)

usip_apply_pch(usip_common)

set_target_properties(usip_common PROPERTIES FOLDER "usip/libs")
