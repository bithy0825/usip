# usip CMake 模块

统一、现代、增量高效的 CMake 构建基础设施。要求 **CMake ≥ 3.26**,工具链为 MSVC 2022 + vcpkg。

## 目录结构

```
cmake/
├── usip-config.cmake          # 全局配置:C++23、输出目录、usip::options(警告/宏)
├── usip-version.cmake         # 版本来源 + <usip/version.hpp> 生成
├── usip-dependencies.cmake    # 一键引入全部第三方依赖
├── usip-pch.cmake             # 分层预编译头
├── usip-deploy.cmake          # 运行时部署(Qt/CRT/资产/翻译,全增量)
├── usip-install.cmake         # install + 运行时依赖收集 + CPack(可选)
├── usip-sanitizers.cmake      # ASan/UBSan(可选)
├── usip-version.hpp.in        # 版本头模板
├── deps/                      # 第三方依赖(每库一个模块)
│   ├── usip-qt.cmake          #   Qt6    动态 · 官方安装包(不走 vcpkg)
│   ├── usip-spdlog.cmake      #   spdlog 动态 · vcpkg(全局 registry 单例)
│   ├── usip-tbb.cmake         #   oneTBB 动态 · vcpkg(全局调度器,官方已无静态)
│   ├── usip-libtiff.cmake     #   libtiff 动态 · vcpkg(C ABI,传递依赖多)
│   ├── usip-highway.cmake     #   highway 静态 · vcpkg(SIMD 内联,官方主推)
│   ├── usip-tomlpp.cmake      #   tomlplusplus 纯头文件 · vcpkg
│   ├── usip-clipper2.cmake    #   clipper2 vcpkg(多边形布尔运算与偏移)
│   ├── usip-vtk.cmake         #   VTK    (未启用,保留备查)
│   ├── usip-exprtk.cmake      #   exprtk (未启用,保留备查)
│   └── usip-json.cmake        #   nlohmann_json (未启用,保留备查)
└── scripts/                   # cmake -P 部署脚本(单进程、增量)
    ├── usip_copy_runtime.cmake
    └── usip_deploy_qt.cmake
```

## 快速上手(父工程 CMakeLists.txt)

```cmake
cmake_minimum_required(VERSION 3.26)
project(usip VERSION 0.1.0 LANGUAGES CXX)   # VERSION 是版本的唯一来源

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(usip-config)          # 必须最先
include(usip-version)
include(usip-dependencies)    # 全部第三方库
include(usip-pch)
include(usip-deploy)
include(usip-install)         # 可选
include(usip-sanitizers)      # 可选

add_executable(usip WIN32 main.cpp ...)
target_link_libraries(usip PRIVATE
    usip::options             # C++23 + /W4 + 公共宏
    3rdparty::qt6
    3rdparty::spdlog
    3rdparty::tomlpp
    3rdparty::tiff
    3rdparty::highway
    3rdparty::tbb
    3rdparty::clipper2
)

usip_add_version_header(usip) # 之后可 #include <usip/version.hpp>
usip_apply_pch(usip)
usip_deploy_all(usip)         # Qt + CRT + 资产 + 翻译
usip_install_executable(usip) # 可选
```

配置与构建(需先设置 `VCPKG_ROOT` 环境变量):

```bash
cmake --preset ninja
cmake --build --preset ninja-debug
```

## 增量部署机制(未变化不搬运)

| 内容 | 机制 | 无变化时的开销 |
|---|---|---|
| CRT DLL | POST_BUILD + `usip_copy_runtime.cmake` 单进程脚本:stamp 快速路径仅 stat;写盘一律 `ONLY_IF_DIFFERENT` | 重链接才触发;触发也仅毫秒级 stat |
| Qt DLL | windeployqt(自身跳过已同步文件)+ `$<TARGET_FILE:Qt6::OpenGL*>` 按配置解析(自动 d 后缀) | 重链接才触发 |
| vcpkg DLL(TBB/TIFF/spdlog…) | **vcpkg applocal 自动处理**,不要手动拷贝 | 自动增量 |
| 资产(qss/glsl) | `OUTPUT/DEPENDS` 构建图规则 | 零进程,生成器原生跳过 |
| 翻译(ts→qm) | lrelease 进入构建图 | 零进程,ts 不变不重编 |

## PCH:全项目唯一聚合 PCH

一个编译单元只能用一份 PCH,MSVC 又不支持链式叠加——因此全项目只编译**一份**聚合 PCH(`stl + thirdparty + qt`),owner 为 app 层目标 `usip`,其余目标一律 `REUSE_FROM` 复用。多组合分档的旧方案(每个模块各编一份)已废弃:重复编译、各占数百 MB、组合随模块数发散。

```cmake
# 各模块 CMakeLists(仅登记,顺序无关)
usip_apply_pch(usip_core)

# src/CMakeLists.txt 末尾(统一落地)
usip_pch_finalize()
```

约定:
- 任何模块在预处理器层面都能看见 Qt 头——这是聚合的已知代价;**依赖纪律由链接的 PRIVATE/PUBLIC 划分保证**,低层模块不得包含 Qt 头(code review 把关)。
- 片段只放稳定头(STL/第三方),项目头一律不放。
- 新模块只需 `usip_apply_pch(<target>)`,不得自建 PCH 组合。
- 自定义头可用 `target_precompile_headers` 之外的……不需要,唯一 PCH 即全部。

## 选项一览

| 选项 | 默认 | 说明 |
|---|---|---|
| `USIP_WARNINGS_AS_ERRORS` | OFF | 警告视为错误 |
| `USIP_ENABLE_IPO` | OFF | Release 启用 LTO |
| `USIP_ENABLE_PCH` | ON | PCH 总开关 |
| `USIP_ENABLE_ASAN` / `USIP_ENABLE_UBSAN` | OFF | 消毒剂(配合 `usip_enable_sanitizers`) |
| `USIP_ENABLE_PACKAGING` | OFF | CPack 打包 |
| `QT_ROOT` / `USIP_ASSETS_DIR` / `USIP_PCH_HEADER` | 见各模块 | 路径覆盖(-D 或同名环境变量) |
