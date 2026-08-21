cmake_minimum_required(VERSION 3.26)

project(usip VERSION 0.13.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(usip-config)        # 必须最先:全局配置 + usip::options
include(usip-version)       # 版本信息 + <usip/version.hpp> 生成
include(usip-dependencies)  # 全部第三方依赖(3rdparty::*)
include(usip-pch)           # 分层预编译头
include(usip-deploy)        # 运行时部署(全增量)
include(usip-install)       # 安装与打包(可选)
include(usip-sanitizers)    # ASan/UBSan(可选)

add_subdirectory(3rdparty)  # 自研库(以第三方形式引入)
add_subdirectory(src)
