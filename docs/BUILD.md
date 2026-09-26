# 编译说明

## Windows（主力平台）

### 需要准备

- Windows 10 / 11，64 位
- Visual Studio 2022 或更新版本，安装「使用 C++ 的桌面开发」工作负载（包含 MSVC 和 CMake）
- Git（CMake 会用它下载 Dear ImGui 和 cpp-httplib）
- 能访问 GitHub 的网络（首次配置时下载 GLFW、GLEW、ImGui、cpp-httplib、nlohmann/json）

### 步骤

1. 克隆仓库

   ```powershell
   git clone https://github.com/WZhongyun/Kujira.git
   cd Kujira
   git checkout claude/project-thread-y5vv76   # 合并前先用这个分支
   ```

2. 放入 Cubism SDK：从 [Live2D 官网](https://www.live2d.com/sdk/download/native/) 下载 Cubism SDK for Native（已验证 5-r.5），解压后把文件夹改名或复制为 `third_party/CubismSdkForNative`，里面应能看到 `Core`、`Framework` 两个文件夹。

3. 放入模型：把模型文件夹（例如 `Kujira-Live2D`）复制到 `assets/models/` 下，见 [assets/models/README.md](../assets/models/README.md)。

4. 在「Developer PowerShell for VS 2022」里编译：

   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   ```

   用 VS 2026 时把生成器换成对应版本，或者直接用 Visual Studio 打开仓库文件夹（它会识别 CMakeLists.txt）。

5. 运行 `build\Release\Kujira.exe`。编译后会自动把着色器和 `assets` 复制到 exe 旁边，整个 `build\Release` 文件夹可以挪到别处使用。

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

## macOS

尚未移植（计划用 Metal，里程碑 M3）。CMake 里保留了 OpenGL 的临时路径，但没有测试过。

## 配置与日志位置

| 平台 | 配置文件 |
| --- | --- |
| Windows | `%APPDATA%\Kujira\config.json` |
| Linux | `~/.config/kujira/config.json` |

删除配置文件即可恢复默认设置（hook 的令牌会重新生成，需要在设置里点一次「更新 hook」）。
