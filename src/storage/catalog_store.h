#pragma once

#include "core/types.h"

namespace searchapp
{

// Each call owns a thread-local SQLite connection; the store itself holds no connection.
class CatalogStore
{
public:
    explicit CatalogStore(QString databasePath);
    Catalog load() const;
    void replace(const Catalog& catalog) const;
    void recordLaunch(const QString& id) const;
    void setPersonalization(const QString& id, bool pinned, const QString& alias) const;
    void clearHistory() const;

private:
    QString path_;
};

} // namespace searchapp
