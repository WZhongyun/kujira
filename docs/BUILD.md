# 编译说明

仓库只有一个分支 `main`，克隆后直接编译即可。

两样东西不在仓库里，需要自己放：

| 内容 | 放到哪里 | 说明 |
| --- | --- | --- |
| Cubism SDK for Native（已验证 5-r.5） | `third_party/CubismSdkForNative/` | 从 [Live2D 官网](https://www.live2d.com/sdk/download/native/) 下载，解压后改名，里面应直接看到 `Core`、`Framework` 两个文件夹 |
| 模型文件夹（例如 `Kujira-Live2D`） | `assets/models/Kujira-Live2D/` | 见 [assets/models/README.md](../assets/models/README.md)；模型里的 `icon.png` 会用作程序图标 |

首次配置时 CMake 会从 GitHub 下载 GLFW、GLEW、Dear ImGui、FreeType、cpp-httplib、nlohmann/json，需要能访问 GitHub，并且装了 Git。

## Windows（64 位）

### 环境

- Windows 10 / 11，64 位
- Visual Studio Build Tools 2026，勾选「使用 C++ 的桌面开发」（MSVC 14.51）
- CMake 4.x（最低 3.20）和 Ninja，在 PATH 里能找到
- Git

一定要在 **64 位（x64）** 的开发者环境里编译，否则会编出 32 位程序。打开方式二选一：

- 开始菜单里的「x64 Native Tools Command Prompt for VS」（cmd）
- 普通 PowerShell 里执行：

  ```powershell
  $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
  & "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
  ```

进入后可以先检查一下：`cl` 的第一行应显示「用于 x64」，`cmake --version`、`ninja --version` 都有输出。

### 编译

```powershell
git clone https://github.com/WZhongyun/Kujira.git
cd Kujira
# 放入 SDK 和模型（见上表）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

编译完成后运行 `build\Kujira.exe`。

- 编译时会把着色器复制到 exe 旁边的 `FrameworkShaders\`，把 `assets\models\` 复制到 exe 旁边的 `models\`。
- `kujira-hook.exe` 是转发 Claude Code 事件的小程序，必须和 `Kujira.exe` 放在同一个文件夹。
- 想把程序挪到别处用，复制这几样即可：`Kujira.exe`、`kujira-hook.exe`、`FrameworkShaders\`、`models\`。`build\` 里的其他文件（`_deps`、`CMakeFiles` 等）是编译中间产物，不需要。
- 更新代码后只需 `git pull` 再执行 `cmake --build build`；换了编译器或改了生成器时，删掉 `build` 文件夹重新配置。

### 常见问题

- **CMake 提示找不到 Cubism SDK**：确认 `third_party\CubismSdkForNative\Core\include\Live2DCubismCore.h` 存在，或用 `-DCUBISM_SDK_DIR=D:\path\to\CubismSdkForNative-5-r.5` 指定路径。
- **链接时报 Live2DCubismCore 相关错误**：多半是在 32 位环境里编译的，换成 x64 的开发者环境，删掉 `build` 重新配置。本项目使用 `/MD` 运行时，对应 SDK 里的 `Live2DCubismCore_MD.lib`，不要改成 `/MT`。
- **看到黑色方块而不是透明背景**：显卡驱动或系统设置不支持 OpenGL 透明窗口，请记下显卡型号和驱动版本。
- **启动后看不到她**：在设置里点「重置到屏幕右下角」，或删掉 `%APPDATA%\Kujira\config.json` 里的 `windowX` / `windowY`。
- **设置窗口的中文显示成方块**：程序会依次尝试微软雅黑、黑体、宋体，请确认 `C:\Windows\Fonts` 里至少有其中一个。

## macOS（Apple 芯片）

用 OpenGL 渲染（苹果已弃用但仍可用）。已在 Mac mini M1 上编译运行；Intel Mac 理论上也能用，CMake 会自动选对应的 Cubism Core 库。

### 环境

- macOS 11 或更新
- Xcode 命令行工具：`xcode-select --install`（不需要完整的 Xcode）
- CMake 和 Ninja：`brew install cmake ninja`（需要 [Homebrew](https://brew.sh)）
- Git

### 编译

```bash
git clone https://github.com/WZhongyun/Kujira.git
cd Kujira
# 放入 SDK 和模型（见上表）
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/Kujira
```

- 她不会出现在程序坞里。右键她或用悬浮按钮打开设置，悬浮按钮里的 × 退出。
- `kujira-hook` 同样要和 `Kujira` 放在同一个文件夹。
- 自己编译的程序不会被“无法验证开发者”拦住；复制到别的 Mac 上被拦时，右键 → 打开即可。

## Linux（仅开发调试）

```bash
sudo apt install build-essential cmake ninja-build git libx11-dev libxrandr-dev libxinerama-dev \
  libxcursor-dev libxi-dev libgl1-mesa-dev fonts-noto-cjk
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/Kujira
```

需要带合成器的桌面环境才有透明背景。

## 配置文件位置

| 平台 | 配置文件 | 其他 |
| --- | --- | --- |
| Windows | `%APPDATA%\Kujira\config.json` | |
| macOS | `~/Library/Application Support/Kujira/config.json` | 开机自启：`~/Library/LaunchAgents/com.kujira.pet.plist` |
| Linux | `~/.config/kujira/config.json` | |

删除配置文件即可恢复默认设置（hook 的令牌会重新生成，需要在设置里点一次「更新 hook」）。
