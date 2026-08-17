add_library(usip_service STATIC)
add_library(usip::service ALIAS usip_service)

target_sources(usip_service
    PRIVATE
        src/service.cpp
        src/document_service.cpp
        src/file_service.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS inc
        FILES
            inc/service.hpp
            inc/document_service.hpp
            inc/file_service.hpp
)

target_include_directories(usip_service
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(usip_service
    PUBLIC
        usip::options
        usip::common
        usip::core
        usip::platform
        3rdparty::qt6
    PRIVATE
        usip::io # 仅 document_service.cpp 调用 load_tiff,不外泄到本层头文件
)

# PCH 组合:标准库 + 通用第三方(不引入 Qt,保持依赖卫生)
usip_apply_pch(usip_service)

set_target_properties(usip_service PROPERTIES FOLDER "usip/libs")
