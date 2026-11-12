#include "application.h"

#include "core/search.h"
#include "ui/theme.h"

#include <QApplication>
#include <QMenu>
#include <QTimer>
#include <QtConcurrentRun>
#include <Windows.h>
#include <objbase.h>

namespace searchapp
{

Application::Application(QString dataDirectory, QObject* parent)
    : QObject(parent), settingsStore_(std::move(dataDirectory)), settings_(settingsStore_.load()),
      updates_(settingsStore_.directory(), this),
      catalog_(settingsStore_.directory() + QStringLiteral("/catalog.sqlite")), hotkey_(this), tray_(this)
{
    applyTheme(*qApp, settings_.theme);
    search_.setHideOnFocusLoss(settings_.hideOnFocusLoss);
    settingsWindow_.setSettings(settings_);
    queryPool_.setMaxThreadCount(1);
    actionPool_.setMaxThreadCount(1);
    tray_.setIcon(glyphIcon(Glyph::Search));
    tray_.setToolTip(QStringLiteral("search"));
    auto* menu = new QMenu(&search_);
    menu->addAction(QStringLiteral("搜索"), this, &Application::showSearch);
    menu->addAction(QStringLiteral("设置"), this, &Application::showSettings);
    menu->addSeparator();
    menu->addAction(QStringLiteral("刷新搜索列表"), &catalog_, &CatalogService::refresh);
    menu->addAction(QStringLiteral("检查更新"), this, [this] {
        showSettings();
        settingsWindow_.showUpdates();
        updates_.check();
    });
    menu->addSeparator();
    menu->addAction(QStringLiteral("退出 search"), qApp, &QApplication::quit);
    tray_.setContextMenu(menu);
    connect(&tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            showSearch();
    });
    connect(&hotkey_, &GlobalHotkey::activated, this, [this] {
        if (search_.isVisible() && search_.isActiveWindow())
            search_.hide();
        else
            showSearch();
    });
    connect(&search_, &SearchWindow::queryChanged, this, &Application::requestSearch);
    connect(&search_, &SearchWindow::settingsRequested, this, &Application::showSettings);
    connect(&search_, &SearchWindow::actionRequested, this, &Application::execute);
    connect(&search_, &SearchWindow::personalizeRequested, this,
            [this](const CatalogItem& item, bool pinned, const QString& alias) {
                try {
                    catalog_.personalize(item.id, pinned, alias);
                } catch (const std::exception& error) {
                    reportError(QString::fromUtf8(error.what()));
                }
            });
    connect(&settingsWindow_, &SettingsWindow::saveRequested, this, &Application::saveSettings);
    connect(&settingsWindow_, &SettingsWindow::refreshRequested, &catalog_, &CatalogService::refresh);
    connect(&settingsWindow_, &SettingsWindow::checkUpdateRequested, &updates_, [this] { updates_.check(); });
    connect(&settingsWindow_, &SettingsWindow::downloadUpdateRequested, &updates_, &UpdateService::download);
    connect(&settingsWindow_, &SettingsWindow::cancelUpdateRequested, &updates_, &UpdateService::cancel);
    connect(&settingsWindow_, &SettingsWindow::installUpdateRequested, &updates_, &UpdateService::install);
    connect(&updates_, &UpdateService::changed, &settingsWindow_, &SettingsWindow::setUpdateStatus);
    connect(&updates_, &UpdateService::changed, this, [this](const UpdateStatus& status) {
        if (status.state == UpdateState::Available && !settingsWindow_.isVisible())
            tray_.showMessage(QStringLiteral("search 更新"),
                              status.message + QStringLiteral(" 可在设置 → 关于中下载。"),
                              QSystemTrayIcon::Information);
    });
    connect(&updates_, &UpdateService::restartRequested, qApp, &QCoreApplication::quit);
    auto* updateTimer = new QTimer(this);
    updateTimer->setInterval(24 * 60 * 60 * 1000);
    connect(updateTimer, &QTimer::timeout, this, [this] {
        if (settings_.checkForUpdates)
            updates_.check(false);
    });
    updateTimer->start();
    connect(&settingsWindow_, &SettingsWindow::clearHistoryRequested, this, [this] {
        try {
            catalog_.clearHistory();
        } catch (const std::exception& error) {
            settingsWindow_.setError(QString::fromUtf8(error.what()));
        }
    });
    connect(&catalog_, &CatalogService::updated, this, [this] { requestSearch(search_.query()); });
    connect(&catalog_, &CatalogService::statusChanged, &search_, &SearchWindow::setStatus);
    connect(&catalog_, &CatalogService::statusChanged, &settingsWindow_, &SettingsWindow::setStatus);
    connect(&queryWatcher_, &QFutureWatcher<Results>::finished, this, [this] {
        queryRunning_ = false;
        if (runningGeneration_ == generation_ && search_.query() == runningText_ && !search_.composing()) {
            resultsText_ = runningText_;
            search_.setResults(queryWatcher_.result());
        }
        startPendingSearch();
    });
    connect(&actionWatcher_, &QFutureWatcher<bool>::finished, this, [this] {
        actionRunning_ = false;
        if (!actionWatcher_.result()) {
            showSearch();
            reportError(QStringLiteral("无法打开目标，它可能已被移动、删除或卸载。"));
        } else if (!actionReveal_) {
            search_.clearQuery();
            try {
                catalog_.recordLaunch(actionItem_.id);
            } catch (const std::exception& error) {
                reportError(QString::fromUtf8(error.what()));
            }
        }
    });
}

Application::~Application()
{
    if (queryCanceled_)
        queryCanceled_->store(true);
    queryPool_.waitForDone();
    actionPool_.waitForDone();
    tray_.hide();
}

void Application::start(bool background)
{
    tray_.show();
    QString error;
    if (settings_.launchAtLogin && !setLaunchAtLogin(true, &error))
        reportError(error);
    if (!hotkey_.setShortcut(QKeySequence::fromString(settings_.hotkey, QKeySequence::PortableText),
                             &error)) {
        showSettings();
        settingsWindow_.setError(error);
    }
    search_.setHotkeyLabel(settings_.hotkey);
    QTimer::singleShot(0, &catalog_, &CatalogService::start);
    QTimer::singleShot(10000, this, [this] {
        if (settings_.checkForUpdates)
            updates_.check(false);
    });
    if (!background)
        showSearch();
}

void Application::showSearch()
{
    search_.summon();
}

void Application::showSettings()
{
    search_.hide();
    settingsWindow_.setSettings(settings_);
    settingsWindow_.show();
    settingsWindow_.raise();
    settingsWindow_.activateWindow();
    foregroundWindow(settingsWindow_.winId());
    applyWindowTheme(settingsWindow_.winId(), isDarkTheme());
}

void Application::requestSearch(const QString& query)
{
    ++generation_;
    if (queryCanceled_)
        queryCanceled_->store(true);
    pendingQuery_ = QueryRequest{query, generation_, catalog_.snapshot()};
    startPendingSearch();
}

void Application::startPendingSearch()
{
    if (queryRunning_ || !pendingQuery_)
        return;
    auto request = std::move(*pendingQuery_);
    pendingQuery_.reset();
    runningGeneration_ = request.generation;
    runningText_ = request.text;
    queryCanceled_ = std::make_shared<std::atomic_bool>(false);
    queryRunning_ = true;
    queryWatcher_.setFuture(
        QtConcurrent::run(&queryPool_, [request = std::move(request), canceled = queryCanceled_] {
            return search(*request.catalog, request.text, canceled);
        }));
}

void Application::saveSettings(const Settings& value)
{
    QString error;
    if (!hotkey_.setShortcut(QKeySequence::fromString(value.hotkey, QKeySequence::PortableText), &error)) {
        settingsWindow_.setError(error);
        return;
    }
    if (!setLaunchAtLogin(value.launchAtLogin, &error) || !settingsStore_.save(value, &error)) {
        QString ignored;
        hotkey_.setShortcut(QKeySequence::fromString(settings_.hotkey, QKeySequence::PortableText), &ignored);
        setLaunchAtLogin(settings_.launchAtLogin, &ignored);
        settingsWindow_.setError(error);
        return;
    }
    settings_ = value;
    applyTheme(*qApp, value.theme);
    search_.setHideOnFocusLoss(value.hideOnFocusLoss);
    search_.setHotkeyLabel(value.hotkey);
    settingsWindow_.setSettings(value);
    settingsWindow_.showSaved();
    applyWindowTheme(settingsWindow_.winId(), isDarkTheme());
    requestSearch(search_.query());
}

void Application::execute(const CatalogItem& item, bool reveal)
{
    if (resultsText_ != search_.query() || search_.composing()) {
        requestSearch(search_.query());
        return;
    }
    if (actionRunning_)
        return;
    search_.hide();
    actionItem_ = item;
    actionReveal_ = reveal;
    actionRunning_ = true;
    actionWatcher_.setFuture(QtConcurrent::run(&actionPool_, [item, reveal] {
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool opened = reveal ? revealTarget(item) : openTarget(item);
        if (SUCCEEDED(apartment))
            CoUninitialize();
        return opened;
    }));
}

void Application::reportError(const QString& message)
{
    search_.setStatus(message);
    if (tray_.isVisible())
        tray_.showMessage(QStringLiteral("search"), message, QSystemTrayIcon::Warning);
}

} // namespace searchapp
