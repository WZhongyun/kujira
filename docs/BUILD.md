# 编译说明

## Windows（主力平台）

### 需要准备

- Windows 10 / 11，64 位
- Visual Studio 2022 或更新版本，安装「使用 C++ 的桌面开发」工作负载（包含 MSVC 和 CMake）
- Git（CMake 会用它下载 Dear ImGui 和 cpp-httplib）
- 能访问 GitHub 的网络（首次配置时下载 GLFW、GLEW、ImGui、FreeType、cpp-httplib、nlohmann/json）

### 步骤

1. 克隆仓库

   ```powershell
   git clone https://github.com/WZhongyun/Kujira.git
   cd Kujira
   git checkout claude/project-thread-y5vv76   # 合并前先用这个分支
   ```

2. 放入 Cubism SDK：从 [Live2D 官网](https://www.live2d.com/sdk/download/native/) 下载 Cubism SDK for Native（已验证 5-r.5），解压后把文件夹改名或复制为 `third_party/CubismSdkForNative`，里面应能看到 `Core`、`Framework` 两个文件夹。

3. 放入模型：把模型文件夹（例如 `Kujira-Live2D`）复制到 `assets/models/` 下，见 [assets/models/README.md](../assets/models/README.md)。编译时会用模型文件夹里的 `icon.png` 生成 exe 图标；没有这个文件就用默认图标。

4. 在「Developer PowerShell for VS 2022」里编译：

   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```

   用 VS 2026 时把生成器换成对应版本，或者直接用 Visual Studio 打开仓库文件夹（它会识别 CMakeLists.txt）。

5. 运行 `build\Release\Kujira.exe`。编译后会自动把着色器和 `assets` 复制到 exe 旁边，整个 `build\Release` 文件夹可以挪到别处使用。旁边的 `kujira-hook.exe` 是转发 Claude Code 事件的小程序，要和 `Kujira.exe` 放在一起。

### 常见问题

- **CMake 提示找不到 Cubism SDK**：确认 `third_party/CubismSdkForNative/Core/include/Live2DCubismCore.h` 存在，或用 `-DCUBISM_SDK_DIR=D:\path\to\CubismSdkForNative-5-r.5` 指定路径。
- **链接时报 Live2DCubismCore 相关错误**：本项目使用 `/MD` 运行时，对应 SDK 里的 `Live2DCubismCore_MD.lib`。不要改成 `/MT`。
- **看到黑色方块而不是透明背景**：说明显卡驱动或系统设置不支持 OpenGL 透明窗口，请把显卡型号、驱动版本告诉我们。
- **启动后看不到她**：检查 `%APPDATA%\Kujira\config.json` 里的 `windowX` / `windowY`，删掉这两项会回到屏幕右下角；也可以在设置里点「重置到屏幕右下角」。
- **设置窗口的中文显示成方块**：程序会依次尝试微软雅黑、黑体、宋体，请确认 `C:\Windows\Fonts` 里至少有其中一个。

## Linux（开发调试用）

```bash
sudo apt install build-essential cmake git libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxi-dev libgl1-mesa-dev fonts-noto-cjk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/Kujira
```

需要带合成器的桌面环境才有透明背景。

## macOS（试验中）

目前用 OpenGL 渲染（苹果已弃用但仍可用），计划中的 Metal 版本之后再做。Apple 芯片和 Intel 都支持，CMake 会自动选对应的 Cubism Core 库。

### 需要准备

- macOS 11 或更新
- Xcode 命令行工具：`xcode-select --install`（不需要完整的 Xcode）
- CMake 和 Ninja：装了 [Homebrew](https://brew.sh) 的话 `brew install cmake ninja`

### 步骤

```bash
git clone https://github.com/WZhongyun/Kujira.git
cd Kujira
git checkout claude/project-thread-y5vv76
# 和 Windows 一样放入 SDK 和模型：
#   third_party/CubismSdkForNative/  （里面有 Core、Framework）
#   assets/models/Kujira-Live2D/
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/Kujira
```

- 她不会出现在程序坞里，右键她或用悬浮按钮打开设置，悬浮按钮里的 × 退出。
- 设置保存在 `~/Library/Application Support/Kujira/`，开机自启写在 `~/Library/LaunchAgents/com.kujira.pet.plist`。
- 自己编译的程序不会被“无法验证开发者”拦住；如果复制到别的 Mac 上被拦，右键 → 打开即可。

## 配置与日志位置

| 平台 | 配置文件 |
| --- | --- |
| Windows | `%APPDATA%\Kujira\config.json` |
| macOS | `~/Library/Application Support/Kujira/config.json` |
| Linux | `~/.config/kujira/config.json` |

删除配置文件即可恢复默认设置（hook 的令牌会重新生成，需要在设置里点一次「更新 hook」）。
