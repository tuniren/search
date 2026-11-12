# 使用与构建

search 支持 Windows 10/11 x64，使用 C++20 + Qt 6 Widgets，界面采用命令式 C++ 编写。

## 使用

从 [Releases](https://github.com/tuniren/search/releases) 下载 ZIP，完整解压后运行 `search.exe`。

- `with-runtime`：附带 VC++ 运行库，推荐解压即用。
- `no-runtime`：需已安装 Microsoft Visual C++ v14 x64 运行库。

`Alt+Space` 呼出，输入名称搜索，`↑` / `↓` 选择，`Enter` 打开；`Shift+Enter` 打开所在位置，`Ctrl+,` 打开设置。右键结果可固定或设置别名。

中文程序名支持原名、全拼和首字母，例如“微信 / weixin / wx”。常见 Windows 系统程序额外支持中英文别名，例如“计算器 / Calculator / jisuanqi / jsq”。第三方程序按开始菜单中的原名搜索，不自动翻译，可手动设置个人别名。文档、文件夹和未加入开始菜单的独立 EXE 不纳入搜索。

设置、固定项与热度保存在 `%LOCALAPPDATA%/search`。已有 `%LOCALAPPDATA%/KeySeek` 目录会继续沿用。关闭窗口后仍在托盘运行，通过托盘菜单退出。命令行参数 `--settings` 打开设置，`--background` 在托盘启动。

## 构建

需要 Visual Studio 2022 C++ 桌面工具、Windows SDK、CMake 3.25+ 和 Ninja；打包与更新程序测试需要 Python 3.10+。脚本自动加载 MSVC 开发者环境。

推荐 vcpkg manifest，已固定 registry baseline：

```powershell
$env:VCPKG_ROOT = 'C:\tools\vcpkg'
.\scripts\build.ps1 -Deploy -Package
```

也可使用 MSVC x64 Qt 6.8+ SDK；当前发布使用 Qt 6.8.3：

```powershell
.\scripts\build.ps1 -QtRoot 'C:\Qt\6.8.3\msvc2022_64' -Deploy -Package
```

本机 `.tools/Qt/6.8.3/msvc2022_64` 存在且未设置 `VCPKG_ROOT` 时，脚本会自动选择它。Debug 使用 `-Configuration Debug` 和 vcpkg preset；发行包仅支持 Release。已验证 Qt SDK 路线；vcpkg baseline 对应的 Qt 版本可能不同。

构建默认运行四组测试：搜索与数据存储、界面、更新协议与下载、原生更新程序的替换及恢复。`-SkipTests` 可跳过测试。

## 部署与打包

`-Deploy` 从干净目录生成 `dist/search.exe`，旧目录保存在构建目录中。程序运行时请先从托盘退出，再部署，以免文件占用。

`-Package` 调用 Python 脚本生成两个 ZIP 和 `update.json`。也可对已有 Release 构建单独打包：

```powershell
python scripts/package.py --build-dir build/qt-sdk-release
```

默认输出在构建目录的 `packages` 下，支持 `--output-dir`。两个包都包含 Qt DLL 和插件，仅 VC++ 运行库不同。打包脚本使用 Python 标准库，校验 ZIP CRC 和包 SHA-256；用户电脑无需 Python。

`-Package` 会对两个 ZIP 校验文件布局，并在清除 Qt 环境变量、仅保留 Windows 系统目录的 PATH 下运行解压后的程序，防止依赖开发机上的 Qt。可单独复查：

```powershell
python scripts/verify_packages.py --packages-dir build/qt-sdk-release/packages
```

更多细节见 [更新与发布](updates.md) 和 [第三方组件](third-party.md)。
