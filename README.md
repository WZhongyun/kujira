# 鲸鱼娘 Kujira

一只住在桌面上的 Live2D 看板娘，陪你用 Claude Code 写代码。

她会跟着 Claude Code 的工作状态变化：开始时打招呼，读文件时戴上眼镜，改文件时拿起画笔，跑命令时手忙脚乱，等你确认时冒出问号，完成时比耶。头顶的小气泡会告诉你 Claude Code 正在做什么、需要你处理什么；闲下来时她会和你聊两句，摸摸她、点点她也会有反应。

- 透明背景、始终在最前面，不占任务栏；点在透明的地方会直接点到下面的窗口
- 只看不碰：她只接收 Claude Code 的事件，不会干预它的任何操作；她没开的时候 Claude Code 照常工作
- 很轻：占用内存约 80 MB，空闲时几乎不占 CPU
- 表情、动画、台词都能在设置里自己改
- 支持 Windows 10 / 11（64 位）和 macOS（Apple 芯片）

非官方的个人作品，与 Anthropic 及任何 Agent 厂商无关。

## 下载和使用

### 下载

到 [Releases](https://github.com/WZhongyun/Kujira/releases) 页面下载最新版本的压缩包：

| 系统 | 压缩包 |
| --- | --- |
| Windows 10 / 11（64 位） | `Kujira-版本号-windows-x64.zip` |
| macOS（M1 / M2 / M3 / M4） | `Kujira-版本号-macos-arm64.zip` |

压缩包里已经带好了模型，解压就能用，不需要另外下载。

### 第一次使用

1. 把压缩包解压到一个固定的位置（例如 `D:\Kujira`），以后不要随意移动
2. 双击 `Kujira.exe`（macOS 上是 `Kujira`），她会出现在屏幕右下角
3. 右键点她打开设置，进入「Agent 接入」，点「安装 hook」
4. 在 Claude Code 里发一条消息，她就会动起来了

想让她跟着 Claude Code 一起出现、一起离开，可以在「Agent 接入」里打开「跟随 Claude Code 启动」；想开机就看到她，在「常规」里打开开机自启。

### 怎么和她互动

| 操作 | 效果 |
| --- | --- |
| 左键拖动 | 移动她的位置（会自动记住） |
| 左键单击 | 她会有反应，并说一句话 |
| 鼠标停在她身上 | 旁边出现小按钮：设置、安静模式、退出 |
| 单击气泡 | 关掉气泡 |
| 右键 | 打开设置 |

设置里可以调整大小、帧率、气泡显示多少，编辑她说的话（「气泡与台词」），以及每种状态下的表情和动画（「状态映射」，可以直接试播）。

### 常见问题

- **装了 hook，她却没反应**：确认她正在运行，然后在 Claude Code 里发一条新消息。「Agent 接入」页面下方会显示收到了多少个事件；一直是 0 的话，点「更新 hook」再试。
- **把程序挪到别的文件夹以后没反应了**：hook 记录的是程序的位置，挪动后在「Agent 接入」里点一次「更新 hook」。
- **不想用了**：先在「Agent 接入」里点「卸载 hook」，再删除程序文件夹。直接删掉也不会影响 Claude Code 工作。
- **她跑到屏幕外面找不到了**：在「常规」里点「重置到屏幕右下角」。设置打不开时，删除配置文件里的 `windowX`、`windowY` 两项（位置见下文）。
- **背景是黑色方块，不透明**：显卡驱动或系统不支持透明窗口，可以先更新显卡驱动。
- **提示找不到模型**：确认 `models` 文件夹和 `Kujira.exe` 在同一个文件夹里，没有被单独移走。
- **Windows 提示“已保护你的电脑”**：点「更多信息」→「仍要运行」。
- **macOS 提示“无法打开”或“无法验证开发者”**：右键点 `Kujira` →「打开」；如果没有「打开」选项，到「系统设置 → 隐私与安全性」页面底部点「仍要打开」。
- **想恢复默认设置**：退出她，删除配置文件后重新打开，再到「Agent 接入」点一次「更新 hook」。

| 系统 | 配置文件 |
| --- | --- |
| Windows | `%APPDATA%\Kujira\config.json` |
| macOS | `~/Library/Application Support/Kujira/config.json` |

设置的「关于」页面里也能直接打开配置文件夹。遇到其他问题，或者想接入别的 Agent，欢迎联系 [Kujira 作者](https://space.bilibili.com/25308604)。

## 开发者

### 编译

需要自己准备两样东西，都不在仓库里：

- **Live2D Cubism SDK for Native**：到 [Live2D 官网](https://www.live2d.com/sdk/download/native/) 下载，同意其许可协议后放进 `third_party/CubismSdkForNative/`
- **模型**：按 [这个视频](https://www.bilibili.com/video/BV16yYi69EQT/) 获取「DS鲸鱼娘」模型，把模型文件夹放进 `assets/models/`（见 [assets/models/README.md](assets/models/README.md)）

然后按 [docs/BUILD.md](docs/BUILD.md) 编译。Windows 和 macOS 都是同一套命令：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 技术概要

- C++17，基于官方 Live2D Cubism SDK for Native（Core + Framework），GLFW + OpenGL 渲染
- 设置窗口用 Dear ImGui，按需创建、关闭即销毁
- 通过 Claude Code 官方 hooks 接收事件：极简转发程序 `kujira-hook` 把事件发到本机 `127.0.0.1` 上的服务，只观察、不干预。安装方式和安全性见 [docs/agents/claude-code.md](docs/agents/claude-code.md)
- 不同 Agent 各有一个适配器，翻译成统一的事件，方便以后接入更多 Agent

### 状态与动画

| Claude Code 事件 | 状态 | 默认表情和动画 |
| --- | --- | --- |
| SessionStart | 会话开始 | 头顶鲸 + 开心兴奋，小鲸喷水（3 秒） |
| UserPromptSubmit | 收到指令 | 星星眼 + 感叹号（1.5 秒） |
| PreToolUse：Read / Grep / Glob / WebFetch… | 阅读文件 | 圆眼镜 |
| PreToolUse：Edit / Write / MultiEdit | 修改文件 | 画笔 + 点菜手按下 |
| PreToolUse：Bash | 执行命令 | 挤番茄酱蛋包饭（循环） |
| PreToolUse：其他工具、PostToolUse | 思考中 | 吹泡泡糖（循环） |
| Notification（请求权限、等待输入） | 需要关注 | 头顶鲸 + 问号，小鲸一直喷水 |
| Stop | 完成 | 双手比耶 + 开心兴奋 + 冒爱心（4 秒） |
| SessionEnd | 会话结束 | 喵喵手 + 脸红（2.5 秒） |
| 无事件超过 5 分钟 | 睡眠 | 闭眼口水 + 吐魂 |
| 单击她 / 拖动她 | 互动 | 重锤出击 / 晕晕眼 |

默认组合参考了模型作者在 VTube Studio 里的用法：表情像开关一样叠加，动画叠在待机动画上播放。同时开多个会话时，按「需要关注 > 工作中 > 完成 > 空闲」显示最重要的那个。完成时气泡里的回复内容从 Claude Code 保存在本机的对话记录里读取，不会发往任何地方。

### 目录结构

```
src/
  core/        主循环、模型封装、状态机、配置
  events/      统一事件模型与本地 HTTP 服务
  agents/      每个 Agent 一个适配器（事件翻译 + hook 安装器）
  hook/        kujira-hook 转发程序
  platform/    平台层（win / mac / posix）
  ui/          设置窗口、气泡与主题
cmake/         第三方依赖（配置时自动下载）
assets/models/ 模型放这里（不入库）
third_party/   Cubism SDK 放这里（不入库）
```

## 致谢与许可

- 模型「DS鲸鱼娘」作者：[B 站 @氵六青](https://space.bilibili.com/11272072)。模型版权归原作者所有，经作者同意随 Release 一起发布；请遵守模型附带的使用须知，不要提交到本仓库。
- This application contains Live2D Cubism SDK developed by Live2D Inc. SDK 需自行下载并同意其许可协议，不包含在本仓库中。
- 本仓库代码：Apache License 2.0
