#include "settings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace searchapp
{

SettingsStore::SettingsStore(QString directory) : directory_(std::move(directory))
{
    if (directory_.isEmpty()) {
        const auto dataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        directory_ = dataRoot + QStringLiteral("/search");
        // Reuse an existing installation's settings, pins and launch history.
        // New installations use the new name; existing data need not be moved.
        const auto legacy = dataRoot + QStringLiteral("/KeySeek");
        if (!QDir(directory_).exists() && QDir(legacy).exists())
            directory_ = legacy;
    }
    QDir().mkpath(directory_);
}

Settings SettingsStore::load() const
{
    QSettings file(directory_ + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    Settings value;
    value.hotkey = file.value(QStringLiteral("general/hotkey"), value.hotkey).toString();
    const int theme = file.value(QStringLiteral("appearance/theme"), 0).toInt();
    value.theme = theme >= 0 && theme <= 2 ? static_cast<Theme>(theme) : Theme::System;
    value.hideOnFocusLoss = file.value(QStringLiteral("general/hideOnFocusLoss"), true).toBool();
    value.launchAtLogin = file.value(QStringLiteral("general/launchAtLogin"), false).toBool();
    value.checkForUpdates = file.value(QStringLiteral("updates/automaticCheck"), true).toBool();
    return value;
}

bool SettingsStore::save(const Settings& value, QString* error) const
{
    QSettings file(directory_ + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    file.setValue(QStringLiteral("general/hotkey"), value.hotkey);
    file.setValue(QStringLiteral("appearance/theme"), static_cast<int>(value.theme));
    file.remove(QStringLiteral("index")); // Retire file-index settings from older versions.
    file.remove(QStringLiteral("search"));
    file.setValue(QStringLiteral("general/hideOnFocusLoss"), value.hideOnFocusLoss);
    file.setValue(QStringLiteral("general/launchAtLogin"), value.launchAtLogin);
    file.setValue(QStringLiteral("updates/automaticCheck"), value.checkForUpdates);
    file.sync();
    if (file.status() == QSettings::NoError)
        return true;
    if (error)
        *error = QStringLiteral("无法保存设置，请检查用户数据目录的写入权限。");
    return false;
}

} // namespace searchapp
