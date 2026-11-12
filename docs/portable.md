# search 便携版

解压整个目录后运行 `search.exe`。需要 Windows 10/11 x64。

提供两种 ZIP，程序功能相同，均已包含 Qt DLL 和插件，无需另装 Qt：

| 文件名后缀 | VC++ 运行库 | 适用情况 |
| --- | --- | --- |
| `with-runtime.zip` | 包内附带必要的 MSVC 运行库 DLL | 推荐；解压即用 |
| `no-runtime.zip` | 不附带 MSVC 运行库 DLL | 电脑已安装 Microsoft Visual C++ v14 x64 运行库 |

精简版若提示缺少 `VCRUNTIME140.dll`、`MSVCP140.dll` 等，请改用 `with-runtime` 版，或从微软官网下载并安装最新的 x64 运行库：
https://aka.ms/vc14/vc_redist.x64.exe

默认 `Alt+Space` 呼出搜索，输入名称、全拼或拼音首字母；方向键选择，Enter 打开。`Ctrl+,` 打开设置，托盘菜单退出。每次成功打开程序记录一次热度，未输入时显示常用程序；设置中可开启登录启动。

配置和历史保存在 `%LOCALAPPDATA%/search`，两种版本共用同一份数据。切换版本时请完整解压到新的目录，避免旧 DLL 残留。

从旧版升级时会沿用已有的 `%LOCALAPPDATA%/KeySeek` 数据目录，保留设置及使用记录。

第三方许可见 `licenses` 目录。
