# ==============================================================================
# app — 主可执行程序
# ==============================================================================
# add_executable(usip WIN32)  # 发布时启用 WIN32(隐藏控制台窗口)
add_executable(usip)

target_sources(usip
    PRIVATE
        main.cpp
        app.cpp
        app.hpp
)

target_link_libraries(usip
    PRIVATE
        usip::options
        usip::core
        usip::platform
        usip::ui            
        # usip::rendering     # 待添加
        # usip::service       # 待添加
        3rdparty::qt6
        3rdparty::cbuspp
)

# 版本头:#include <usip/version.hpp>
usip_add_version_header(usip)

usip_apply_pch(usip)

# 部署:Qt + MSVC CRT + 资产(styles/shaders)+ 翻译(i18n),全部增量
usip_deploy_all(usip)

# 安装:exe + 自动收集运行时 DLL + 资产(配合 -DUSIP_ENABLE_PACKAGING=ON 打包)
usip_install_executable(usip)
