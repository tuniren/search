#pragma once

#include "catalog_service.h"
#include "platform/windows_integration.h"
#include "storage/settings.h"
#include "ui/search_window.h"
#include "ui/settings_window.h"

#include <QFutureWatcher>
#include <QSystemTrayIcon>
#include <QThreadPool>
#include <optional>

namespace searchapp
{

class Application final : public QObject
{
    Q_OBJECT
public:
    explicit Application(QString dataDirectory = {}, QObject* parent = nullptr);
    ~Application() override;
    void start(bool background);
    void showSearch();
    void showSettings();

private:
    void requestSearch(const QString& query);
    void startPendingSearch();
    void saveSettings(const Settings& settings);
    void execute(const CatalogItem& item, bool reveal);
    void reportError(const QString& message);

    struct QueryRequest {
        QString text;
        quint64 generation;
        std::shared_ptr<const Catalog> catalog;
    };

    SettingsStore settingsStore_;
    Settings settings_;
    UpdateService updates_;
    CatalogService catalog_;
    GlobalHotkey hotkey_;
    SearchWindow search_;
    SettingsWindow settingsWindow_;
    QSystemTrayIcon tray_;
    QThreadPool queryPool_;
    QFutureWatcher<Results> queryWatcher_;
    QThreadPool actionPool_;
    QFutureWatcher<bool> actionWatcher_;
    std::shared_ptr<std::atomic_bool> queryCanceled_;
    std::optional<QueryRequest> pendingQuery_;
    quint64 generation_ = 0;
    quint64 runningGeneration_ = 0;
    QString runningText_;
    QString resultsText_;
    CatalogItem actionItem_;
    bool actionReveal_ = false;
    bool queryRunning_ = false;
    bool actionRunning_ = false;
};

} // namespace searchapp
