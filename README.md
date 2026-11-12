# search

Windows 快捷启动与搜索工具，使用 **C++20 + Qt 6 Widgets**。界面采用命令式 C++ 编写，包含搜索面板和独立设置窗口。

GitHub 仓库：https://github.com/tuniren/search

## 运行

本机已构建的程序：`dist/search.exe`。保持整个 `dist` 目录完整，Qt DLL 和插件需要随程序一起运行。

- 默认 `Alt+Space` 呼出或收起；快捷键冲突时可在设置中修改。
- 输入程序名称、中文全拼或拼音首字母，`↑` / `↓` 选择，`Enter` 打开。
- 每次成功打开程序，热度加 1；打开后清空搜索词，下次唤起默认显示常用 App。固定项优先，其余按打开次数、最近使用时间排序；记录重启后保留。首次使用可先搜索，设置中可清除使用历史。
- `Shift+Enter` 打开所在位置；右键结果可复制路径、固定或设置别名。
- `Ctrl+,` 或右上角按钮打开设置，`Esc` 收起搜索。
- 从托盘菜单可以搜索、打开设置、刷新程序列表或退出。
- `search.exe --settings` 打开设置；`search.exe --background` 仅在托盘启动。

自动收录当前用户和所有用户开始菜单中的程序快捷方式，以及 Windows 注册的商店应用。快捷方式保留原始启动参数；文档快捷方式、目录和未加入开始菜单的独立 EXE 不纳入搜索。设置可修改主题、快捷键、失焦行为和开机启动。

中文程序名支持原名、全拼和首字母，例如“微信 / weixin / wx”。常见 Windows 系统程序额外支持中英文别名，例如“计算器 / Calculator / jisuanqi / jsq”。第三方程序按开始菜单中的原名搜索，不自动翻译名称；可手动设置个人别名。英文搜索不区分大小写。

配置、SQLite 程序缓存和使用记录存放于 `%LOCALAPPDATA%/search`。查询和拼音匹配均在本地完成。升级时自动移除旧文件索引，保留程序的固定项、别名与使用记录。

旧版升级会继续使用已有的 `%LOCALAPPDATA%/KeySeek` 数据目录，保留设置与常用记录。软件、程序文件和发布包均使用新名称 `search`。应用使用橙色放大镜图标，Windows EXE 内嵌多尺寸 ICO；图标可通过 `python scripts/generate_icon.py` 重新生成。

## 构建

需要 Visual Studio 2022 C++ 桌面工具、Windows SDK、CMake 3.25+ 和 Ninja。脚本会自动定位 MSVC 开发者环境。

推荐使用 **vcpkg manifest**，仓库已固定 registry baseline 并按需声明 Qt 模块：

```powershell
$env:VCPKG_ROOT = 'C:\tools\vcpkg'
.\scripts\build.ps1 -Deploy
```

Debug 构建使用 `.\scripts\build.ps1 -Configuration Debug`。Qt 首次源码构建可能较久，建议启用 vcpkg 二进制缓存。

也支持已安装的 **MSVC x64 Qt SDK**：

```powershell
.\scripts\build.ps1 -QtRoot 'C:\Qt\6.8.3\msvc2022_64' -Deploy
```

本次已验证的环境为 MSVC 19.44、Windows SDK 10.0.22621、Qt 6.8.3、CMake 4.4.4、Ninja 1.13.2。Qt SDK 位于本机 `.tools/Qt/6.8.3/msvc2022_64`，无 `VCPKG_ROOT` 时脚本可自动使用它。vcpkg 源码构建 preset 已提供，尚未在本机完成验证；SDK preset 和 vcpkg baseline 对应的 Qt 版本可能不同。

构建脚本默认运行核心和桌面集成测试；`-Deploy` 从干净目录生成 `dist` 并保留旧目录备份，避免旧插件残留。`-Package` 调用 Python 打包脚本生成两种 ZIP，可与 `-Deploy` 一起使用：

```powershell
.\scripts\build.ps1 -Deploy -Package
```

也可在已有 Release 构建上单独打包（Python 3.10+，仅使用标准库）：

```powershell
python scripts/package.py --build-dir build/qt-sdk-release
```

使用 `--output-dir` 可指定输出目录。脚本从 CMake 的安装组件暂存文件，生成 ZIP 并校验 CRC；不会从旧 `dist` 目录复制残留文件。

每次 `-Package` 在对应构建目录的 `packages` 下生成两个 ZIP：

| 压缩包 | 内容与使用条件 |
| --- | --- |
| `search-0.1.0-windows-x64-with-runtime.zip` | 推荐，附带必要的 MSVC 运行库 DLL，解压即用 |
| `search-0.1.0-windows-x64-no-runtime.zip` | 不带 MSVC 运行库 DLL，要求系统已安装 Microsoft Visual C++ v14 x64 运行库 |

两种包功能相同，都包含 Qt DLL 和插件，无需安装 Qt。Windows 10/11 自带 UCRT。精简版缺少运行库时可切换完整版，或安装[微软官方 x64 运行库](https://aka.ms/vc14/vc_redist.x64.exe)。两种包均附带使用说明，请保留整个解压目录，不要只复制 EXE。`-Deploy` 生成的本机 `dist` 默认包含运行库 DLL。

部署仅保留 Windows 窗口、SQLite 和 ICO 插件。网络模块用于本机单实例通信；不打包 TLS、网络状态、额外数据库、JPEG/GIF、TUIO 或现代样式插件。图标仍由 Windows Shell 读取，Fusion / Windows 样式由 Qt 内置提供。

## 代码结构

| 目录 | 职责 |
| --- | --- |
| `src/core` | 查询规范化、排序、拼音匹配和结果类型 |
| `src/storage` | 设置文件、SQLite 索引及个性化记录 |
| `src/platform` | 开始菜单程序发现、系统程序别名、快捷键、Shell 动作及开始菜单监听 |
| `src/app` | 生命周期、后台扫描、查询取消和模块组装 |
| `src/ui` | 命令式 QWidget 界面、列表模型、自定义 delegate 和主题 |
| `tests` | 拼音及名称检索、旧缓存迁移、开始菜单发现、输入法、快捷键和快速切换验证 |

采用 Qt 父子所有权和 RAII 管理资源。搜索、扫描和 Shell 动作在各自的有界工作池执行；界面只在主线程更新。查询带有代次与取消标记，过期结果不会覆盖新输入。未使用 Designer、`.ui` 或 QML，也未引入额外的响应式框架。

格式遵循 `.clang-format`，编码规则见 `.editorconfig`。详细设计及后续计划见 [设计文档](docs/design.md)。

## 当前边界

使用 SQLite 持久化和内存快照搜索；开始菜单变化合并后后台刷新，并每十分钟重新枚举以补齐商店应用变化。拼音字表固定在源码中，支持多音字的不同读音；不依赖在线服务，不提供按词义消歧。系统程序中英文别名由明确的 Windows 路径及包身份映射，尚未覆盖所有可选 Windows 组件。

结果显示 Windows 提供的程序图标，支持开始菜单快捷方式的自定义图标和商店应用。图标在后台加载并缓存，无法读取时显示默认图标。匹配高亮、插件及安装器留待后续。尚未完成混合 DPI、读屏和干净虚拟机的完整验收。

Qt 采用动态链接；依赖许可与源码位置见 [第三方组件说明](docs/third-party.md)。
