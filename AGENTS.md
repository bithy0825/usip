# AGENTS.md

## 通用原则

- **确定性优先**：所有构建、运行步骤必须可复现，禁止依赖未声明的隐式环境。
- **最小权限**：Agent 只操作自己的工作目录和明确授权的路径，不触碰用户个人数据。
- **失败即停**：构建或测试失败时立即停止，不自动跳过错误。

---

## Python 环境规则

### 环境隔离（强制）

- **严禁**使用全局 Python 环境。禁止 `pip` 直接安装到系统解释器。
- **严禁**使用或修改 `D:\python\uv\venvs` 下用户已有的环境（属于用户个人环境，不可污染）。
- Agent 专属环境：
  - 在 `D:\python\uv\venvs` 下检测是否存在名为 `agent` 的环境。
  - 若存在，直接使用：`D:\python\uv\venvs\agent\python.exe`。
  - 若不存在，新建并复用：
    ```
    uv venv D:/python/uv/venvs/agent
    uv pip install --python D:/python/uv/venvs/agent/python.exe <package>
    ```

### 代码质量

- 格式化：使用 `ruff format` 或 `black`。
- 静态检查：使用 `ruff check` 或 `flake8` + `mypy`。
- 类型注解：新代码必须添加类型注解，对公共 API 强制要求。

---

## C++ 规则

### 1. 语言标准与编译器

- **语言标准**：C++23（ISO/IEC 14882:2024）。若编译器不支持完整 C++23，允许回退到 C++20，但需在代码注释中说明原因。
- **最低编译器版本**：
  - GCC >= 14.0
  - Clang >= 18.0
  - MSVC >= 2022 17.10（`_MSC_VER >= 1940`）
- **禁止**使用已弃用特性（如 `std::auto_ptr`、`std::bind` 优先用 lambda 替代等）。

### 2. 现代 C++ 特性清单（优先使用）

| 场景 | 推荐 | 避免 |
|------|------|------|
| 空指针 | `nullptr` | `NULL`, `0` |
| 类型推导 | `auto` | 冗长显式类型 |
| 常量 | `constexpr` / `consteval` | 宏定义 |
| 枚举 | `enum class` | 裸 `enum` |
| 可选值 | `std::optional` | 哨兵值 / 裸指针 |
| 错误处理 | `std::expected` (C++23) | 输出参数 + bool |
| 并发 | `std::jthread`, `std::latch`, `std::barrier` | 裸 `std::thread` + 手动 join |
| 格式化 | `std::format` (C++20/23) | `printf`, `std::ostringstream` |
| 范围 | `std::ranges` | 手写循环 |
| 概念 | `concept` + `requires` | SFINAE（除非必要） |

---

## Agent 工作流规范

1. **开始任务前**：检查 `AGENTS.md` 中对应语言的规则。
2. **修改代码前**：先运行现有测试，确保基线通过。
3. **提交前**：
   - Python：`ruff check . && ruff format . && mypy .`
   - C++：`cmake --build build && ctest --test-dir build && clang-tidy src/**/*.cpp`
4. **禁止行为**：
   - 不经过构建/测试直接声称"已完成"。
   - 修改用户个人配置文件（如 `~/.bashrc`, 注册表等）。
   - 生成无意义的占位代码（如 `// TODO: implement this`）并声称功能已完成。
