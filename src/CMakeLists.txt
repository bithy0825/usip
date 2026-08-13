add_subdirectory(common)
add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(service)
# add_subdirectory(renderer)  # 待添加
add_subdirectory(ui)
add_subdirectory(app)

# 全项目唯一聚合 PCH:统一落地(须在全部子目录之后)
usip_pch_finalize()
