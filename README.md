# 鲸鱼娘 Kujira

常驻桌面的 Live2D 看板娘，实时反映编程 Agent 的工作状态：会话开始时打招呼，读文件时戴上眼镜，改文件时拿起画笔，跑命令时流汗，等你确认时冒出感叹号，完成时比耶。

- C++ 原生，基于官方 Live2D Cubism SDK for Native（Core + Framework）
- 透明、无边框、置顶，不占任务栏；点在她身上可以拖动和互动，点在透明区域会穿透到下面的窗口
- 通过 Claude Code 官方 hooks 接收事件，只观察、不干预；鲸鱼娘没运行时 Claude Code 照常工作
- 设置窗口（Dear ImGui）按需创建、关闭即销毁
- 非官方个人工具，与 Anthropic 及任何 Agent 厂商无关

目前状态：Windows 为主力平台（首次实机测试中），Linux 可用于开发调试，macOS（Metal）尚未移植。

## 快速开始

1. 按 [docs/BUILD.md](docs/BUILD.md) 编译（需要自行下载 Cubism SDK）
2. 把模型文件夹放进 `assets/models/`（见 [assets/models/README.md](assets/models/README.md)）
3. 运行 `Kujira.exe`，她会出现在屏幕右下角
4. 右键点她打开设置，在「Agent 接入」里点「安装 hook」
5. 在 Claude Code 里发一条消息，看她的反应

## 操作

| 操作 | 效果 |
| --- | --- |
| 左键拖动 | 移动位置（自动记住） |
| 左键单击 | 她会害羞一下 |
| 右键 | 打开设置 |

## 状态与动作

| Claude Code 事件 | 状态 | 默认表情 |
| --- | --- | --- |
| SessionStart | 会话开始 | 开心兴奋（3 秒） |
| UserPromptSubmit | 收到指令 | 星星眼（1.5 秒） |
| PreToolUse：Read / Grep / Glob / WebFetch… | 阅读文件 | 圆眼镜 |
| PreToolUse：Edit / Write / MultiEdit | 修改文件 | 画笔 |
| PreToolUse：Bash | 执行命令 | 流汗 |
| PreToolUse：其他工具、PostToolUse | 思考中 | 无 |
| Notification（请求权限、等待输入） | 需要关注 | 感叹号 + 鲸鱼喷水 |
| Stop | 完成 | 双手比耶（4 秒） |
| SessionEnd | 会话结束 | 爱心眼（2 秒） |
| 无事件超过 5 分钟 | 睡眠 | 闭眼口水 |

每一项都能在设置的「动作映射」里改，并可以「试播」。同时开多个会话时，按「需要关注 > 工作中 > 完成 > 空闲」显示最重要的那个。

Hook 的安装方式和安全性见 [docs/agents/claude-code.md](docs/agents/claude-code.md)。

## 目录结构

```
src/
  core/        主循环、模型封装、状态机、配置
  events/      统一事件模型与本地 HTTP 服务
  agents/      每个 Agent 一个适配器（事件翻译 + hook 安装器）
  platform/    平台层（win / posix）
  ui/          设置窗口与主题
cmake/         第三方依赖（配置时自动下载）
assets/models/ 模型放这里（不入库）
third_party/   Cubism SDK 放这里（不入库）
```

## 许可

- 本仓库代码：Apache License 2.0
- This application contains Live2D Cubism SDK developed by Live2D Inc. SDK 需自行下载并同意其许可协议，不包含在本仓库中。
- 模型版权归原作者所有，请遵守模型附带的使用须知，不要提交到本仓库。
