# ==============================================================================
# cuuidpp — 自研 uuid 库(RFC 9562 v7/v4)
#
# 注意:必须是 STATIC —— generate_uuid_v7/v4 的实现在 cuuidpp.cpp,
# INTERFACE 库不会编译任何 .cpp,调用方链接时必报 LNK2019。
# ==============================================================================
add_library(3rdparty_cuuidpp STATIC)
add_library(3rdparty::cuuidpp ALIAS 3rdparty_cuuidpp)

target_sources(3rdparty_cuuidpp
    PRIVATE
        cuuidpp.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/..
        FILES
            cuuidpp.hpp
)

target_include_directories(3rdparty_cuuidpp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>
)

# 自研代码,与项目同选项(C++23 / /utf-8 中文注释)
target_link_libraries(3rdparty_cuuidpp PRIVATE usip::options)

set_target_properties(3rdparty_cuuidpp PROPERTIES FOLDER "usip/3rdparty")

message(STATUS "[usip] cuuidpp [自研 · 静态] ${CMAKE_CURRENT_SOURCE_DIR}")
