# ==============================================================================
# usip-vtk.cmake — VTK(源码自行编译,动态库)
#
# 动态理由:VTK 模块注册/autoinit 机制为动态加载设计;静态链接数百个模块极慢,
# 官方亦以动态为主推方式。
#
# 提供:
#   3rdparty::vtk                INTERFACE 包装
#   usip_vtk_autoinit(<target>)  VTK 模块自动初始化
#       注意:vtk_module_autoinit 内部对目标使用 PRIVATE 属性,无法作用于
#       INTERFACE 库;必须在最终链接 VTK 的可执行/动态库目标上调用本函数。
#
# 全局属性(供 usip-deploy.cmake / usip-install.cmake 使用):
#   USIP_VTK_MODULES      VTK 模块列表
#   USIP_VTK_RUNTIME_DIR  运行时目录(<VTK_DIR>/bin,内含 Debug/Release 子目录)
#
# 缓存变量:
#   VTK_DIR — VTK 构建/安装目录;缺省读环境变量 VTK_DIR,再回落默认路径
# ==============================================================================
include_guard(GLOBAL)

set(VTK_DIR "" CACHE PATH "VTK 构建/安装目录(含 vtk-config.cmake)")
if(NOT VTK_DIR)
    if(DEFINED ENV{VTK_DIR})
        set(VTK_DIR "$ENV{VTK_DIR}" CACHE PATH "VTK 构建/安装目录" FORCE)
    else()
        set(VTK_DIR "D:/BuildTools/VTK-9.6.2/build_usip" CACHE PATH "VTK 构建/安装目录" FORCE)
    endif()
endif()

if(NOT EXISTS "${VTK_DIR}")
    message(FATAL_ERROR
        "[usip] VTK_DIR 不存在:'${VTK_DIR}'\n"
        "       请用 -DVTK_DIR= 或环境变量 VTK_DIR 指定 VTK 构建/安装目录")
endif()

list(APPEND CMAKE_PREFIX_PATH "${VTK_DIR}")

find_package(VTK REQUIRED CONFIG COMPONENTS
    CommonCore
    CommonDataModel
    CommonExecutionModel
    CommonMath
    CommonMisc
    CommonSystem
    CommonTransforms
    IOImage
    IOCore
    ImagingCore
    ImagingGeneral
    ImagingMath
    ImagingSources
    ImagingStatistics
    ImagingFourier
    ImagingHybrid
    ImagingMorphological
    ImagingColor
    FiltersCore
    FiltersGeneral
    FiltersSources
    FiltersStatistics
    FiltersHybrid
    FiltersModeling
    FiltersExtraction
    FiltersGeometry
    RenderingCore
    RenderingOpenGL2
    RenderingVolumeOpenGL2
    RenderingVolume
    RenderingFreeType
    RenderingAnnotation
    RenderingImage
    RenderingUI
    InteractionStyle
    InteractionWidgets
    GUISupportQt
)

# 仅静态构建的 VTK 才需要下游显式提供 Freetype;动态构建由 VTK DLL 自行携带
if(DEFINED VTK_BUILD_SHARED_LIBS AND NOT VTK_BUILD_SHARED_LIBS)
    find_package(Freetype REQUIRED)
endif()

if(NOT TARGET 3rdparty_vtk)
    add_library(3rdparty_vtk INTERFACE)
    target_link_libraries(3rdparty_vtk INTERFACE ${VTK_LIBRARIES})
    add_library(3rdparty::vtk ALIAS 3rdparty_vtk)
endif()

set_property(GLOBAL PROPERTY USIP_VTK_MODULES "${VTK_LIBRARIES}")
set_property(GLOBAL PROPERTY USIP_VTK_RUNTIME_DIR "${VTK_DIR}/bin")

if(NOT EXISTS "${VTK_DIR}/bin/Debug")
    message(STATUS "[usip] VTK 无 Debug 运行时目录,Debug 配置将复用 Release DLL")
endif()
# VTK 仅构建了 Release 时:把各导入目标的 Debug 映射到 Release 导入库,
# 使 Debug 配置可正常链接(/MDd 应用 + /MD VTK 的 CRT 混用为已知风险,
# VTK 跨 DLL 边界不转移 CRT 对象所有权,工程实践可行)
if(EXISTS "${VTK_DIR}/lib/Release" AND NOT EXISTS "${VTK_DIR}/lib/Debug")
    get_property(_usip_vtk_targets DIRECTORY PROPERTY IMPORTED_TARGETS)
    foreach(_t IN LISTS _usip_vtk_targets)
        if(_t MATCHES "^VTK::")
            set_target_properties(${_t} PROPERTIES MAP_IMPORTED_CONFIG_DEBUG Release)
        endif()
    endforeach()
    message(STATUS "[usip] VTK 仅有 Release 构建:已将 VTK::* 的 Debug 映射到 Release 导入库")
endif()

function(usip_vtk_autoinit TARGET)
    if(NOT TARGET ${TARGET})
        message(FATAL_ERROR "[usip] usip_vtk_autoinit:目标 ${TARGET} 不存在")
    endif()
    get_property(_usip_vtk_modules GLOBAL PROPERTY USIP_VTK_MODULES)
    if(NOT _usip_vtk_modules)
        message(FATAL_ERROR "[usip] usip_vtk_autoinit:VTK 模块列表为空,请先 include(usip-dependencies)")
    endif()
    vtk_module_autoinit(TARGETS ${TARGET} MODULES ${_usip_vtk_modules})
endfunction()

usip_log_dep("VTK" "动态 · 本地编译" "${VTK_VERSION} — ${VTK_DIR}")
