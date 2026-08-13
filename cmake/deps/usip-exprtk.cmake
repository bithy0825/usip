# ==============================================================================
# usip-exprtk.cmake — exprtk(vcpkg,纯头文件)
#
# 官方仅发布头文件(单头逾五万行),vcpkg 端口不含 config 文件,只能 find_path。
# 注意:该头编译极慢,建议仅在少数 .cpp 中包含(编译防火墙),或用
# exprtk_disable_* 宏裁剪功能;一般不建议放入 PCH。
#
# 提供:3rdparty::exprtk
# ==============================================================================
include_guard(GLOBAL)

find_path(EXPRTK_INCLUDE_DIR
    NAMES exprtk.hpp
    DOC "exprtk 头文件目录(vcpkg 安装)"
)

if(NOT EXPRTK_INCLUDE_DIR)
    message(FATAL_ERROR
        "[usip] 未找到 exprtk.hpp,请执行:vcpkg install exprtk:x64-windows")
endif()

if(NOT TARGET 3rdparty_exprtk)
    add_library(3rdparty_exprtk INTERFACE)
    # SYSTEM:include 目录按系统头处理,第三方警告不污染 /W4
    target_include_directories(3rdparty_exprtk SYSTEM INTERFACE "${EXPRTK_INCLUDE_DIR}")
    add_library(3rdparty::exprtk ALIAS 3rdparty_exprtk)
endif()

usip_log_dep("exprtk" "纯头文件 · vcpkg" "${EXPRTK_INCLUDE_DIR}")
