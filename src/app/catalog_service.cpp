#include "catalog_service.h"

#include "platform/windows_integration.h"

#include <QDateTime>
#include <QtConcurrentRun>
#include <algorithm>

namespace searchapp
{
namespace
{

ScanOutcome scan(const CatalogStore& store, const std::shared_ptr<std::atomic_bool>& canceled)
{
    ScanOutcome result;
    try {
        result.items = discoverApplications(canceled);
        if (!canceled->load()) {
            prepareApplications(result.items);
            store.replace(result.items);
            result.items = store.load();
            prepareApplications(result.items);
        }
    } catch (const std::exception& error) {
        result.error = QString::fromUtf8(error.what());
    }
    return result;
}

} // namespace

CatalogService::CatalogService(QString databasePath, QObject* parent)
    : QObject(parent), store_(std::move(databasePath)), monitor_(this)
{
    pool_.setMaxThreadCount(1);
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(2000);
    reconciliationTimer_.setInterval(10 * 60 * 1000);
    connect(&refreshTimer_, &QTimer::timeout, this, &CatalogService::refresh);
    connect(&reconciliationTimer_, &QTimer::timeout, this, &CatalogService::refresh);
    connect(&monitor_, &DirectoryMonitor::changed, this, [this] { refreshTimer_.start(); });
    connect(&watcher_, &QFutureWatcher<ScanOutcome>::finished, this, &CatalogService::finishScan);
}

CatalogService::~CatalogService()
{
    if (canceled_)
        canceled_->store(true);
    monitor_.setRoots({});
    pool_.waitForDone();
}

void CatalogService::start()
{
    if (configured_)
        return;
    configured_ = true;
    monitor_.setRoots(startMenuDirectories());
    try {
        auto items = store_.load();
        prepareApplications(items);
        catalog_ = std::make_shared<Catalog>(std::move(items));
        emit updated();
    } catch (const std::exception& error) {
        emit statusChanged(QStringLiteral("程序缓存读取失败：%1").arg(QString::fromUtf8(error.what())));
    }
    reconciliationTimer_.start();
    startScan();
}

void CatalogService::refresh()
{
    refreshTimer_.stop();
    if (scanRunning_) {
        rerun_ = true;
        return;
    }
    startScan();
}

void CatalogService::startScan()
{
    scanRunning_ = true;
    rerun_ = false;
    canceled_ = std::make_shared<std::atomic_bool>(false);
    emit statusChanged(QStringLiteral("正在更新程序和系统入口…"));
    watcher_.setFuture(
        QtConcurrent::run(&pool_, [store = store_, canceled = canceled_] { return scan(store, canceled); }));
}

void CatalogService::finishScan()
{
    scanRunning_ = false;
    auto result = watcher_.result();
    if (!canceled_->load() && result.error.isEmpty()) {
        // Preserve edits made while the background snapshot was being built.
        QHash<QString, CatalogItem> previous;
        for (const auto& item : *catalog_)
            previous.insert(item.id, item);
        for (auto& item : result.items) {
            if (const auto found = previous.constFind(item.id); found != previous.cend()) {
                item.launches = found->launches;
                item.lastUsed = found->lastUsed;
                item.pinned = found->pinned;
                item.alias = found->alias;
            }
        }
        catalog_ = std::make_shared<Catalog>(std::move(result.items));
        emit updated();
        const auto systemCount = std::count_if(catalog_->cbegin(), catalog_->cend(), [](const auto& item) {
            return item.kind != ItemKind::Application;
        });
        emit statusChanged(
            QStringLiteral("已收录 %1 项，含 %2 个系统入口").arg(catalog_->size()).arg(systemCount));
    } else if (!result.error.isEmpty()) {
        emit statusChanged(QStringLiteral("程序列表更新失败：%1").arg(result.error));
    }
    if (rerun_)
        startScan();
}

void CatalogService::recordLaunch(const QString& id)
{
    store_.recordLaunch(id);
    auto updated = std::make_shared<Catalog>(*catalog_);
    for (auto& item : *updated)
        if (item.id == id) {
            ++item.launches;
            item.lastUsed = QDateTime::currentSecsSinceEpoch();
            break;
        }
    catalog_ = std::move(updated);
    emit this->updated();
}

void CatalogService::personalize(const QString& id, bool pinned, const QString& alias)
{
    store_.setPersonalization(id, pinned, alias);
    auto updated = std::make_shared<Catalog>(*catalog_);
    for (auto& item : *updated)
        if (item.id == id) {
            item.pinned = pinned;
            item.alias = alias;
            break;
        }
    catalog_ = std::move(updated);
    emit this->updated();
}

void CatalogService::clearHistory()
{
    store_.clearHistory();
    auto updated = std::make_shared<Catalog>(*catalog_);
    for (auto& item : *updated) {
        item.launches = 0;
        item.lastUsed = 0;
    }
    catalog_ = std::move(updated);
    emit this->updated();
}

} // namespace searchapp
