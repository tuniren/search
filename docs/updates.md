# 更新与发布

search 从公开仓库 `tuniren/search` 的 GitHub Releases 获取更新，不需要 GitHub 账号或访问令牌。

## 使用

设置 → 关于 → 检查更新 → 下载更新 → 重启更新。下载前可查看发布说明，下载过程中可取消；断网、GitHub 限流或校验失败时可重试，也可打开发布页手动下载。自动检查默认开启，每天最多一次，可在常规设置中关闭；不会自动下载或退出程序。

更新沿用当前 `with-runtime` / `no-runtime` 类型，只接受正式版和 Windows x64 包。更新不会移动设置、固定项和使用热度。0.1.0 尚无更新模块，首次升级请完整解压 0.2.0 到新目录，之后可使用内置更新。

HTTPS 使用系统 Schannel。程序限制更新地址为 GitHub 及其附件域名，下载时检查清单大小和 SHA-256，独立更新程序再次校验，并验证 ZIP 的 CRC、目录、文件类型及安装清单。SHA-256 用于完整性校验；当前未提供独立的发布签名或 Windows 代码签名。

## 安装与恢复

更新程序为不依赖 Qt 的 C++/Win32 EXE，静态链接 VC++ 运行库和 miniz 解压代码。主程序将其复制到本机更新缓存，然后正常退出；更新程序等待退出后执行：

1. 校验更新包，将所有文件解压到程序目录下的 `.search-update/new`。
2. 根据 `installed-files.txt` 将旧版本管理的文件移入 `.search-update/backup`，保留其他个人文件；如新增文件与个人文件重名则终止。
3. 安装新版本，启动 `search.exe`，等待其在初始化和事件循环启动后确认就绪。
4. 确认后提交更新并删除备份；替换或启动失败则恢复原文件，尝试重新启动原版本。

文件操作前会写入并刷新恢复日志。意外断电或更新程序被终止时，备份留在程序目录下；先退出 search，然后运行：

```powershell
& 'C:\apps\search\.search-update\recover.exe' --recover 'C:\apps\search'
```

恢复结束后重新启动程序。若恢复程序本身仍占用目录，剩余的事务文件会在下次更新时清理。只在有写权限的普通目录中进行内置更新；受保护目录可手动下载并迁移。包含目录链接或符号链接的安装路径需要改为普通目录。

下载缓存和错误日志位于用户数据目录的 `updates/download-*`。七天前的缓存会在后续启动时清理；失败事务的恢复备份位于程序目录，不依赖下载缓存。

## 发布

版本号以 `CMakeLists.txt` 的 `project(... VERSION ...)` 为准，程序、设置页面、Windows 资源和打包元数据使用同一来源；同步修改 `vcpkg.json` 的 `version-semver`。

```powershell
.\scripts\build.ps1 -Deploy -Package
```

Python 打包脚本输出两个完整 ZIP 和 `update.json`。ZIP 内含发行类型标记、安装文件清单和许可证，更新清单包含 schema、版本、架构、发行类型、文件名、字节数及 SHA-256。用户电脑不需要 Python。

推送与版本号一致的 `vX.Y.Z` 标签后，`.github/workflows/release.yml` 在 Windows 上构建、运行测试并打包，再创建正式 Release，上传两个 ZIP 和 `update.json`。推送 main 或手动运行 workflow 仅构建并保存 Actions 附件。首次运行需仓库启用 GitHub Actions；发布 job 使用仓库自带的 `GITHUB_TOKEN`，无需在程序中保存任何凭据。
