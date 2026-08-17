# AGENTS.md

## Python 环境规则（必须遵守）

- **禁止**使用全局 Python 环境（禁止 `pip` 直接装到全局、禁止给系统解释器装包）。
- **禁止**使用或修改 `D:\python\uv\venvs` 下用户已有的环境（它们是用户本人的环境）。
- 需要 Python 环境时：首先在`D:\python\uv\venvs` 下检测是否存在名为 agent 的环境，如果有则使用，否则用 `uv` 在 `D:\python\uv\venvs` 下**新建一个 agent 专属环境**（命名为 "angent" ），所有依赖装在这个环境里，之后复用它。
- 示例：`uv venv D:/python/uv/venvs/agent`，然后用 `uv pip install --python D:/python/uv/venvs/agent/python.exe <包>` 安装依赖。
