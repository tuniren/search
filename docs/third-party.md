# 第三方组件

search 原创代码采用 0BSD，许可见根目录 `LICENSE`（发行包中为 `licenses/search-LICENSE.txt`）。以下第三方代码、数据和二进制组件分别使用其原有许可。

search 使用动态链接的 Qt Core、Gui、Widgets、Network、Sql，开发和测试使用 Qt Concurrent / Test。发布使用 Qt 6.8.3、MSVC 2022 x64；Qt SQL 的 SQLite 驱动提供本地数据库。

Qt 开源模块使用其适用的 LGPLv3 / GPL 或其他许可选项；本项目所用 Qt 模块按 LGPLv3 动态链接路线构建。Qt 与各第三方组件的版权归各自权利人所有。此说明不替代组件的完整许可文本。

- Qt 许可说明：https://doc.qt.io/qt-6/licensing.html
- Qt Base 6.8.3 对应源码：https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz
- Qt Base 第三方组件说明：https://doc.qt.io/qt-6/qtcore-attribution.html
- SQLite 为公共领域软件：https://www.sqlite.org/copyright.html
- 独立更新程序静态链接 miniz 3.0.2 的 ZIP 解压部分，MIT 许可。源码与来源校验值位于 `third_party/miniz`，发行包附带 `licenses/miniz-LICENSE.txt`。HTTPS 使用 Windows Schannel，不附带 OpenSSL。
- MSVC 运行库 DLL 从 Visual Studio 的 Redistributable Code 目录通过 CMake `InstallRequiredSystemLibraries` 复制，采用应用本地部署；使用微软适用的再分发许可。正式分发以 Visual Studio 许可及 [可再分发代码清单](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution) 为准。便携包不包含 `vc_redist.x64.exe`；Windows 10/11 已提供 UCRT。
- 拼音数据使用 [mozillazg/pinyin-data](https://github.com/mozillazg/pinyin-data)，MIT 许可，固定提交 `fe29ad32bdf352b31a02b9936e537f575432e9a6`（数据版本 0.16.0）。许可及版权见 `licenses/pinyin-data-MIT.txt`。`scripts/generate_pinyin.py` 校验来源 SHA-256 并生成 `src/core/pinyin_data.inc`；正常构建直接使用已提交的数据，不需要 Python 或联网。数据的读音顺序被保留，声调被去除，ü 使用 v 表示。

预编译 Qt SDK 未经 search 修改源码，Qt DLL 保持可替换；替换须使用兼容的 Qt / MSVC ABI。发行包保留 LGPLv3 和 GPLv3 文本；对应 Qt Base 完整源码及第三方组件声明可从上述链接获取。使用 vcpkg 自行发布时，应同时提供其实际版本、补丁与构建配方。使用本软件的许可不限制用户按照适用许可证修改、替换或调试 Qt 库。
