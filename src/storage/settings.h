#pragma once

#include <QString>

namespace searchapp
{

enum class Theme { System, Light, Dark };

struct Settings {
    QString hotkey = QStringLiteral("Alt+Space");
    Theme theme = Theme::System;
    bool hideOnFocusLoss = true;
    bool launchAtLogin = false;
};

class SettingsStore
{
public:
    explicit SettingsStore(QString directory = {});
    Settings load() const;
    bool save(const Settings& settings, QString* error = nullptr) const;
    QString directory() const
    {
        return directory_;
    }

private:
    QString directory_;
};

} // namespace searchapp
