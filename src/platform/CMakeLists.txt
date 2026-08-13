add_library(usip_platform STATIC)
add_library(usip::platform ALIAS usip_platform)

target_sources(usip_platform
    PRIVATE
        src/system_common.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS inc
        FILES
            inc/platform/system.hpp
            inc/platform/cpu.hpp
            inc/platform/gpu.hpp
)

if(WIN32)
    target_sources(usip_platform PRIVATE
        src/system_win32.cpp
        src/cpu_win32.cpp
        src/gpu_win32.cpp
    )
    target_link_libraries(usip_platform PRIVATE dxgi)   # gpu 信息走 DXGI
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(usip_platform PRIVATE
        src/system_linux.cpp
        src/cpu_linux.cpp
        src/gpu_linux.cpp
    )
else()
    message(FATAL_ERROR "[usip] platform 模块仅支持 Windows 与 Linux")
endif()

target_include_directories(usip_platform
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(usip_platform
    PUBLIC
        usip::options
        usip::common
)

usip_apply_pch(usip_platform)

set_target_properties(usip_platform PROPERTIES FOLDER "usip/libs")
