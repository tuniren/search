#pragma once

#include "core/types.h"
#include "platform/directory_monitor.h"
#include "storage/catalog_store.h"

#include <QFutureWatcher>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <memory>

namespace searchapp
{

struct ScanOutcome {
    Catalog items;
    QString error;
};

class CatalogService final : public QObject
{
    Q_OBJECT
public:
    CatalogService(QString databasePath, QObject* parent = nullptr);
    ~CatalogService() override;
    void start();
    void refresh();
    void recordLaunch(const QString& id);
    void personalize(const QString& id, bool pinned, const QString& alias);
    void clearHistory();
    std::shared_ptr<const Catalog> snapshot() const
    {
        return catalog_;
    }

signals:
    void updated();
    void statusChanged(const QString& status);

private:
    void startScan();
    void finishScan();

    CatalogStore store_;
    std::shared_ptr<const Catalog> catalog_ = std::make_shared<Catalog>();
    std::shared_ptr<std::atomic_bool> canceled_;
    QThreadPool pool_;
    QFutureWatcher<ScanOutcome> watcher_;
    DirectoryMonitor monitor_;
    QTimer refreshTimer_;
    QTimer reconciliationTimer_;
    bool rerun_ = false;
    bool configured_ = false;
    bool scanRunning_ = false;
};

} // namespace searchapp
