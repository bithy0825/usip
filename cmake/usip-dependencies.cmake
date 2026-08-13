# ==============================================================================
# usip-dependencies.cmake — 一键引入全部第三方依赖
#
# 包装约定:每个库统一为 3rdparty_<name> INTERFACE 目标 + 3rdparty::<name> ALIAS,
# 便于集中挂载编译定义、SYSTEM include 与未来的替换/裁剪。
#
#   Qt6                  外部安装(动态),不走 vcpkg
#   spdlog / TBB / TIFF  vcpkg(动态)—— 全局 registry/调度器单例,必须全进程唯一
#   highway              vcpkg(静态)—— SIMD 内联与架构分派,官方主推静态
#   clipper2/tomlplusplus vcpkg(纯头文件)
#
# 依赖 usip-config(提供 usip_log_dep),必须先 include(usip-config)。
# ==============================================================================
include_guard(GLOBAL)

if(NOT COMMAND usip_log_dep)
    message(FATAL_ERROR "[usip] 请先 include(usip-config),再 include(usip-dependencies)")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/deps")

include(usip-qt)
include(usip-spdlog)
include(usip-tomlpp)
include(usip-libtiff)
include(usip-highway)
include(usip-tbb)
include(usip-clipper2)
# include(usip-vtk)       # 不再使用 VTK
# include(usip-exprtk)    # 不再使用 exprtk
# include(usip-json)      # 不再使用 nlohmann_json
