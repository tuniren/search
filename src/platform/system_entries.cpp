#include "system_entries.h"

#include <QDir>
#include <QFileInfo>
#include <iterator>

namespace searchapp
{
namespace
{
const QString settingsApplication = QStringLiteral(
    "shell:AppsFolder\\windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel");

const QVector<SystemEntry>& entries()
{
    struct Definition {
        const char* key;
        const char* name;
        const char* target;
        const char* aliases;
        ItemKind kind = ItemKind::Settings;
    };
    static const QVector<SystemEntry> items = [] {
        // Documented Windows 10/11 settings URIs and Control Panel shell identities.
        // Entries only open a page; they never execute user input or change its settings.
        static constexpr Definition definitions[] = {
            {"settings", "设置", "ms-settings:", "Settings;Windows Settings;系统设置;Windows 设置"},
            {"network", "网络和 Internet", "ms-settings:network",
             "Network;Network and Internet;网络;网络设置;联网;互联网"},
            {"wifi", "Wi-Fi 设置", "ms-settings:network-wifi", "WiFi;Wi-Fi;WLAN;无线网络;无线网;无线连接"},
            {"ethernet", "以太网", "ms-settings:network-ethernet", "Ethernet;有线网络;有线连接"},
            {"vpn", "VPN 设置", "ms-settings:network-vpn", "VPN;虚拟专用网络"},
            {"proxy", "网络代理", "ms-settings:network-proxy", "Proxy;代理;代理服务器"},
            {"hotspot", "移动热点", "ms-settings:network-mobilehotspot", "Mobile Hotspot;热点;网络共享"},
            {"airplane", "飞行模式", "ms-settings:network-airplanemode", "Airplane Mode"},
            {"bluetooth", "蓝牙和设备", "ms-settings:bluetooth", "Bluetooth;蓝牙;设备设置"},
            {"display", "显示设置", "ms-settings:display", "Display;显示;屏幕;分辨率;缩放;亮度"},
            {"sound", "声音设置", "ms-settings:sound", "Sound;声音;音量;扬声器;麦克风;音频"},
            {"notifications", "通知和操作", "ms-settings:notifications", "Notifications;通知;通知设置"},
            {"power", "电源和睡眠", "ms-settings:powersleep", "Power and Sleep;睡眠;电源设置;休眠"},
            {"battery", "电池设置", "ms-settings:batterysaver", "Battery;Battery Saver;电池;节电模式"},
            {"storage", "存储设置", "ms-settings:storagesense", "Storage;存储;存储空间;磁盘空间;临时文件"},
            {"about", "关于此电脑", "ms-settings:about", "About;PC Info;系统信息;电脑信息;设备规格;系统版本"},
            {"apps", "应用和功能", "ms-settings:appsfeatures",
             "Apps and Features;安装的应用;应用卸载;卸载软件"},
            {"defaults", "默认应用", "ms-settings:defaultapps", "Default Apps;默认程序;文件关联"},
            {"startup", "启动应用", "ms-settings:startupapps", "Startup Apps;启动项;开机启动;自启动"},
            {"accounts", "账户信息", "ms-settings:yourinfo", "Accounts;Your Info;账户;帐户;用户;个人信息"},
            {"signin", "登录选项", "ms-settings:signinoptions", "Sign-in Options;密码;PIN;Windows Hello"},
            {"datetime", "日期和时间", "ms-settings:dateandtime", "Date and Time;日期;时间;时区;校时"},
            {"language", "语言设置", "ms-settings:regionlanguage", "Language;语言;输入法;输入语言"},
            {"region", "区域设置", "ms-settings:regionformatting", "Region;区域;国家;地区"},
            {"personalization", "个性化", "ms-settings:personalization", "Personalization;外观;桌面美化"},
            {"background", "桌面背景", "ms-settings:personalization-background",
             "Background;Wallpaper;背景;壁纸"},
            {"colors", "颜色设置", "ms-settings:colors", "Colors;颜色;深色模式;浅色模式"},
            {"taskbar", "任务栏设置", "ms-settings:taskbar", "Taskbar;任务栏"},
            {"update", "Windows 更新", "ms-settings:windowsupdate", "Windows Update;系统更新;更新系统"},
            {"security", "Windows 安全中心", "ms-settings:windowsdefender",
             "Windows Security;Windows Defender;安全中心;病毒防护"},
            {"privacy", "隐私设置", "ms-settings:privacy", "Privacy;隐私;应用权限"},
            {"recovery", "恢复选项", "ms-settings:recovery", "Recovery;恢复;重置电脑;重置此电脑;高级启动"},
            {"activation", "Windows 激活", "ms-settings:activation", "Activation;激活;产品密钥"},
            {"accessibility", "辅助功能", "ms-settings:easeofaccess",
             "Accessibility;Ease of Access;轻松使用;无障碍"},
            {"printers", "打印机和扫描仪", "ms-settings:printers", "Printers and Scanners;打印机;扫描仪"},
            {"mouse", "鼠标设置", "ms-settings:mousetouchpad", "Mouse;鼠标;指针"},
            {"control-panel", "控制面板", "shell:::{26EE0668-A00A-44D7-9371-BEB064C98683}",
             "Control Panel;控制面板主页", ItemKind::ControlPanel},
            {"connections", "网络连接", "shell:::{7007ACC7-3202-11D1-AAD2-00805FC1270E}",
             "Network Connections;网络适配器;更改适配器选项;网卡;ncpa.cpl", ItemKind::ControlPanel},
            {"network-center", "网络和共享中心", "shell:::{8E908FC9-BECC-40F6-915B-F4CA0E70D03D}",
             "Network and Sharing Center;网络共享中心;共享中心", ItemKind::ControlPanel},
            {"programs", "程序和功能", "shell:::{7B81BE6A-CE2B-4676-A29E-EB907A5126C5}",
             "Programs and Features;卸载程序;卸载或更改程序;appwiz.cpl", ItemKind::ControlPanel},
            {"power-options", "电源选项", "shell:::{025A5937-A6BE-4686-A844-36FE4BEC8B6D}",
             "Power Options;电源计划;高性能;powercfg.cpl", ItemKind::ControlPanel},
            {"firewall", "Windows 防火墙", "shell:::{4026492F-2F69-46B8-B9BF-5654FC07E423}",
             "Windows Firewall;防火墙;firewall.cpl", ItemKind::ControlPanel},
            {"task-manager", "任务管理器", "system32:Taskmgr.exe", "Task Manager;任务管理;进程",
             ItemKind::SystemTool},
            {"device-manager", "设备管理器", "system32:devmgmt.msc", "Device Manager;硬件设备;devmgmt",
             ItemKind::SystemTool},
            {"services", "服务", "system32:services.msc", "Services;系统服务;服务管理", ItemKind::SystemTool},
            {"disk-management", "磁盘管理", "system32:diskmgmt.msc",
             "Disk Management;磁盘分区;分区管理;diskmgmt", ItemKind::SystemTool},
            {"computer-management", "计算机管理", "system32:compmgmt.msc", "Computer Management;compmgmt",
             ItemKind::SystemTool},
            {"event-viewer", "事件查看器", "system32:eventvwr.msc", "Event Viewer;事件日志;系统日志;eventvwr",
             ItemKind::SystemTool},
            {"task-scheduler", "任务计划程序", "system32:taskschd.msc",
             "Task Scheduler;计划任务;定时任务;taskschd", ItemKind::SystemTool},
            {"system-information", "系统信息", "system32:msinfo32.exe",
             "System Information;系统摘要;msinfo32", ItemKind::SystemTool},
            {"resource-monitor", "资源监视器", "system32:resmon.exe", "Resource Monitor;性能监视;resmon",
             ItemKind::SystemTool},
            {"advanced-system", "高级系统设置", "system32:SystemPropertiesAdvanced.exe",
             "Advanced System Settings;环境变量;系统属性;性能选项", ItemKind::SystemTool},
            {"windows-features", "Windows 功能", "system32:OptionalFeatures.exe",
             "Windows Features;启用或关闭 Windows 功能;可选功能", ItemKind::SystemTool},
        };
        QVector<SystemEntry> result;
        result.reserve(std::size(definitions));
        const auto windows = QDir::fromNativeSeparators(qEnvironmentVariable("SystemRoot"));
        for (const auto& definition : definitions) {
            auto target = QString::fromLatin1(definition.target);
            if (target.startsWith(QStringLiteral("system32:"))) {
                if (windows.isEmpty())
                    continue;
                target = windows + QStringLiteral("/System32/") + target.mid(9);
            }
            const auto id = target == QStringLiteral("ms-settings:")
                                ? settingsApplication.toCaseFolded()
                                : QStringLiteral("system:") + QString::fromLatin1(definition.key);
            result.push_back({id, QString::fromUtf8(definition.name), target,
                              QString::fromUtf8(definition.aliases).split(u';', Qt::SkipEmptyParts),
                              definition.kind});
        }
        return result;
    }();
    return items;
}
} // namespace

const SystemEntry* systemEntry(const QString& target)
{
    const auto canonical = target.compare(settingsApplication, Qt::CaseInsensitive) == 0
                               ? QStringLiteral("ms-settings:")
                               : QDir::fromNativeSeparators(target);
    for (const auto& entry : entries())
        if (canonical.compare(entry.target, Qt::CaseInsensitive) == 0)
            return &entry;
    return nullptr;
}

Catalog discoverSystemEntries()
{
    Catalog items;
    for (const auto& entry : entries()) {
        if (entry.kind == ItemKind::SystemTool && !QFileInfo::exists(entry.target))
            continue;
        CatalogItem item{entry.id, entry.name, entry.target};
        item.kind = entry.kind;
        items.push_back(std::move(item));
    }
    return items;
}

} // namespace searchapp
