#include "windows_integration.h"

#include "core/search.h"
#include "system_entries.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <dwmapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace searchapp
{
namespace
{

class ComApartment
{
public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComApartment()
    {
        if (SUCCEEDED(result_))
            CoUninitialize();
    }
    bool valid() const
    {
        return SUCCEEDED(result_);
    }

private:
    HRESULT result_;
};

QString shellName(IShellItem* item, SIGDN type)
{
    PWSTR raw = nullptr;
    if (FAILED(item->GetDisplayName(type, &raw)))
        return {};
    const QString value = QString::fromWCharArray(raw);
    CoTaskMemFree(raw);
    return value;
}

bool shellOpen(const QString& target, const QString& parameters = {})
{
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(target.utf16()),
                      parameters.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(parameters.utf16()), nullptr,
                      SW_SHOWNORMAL));
    return result > 32;
}

} // namespace

QString pathIdentity(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toCaseFolded();
}

QStringList startMenuDirectories()
{
    QStringList directories;
    for (const auto* folder : {&FOLDERID_Programs, &FOLDERID_CommonPrograms}) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*folder, 0, nullptr, &path))) {
            directories.push_back(QDir::fromNativeSeparators(QString::fromWCharArray(path)));
            CoTaskMemFree(path);
        }
    }
    directories.removeDuplicates();
    return directories;
}

namespace
{

QString shortcutTarget(const QString& path)
{
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    Microsoft::WRL::ComPtr<IPersistFile> file;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
        FAILED(link.As(&file)) || FAILED(file->Load(reinterpret_cast<LPCWSTR>(path.utf16()), STGM_READ)))
        return {};
    std::array<wchar_t, 32768> raw{};
    if (SUCCEEDED(link->GetPath(raw.data(), static_cast<int>(raw.size()), nullptr, SLGP_RAWPATH)) && raw[0]) {
        std::array<wchar_t, 32768> expanded{};
        const DWORD length =
            ExpandEnvironmentStringsW(raw.data(), expanded.data(), static_cast<DWORD>(expanded.size()));
        if (length > 0 && length <= expanded.size())
            return QDir::fromNativeSeparators(QString::fromWCharArray(expanded.data()));
    }
    Microsoft::WRL::ComPtr<IPropertyStore> properties;
    PROPVARIANT value{};
    QString target;
    if (SUCCEEDED(link.As(&properties)) && SUCCEEDED(properties->GetValue(PKEY_AppUserModel_ID, &value)) &&
        value.vt == VT_LPWSTR && value.pwszVal)
        target = QStringLiteral("shell:AppsFolder\\") + QString::fromWCharArray(value.pwszVal);
    PropVariantClear(&value);
    return target;
}

bool runnableTarget(const QString& target)
{
    if (systemEntry(target))
        return true;
    if (target.startsWith(QStringLiteral("shell:AppsFolder\\"), Qt::CaseInsensitive))
        return true;
    const QString extension = QFileInfo(target).suffix().toCaseFolded();
    return QStringList{QStringLiteral("exe"), QStringLiteral("com"), QStringLiteral("bat"),
                       QStringLiteral("cmd"), QStringLiteral("msc"), QStringLiteral("appref-ms")}
        .contains(extension);
}

} // namespace

Catalog discoverStartMenuApplications(const QStringList& directories,
                                      const std::shared_ptr<std::atomic_bool>& canceled)
{
    const ComApartment apartment;
    Catalog items;
    QSet<QString> identities;
    for (const auto& directory : directories) {
        QDirIterator iterator(directory, {QStringLiteral("*.lnk"), QStringLiteral("*.appref-ms")},
                              QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            if (canceled->load())
                return {};
            const QString path = iterator.next();
            const QString id = pathIdentity(path);
            if (identities.contains(id))
                continue;
            const bool clickOnce = path.endsWith(QStringLiteral(".appref-ms"), Qt::CaseInsensitive);
            if (!clickOnce && !runnableTarget(shortcutTarget(path)))
                continue;
            identities.insert(id);
            items.push_back({id, QFileInfo(path).completeBaseName(), path});
        }
    }
    return items;
}

Catalog discoverApplications(const std::shared_ptr<std::atomic_bool>& canceled)
{
    auto items = discoverStartMenuApplications(startMenuDirectories(), canceled);
    if (canceled->load())
        return {};
    items += discoverSystemEntries();
    QSet<QString> identities;
    for (const auto& item : items)
        identities.insert(item.id);

    // AppsFolder includes packaged applications which have no directly runnable exe.
    const ComApartment apartment;
    if (!apartment.valid())
        return items;
    Microsoft::WRL::ComPtr<IShellItem> folderItem;
    if (FAILED(SHCreateItemFromParsingName(L"shell:AppsFolder", nullptr, IID_PPV_ARGS(&folderItem))))
        return items;
    Microsoft::WRL::ComPtr<IEnumShellItems> enumerator;
    if (FAILED(folderItem->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&enumerator))))
        return items;
    Microsoft::WRL::ComPtr<IShellItem> item;
    while (!canceled->load() && enumerator->Next(1, &item, nullptr) == S_OK) {
        const QString name = shellName(item.Get(), SIGDN_NORMALDISPLAY);
        const QString parsingName = shellName(item.Get(), SIGDN_PARENTRELATIVEPARSING);
        // Win32 AppsFolder entries usually duplicate Start Menu shortcuts. Retain
        // package identities (AUMID contains '!'); launch shortcut variants intact.
        if (!name.isEmpty() && parsingName.contains(u'!')) {
            const QString target = parsingName.startsWith(QStringLiteral("shell:"))
                                       ? parsingName
                                       : QStringLiteral("shell:AppsFolder\\") + parsingName;
            const QString id = target.toCaseFolded();
            if (!identities.contains(id) && !systemEntry(target)) {
                identities.insert(id);
                items.push_back({id, name, target});
            }
        }
        item.Reset();
    }
    return items;
}
// Only verified Windows locations or exact Microsoft package identities receive
// translations. A third-party shortcut named "Calculator" does not qualify.
QStringList systemApplicationAliases(const QString& target)
{
    if (const auto* entry = systemEntry(target))
        return QStringList{entry->name} + entry->aliases;
    struct SystemApplication {
        const char* identity;
        const char* chinese;
        const char* english;
    };
    static constexpr SystemApplication packages[] = {
        {"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App", "计算器", "Calculator"},
        {"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App", "记事本", "Notepad"},
        {"Microsoft.Paint_8wekyb3d8bbwe!App", "画图", "Paint"},
        {"Microsoft.ScreenSketch_8wekyb3d8bbwe!App", "截图工具", "Snipping Tool"},
        {"Microsoft.Windows.Photos_8wekyb3d8bbwe!App", "照片", "Photos"},
        {"Microsoft.WindowsCamera_8wekyb3d8bbwe!App", "相机", "Camera"},
        {"Microsoft.WindowsAlarms_8wekyb3d8bbwe!App", "时钟 闹钟", "Clock Alarms"},
        {"Microsoft.WindowsSoundRecorder_8wekyb3d8bbwe!App", "录音机 语音录音机",
         "Sound Recorder Voice Recorder"},
        {"Microsoft.WindowsStore_8wekyb3d8bbwe!App", "微软商店 应用商店", "Microsoft Store"},
        {"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App", "终端", "Windows Terminal"},
        {"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel", "设置",
         "Settings"}};
    const QString prefix = QStringLiteral("shell:AppsFolder\\");
    if (target.startsWith(prefix, Qt::CaseInsensitive)) {
        const auto identity = target.mid(prefix.size());
        for (const auto& entry : packages)
            if (identity.compare(QLatin1String(entry.identity), Qt::CaseInsensitive) == 0)
                return {QString::fromUtf8(entry.chinese), QString::fromLatin1(entry.english)};
        return {};
    }
    const QString windows = qEnvironmentVariable("SystemRoot");
    const QString path = pathIdentity(target);
    if (windows.isEmpty() || !path.startsWith(pathIdentity(windows) + u'/'))
        return {};
    static constexpr SystemApplication executables[] = {
        {"calc.exe", "计算器", "Calculator"},
        {"notepad.exe", "记事本", "Notepad"},
        {"mspaint.exe", "画图", "Paint"},
        {"explorer.exe", "文件资源管理器 资源管理器", "File Explorer"},
        {"cmd.exe", "命令提示符", "Command Prompt"},
        {"powershell.exe", "命令行", "Windows PowerShell"},
        {"control.exe", "控制面板", "Control Panel"},
        {"taskmgr.exe", "任务管理器", "Task Manager"},
        {"snippingtool.exe", "截图工具", "Snipping Tool"},
        {"mstsc.exe", "远程桌面连接", "Remote Desktop Connection"},
        {"regedit.exe", "注册表编辑器", "Registry Editor"},
        {"msinfo32.exe", "系统信息", "System Information"},
        {"cleanmgr.exe", "磁盘清理", "Disk Cleanup"},
        {"charmap.exe", "字符映射表", "Character Map"},
        {"osk.exe", "屏幕键盘", "On-Screen Keyboard"},
        {"magnify.exe", "放大镜", "Magnifier"},
        {"narrator.exe", "讲述人", "Narrator"},
        {"wordpad.exe", "写字板", "WordPad"},
        {"wmplayer.exe", "媒体播放器", "Windows Media Player"},
        {"services.msc", "服务", "Services"},
        {"eventvwr.msc", "事件查看器", "Event Viewer"},
        {"compmgmt.msc", "计算机管理", "Computer Management"},
        {"devmgmt.msc", "设备管理器", "Device Manager"},
        {"diskmgmt.msc", "磁盘管理", "Disk Management"},
        {"taskschd.msc", "任务计划程序", "Task Scheduler"}};
    const QString filename = QFileInfo(path).fileName();
    for (const auto& entry : executables)
        if (filename == QLatin1String(entry.identity))
            return {QString::fromUtf8(entry.chinese), QString::fromLatin1(entry.english)};
    return {};
}

void prepareApplications(Catalog& items)
{
    const ComApartment apartment;
    items.erase(std::remove_if(items.begin(), items.end(),
                               [](CatalogItem& item) {
                                   QString target = item.target;
                                   if (target.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive))
                                       target = shortcutTarget(target);
                                   if (!runnableTarget(target))
                                       return true;
                                   item.searchAliases = systemApplicationAliases(target);
                                   const auto* entry = systemEntry(target);
                                   item.kind = entry ? entry->kind : ItemKind::Application;
                                   prepareSearch(item);
                                   return false;
                               }),
                items.end());
}

bool openTarget(const CatalogItem& item)
{
    return shellOpen(QDir::toNativeSeparators(item.target));
}

QImage applicationIcon(const QString& target, int size)
{
    const ComApartment apartment;
    if (!apartment.valid())
        return {};
    const auto* entry = systemEntry(target);
    const QString iconTarget =
        entry && entry->kind == ItemKind::Settings
            ? qEnvironmentVariable("SystemRoot") + QStringLiteral("/ImmersiveControlPanel/SystemSettings.exe")
            : target;
    const QString path = QDir::toNativeSeparators(iconTarget);
    Microsoft::WRL::ComPtr<IShellItemImageFactory> factory;
    if (SUCCEEDED(SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr,
                                              IID_PPV_ARGS(&factory)))) {
        HBITMAP bitmap = nullptr;
        if (SUCCEEDED(factory->GetImage({size, size}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bitmap))) {
            const auto cleanup = qScopeGuard([&] { DeleteObject(bitmap); });
            auto image = QImage::fromHBITMAP(bitmap);
            if (!image.isNull())
                return image;
        }
    }
    if (entry && entry->kind == ItemKind::ControlPanel)
        return applicationIcon(qEnvironmentVariable("SystemRoot") + QStringLiteral("/System32/control.exe"),
                               size);
    // Shell's icon handler respects the shortcut's custom icon and target.
    SHFILEINFOW info{};
    if (!SHGetFileInfoW(reinterpret_cast<LPCWSTR>(path.utf16()), 0, &info, sizeof(info),
                        SHGFI_ICON | SHGFI_LARGEICON))
        return {};
    const auto cleanup = qScopeGuard([&] { DestroyIcon(info.hIcon); });
    return QImage::fromHICON(info.hIcon);
}

bool canRevealTarget(const CatalogItem& item)
{
    return !item.target.startsWith(QStringLiteral("shell:"), Qt::CaseInsensitive) &&
           !item.target.startsWith(QStringLiteral("ms-settings:"), Qt::CaseInsensitive);
}

bool revealTarget(const CatalogItem& item)
{
    if (!canRevealTarget(item))
        return false;
    const ComApartment apartment;
    if (!apartment.valid())
        return false;
    PIDLIST_ABSOLUTE list = nullptr;
    const QString path = QDir::toNativeSeparators(item.target);
    if (FAILED(SHParseDisplayName(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr, &list, 0, nullptr)))
        return false;
    const HRESULT result = SHOpenFolderAndSelectItems(list, 0, nullptr, 0);
    CoTaskMemFree(list);
    return SUCCEEDED(result);
}

bool setLaunchAtLogin(bool enabled, QString* error)
{
    QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                       QSettings::NativeFormat);
    if (enabled) {
        const auto executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        registry.setValue(QStringLiteral("search"), QStringLiteral("\"%1\" --background").arg(executable));
    } else
        registry.remove(QStringLiteral("search"));
    // Replace the previous application's startup entry when settings are saved.
    registry.remove(QStringLiteral("KeySeek"));
    registry.sync();
    if (registry.status() == QSettings::NoError)
        return true;
    if (error)
        *error = QStringLiteral("无法修改当前用户的开机启动设置。");
    return false;
}

QRect activationWorkArea()
{
    POINT point{};
    RECT foreground{};
    const HWND window = GetForegroundWindow();
    if (window && GetWindowRect(window, &foreground)) {
        point.x = (foreground.left + foreground.right) / 2;
        point.y = (foreground.top + foreground.bottom) / 2;
    } else
        GetCursorPos(&point);
    const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
        for (auto* screen : QGuiApplication::screens()) {
            if (screen->name().compare(QString::fromWCharArray(info.szDevice), Qt::CaseInsensitive) == 0)
                return screen->availableGeometry();
        }
    }
    return QGuiApplication::primaryScreen()->availableGeometry();
}

void foregroundWindow(quintptr windowId)
{
    const HWND window = reinterpret_cast<HWND>(windowId);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
}

void applyWindowTheme(quintptr windowId, bool dark)
{
    const BOOL enabled = dark;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(windowId), 20, &enabled, sizeof(enabled));
    const DWORD corners = 2; // DWMWCP_ROUND; unsupported systems simply ignore it.
    DwmSetWindowAttribute(reinterpret_cast<HWND>(windowId), 33, &corners, sizeof(corners));
}

GlobalHotkey::GlobalHotkey(QObject* parent) : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkey::~GlobalHotkey()
{
    if (activeId_)
        UnregisterHotKey(nullptr, activeId_);
    QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool GlobalHotkey::setShortcut(const QKeySequence& shortcut, QString* error)
{
    if (shortcut == shortcut_ && activeId_)
        return true;
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };
    if (shortcut.count() != 1)
        return fail(QStringLiteral("请选择一个带修饰键的快捷键组合。"));
    const auto combination = shortcut[0];
    const auto modifiers = combination.keyboardModifiers();
    UINT flags = MOD_NOREPEAT;
    if (modifiers.testFlag(Qt::AltModifier))
        flags |= MOD_ALT;
    if (modifiers.testFlag(Qt::ControlModifier))
        flags |= MOD_CONTROL;
    if (modifiers.testFlag(Qt::ShiftModifier))
        flags |= MOD_SHIFT;
    if (modifiers.testFlag(Qt::MetaModifier))
        flags |= MOD_WIN;
    if (!(flags & (MOD_ALT | MOD_CONTROL | MOD_WIN)))
        return fail(QStringLiteral("快捷键至少包含 Ctrl、Alt 或 Windows 键。"));
    const int key = combination.key();
    UINT virtualKey = 0;
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        virtualKey = static_cast<UINT>(key);
    else if (key >= Qt::Key_0 && key <= Qt::Key_9)
        virtualKey = static_cast<UINT>(key);
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24 && key != Qt::Key_F12)
        virtualKey = VK_F1 + key - Qt::Key_F1;
    else if (key == Qt::Key_Space)
        virtualKey = VK_SPACE;
    else if (key == Qt::Key_Return)
        virtualKey = VK_RETURN;
    else
        return fail(QStringLiteral("请使用字母、数字、空格、回车或功能键（F12 除外）。"));
    const int nextId = activeId_ == 0x4b51 ? 0x4b52 : 0x4b51;
    if (!RegisterHotKey(nullptr, nextId, flags, virtualKey))
        return fail(QStringLiteral("该快捷键已被系统或其他程序占用，请选择其他组合。"));
    if (activeId_)
        UnregisterHotKey(nullptr, activeId_);
    activeId_ = nextId;
    shortcut_ = shortcut;
    return true;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray&, void* message, qintptr* result)
{
    const auto* event = static_cast<MSG*>(message);
    if (!activeId_ || event->message != WM_HOTKEY || static_cast<int>(event->wParam) != activeId_)
        return false;
    // Qt passes nullptr for thread messages (windows_dispatcher_MSG).
    if (result)
        *result = 0;
    emit activated();
    return true;
}

} // namespace searchapp
