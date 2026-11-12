# 第三方组件

search 使用动态链接的 Qt Core、Gui、Widgets、Network、Sql，开发和测试使用 Qt Concurrent / Test。当前本机 SDK 为 Qt 6.8.3、MSVC 2022 x64；Qt SQL 的 SQLite 驱动提供本地数据库。

Qt 开源模块使用其适用的 LGPLv3 / GPL 或其他许可选项；本项目所用 Qt 模块按 LGPLv3 动态链接路线构建。Qt 与各第三方组件的版权归各自权利人所有。此说明不替代组件的完整许可文本。

- Qt 许可说明：https://doc.qt.io/qt-6/licensing.html
- Qt Base 6.8.3 对应源码：https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz
- Qt Base 第三方组件说明：https://doc.qt.io/qt-6/qtcore-attribution.html
- SQLite 为公共领域软件：https://www.sqlite.org/copyright.html
- MSVC 运行库 DLL 从 Visual Studio 的 Redistributable Code 目录通过 CMake `InstallRequiredSystemLibraries` 复制，采用应用本地部署；使用微软适用的再分发许可。正式分发以 Visual Studio 许可及 [可再分发代码清单](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution) 为准。便携包不包含 `vc_redist.x64.exe`；Windows 10/11 已提供 UCRT。
- 拼音数据使用 [mozillazg/pinyin-data](https://github.com/mozillazg/pinyin-data)，MIT 许可，固定提交 `fe29ad32bdf352b31a02b9936e537f575432e9a6`（数据版本 0.16.0）。许可及版权见 `licenses/pinyin-data-MIT.txt`。`scripts/generate_pinyin.py` 校验来源 SHA-256 并生成 `src/core/pinyin_data.inc`；正常构建直接使用已提交的数据，不需要 Python 或联网。数据的读音顺序被保留，声调被去除，ü 使用 v 表示。

本地预编译 Qt SDK 未经 search 修改源码，Qt DLL 保持可替换；替换须使用兼容的 Qt / MSVC ABI。发布目录保留 LGPLv3 和 GPLv3 文本。面向外部正式分发时，还需按实际 Qt 构建的 SBOM 收集随 DLL 链接的组件声明，提供该构建对应的完整源码或合规的源码提供方式；vcpkg 构建应包含其补丁与构建配方。当前 `dist` 是本机开发部署产物，未制作对外发布的安装包。
