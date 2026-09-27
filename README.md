# 鲸鱼娘 Kujira

常驻桌面的 Live2D 看板娘，实时反映编程 Agent 的工作状态：会话开始时打招呼，读文件时戴上眼镜，改文件时拿起画笔，跑命令时流汗，等你确认时冒出感叹号，完成时比耶。

- C++ 原生，基于官方 Live2D Cubism SDK for Native（Core + Framework）
- 透明、无边框、置顶，不占任务栏；点在她身上可以拖动和互动，点在透明区域会穿透到下面的窗口
- 通过 Claude Code 官方 hooks 接收事件，只观察、不干预；鲸鱼娘没运行时 Claude Code 照常工作
- 头顶对话气泡：Agent 在做什么、等你处理什么、完成时回复的开头；空闲时闲聊，被摸、被点、被拖时有台词，全部可以自定义
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
| 左键单击 | 她会害羞一下，并说一句台词 |
| 鼠标停在她身上 | 旁边出现小按钮：设置、安静模式、退出 |
| 单击气泡 | 关掉气泡 |
| 右键 | 打开设置 |

## 气泡与台词

| 时机 | 气泡内容 |
| --- | --- |
| Agent 读文件、改文件、运行命令 | 「在改 App.cpp」「正在运行：安装依赖」这类简短说明 |
| Agent 等你授权或输入 | Claude Code 的提示原文，直到你处理完才消失 |
| Agent 完成 | 最后一段回复的开头几句 |
| 空闲时（约每 15 分钟） | 按早上、中午、下午、晚上、深夜挑一句闲聊 |
| 鼠标停留、单击、拖动 | 互动台词 |

所有台词都在设置「气泡与台词」里按分组编辑，每行一句，随机挑选；也可以直接改配置目录里的 `dialogue.json`。气泡可以设为「全部显示 / 只显示重要的 / 关闭」；安静模式下只说 Agent 相关的话。完成时的回复内容从 Claude Code 保存在本机的对话记录里读取，不会发往任何地方。

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
