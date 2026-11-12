#include "catalog_store.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <stdexcept>

namespace searchapp
{
namespace
{

void check(bool ok, const QSqlQuery& query)
{
    if (!ok)
        throw std::runtime_error(query.lastError().text().toStdString());
}

class Connection
{
public:
    explicit Connection(const QString& path) : name_(QUuid::createUuid().toString())
    {
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name_);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
        if (!db.open()) {
            const auto message = db.lastError().text().toStdString();
            db = {};
            QSqlDatabase::removeDatabase(name_);
            throw std::runtime_error(message);
        }
        try {
            QSqlQuery query(db);
            check(query.exec(QStringLiteral("PRAGMA journal_mode=WAL")), query);
            check(query.exec(QStringLiteral("PRAGMA user_version")), query);
            const int version = query.next() ? query.value(0).toInt() : 0;
            if (version > 2)
                throw std::runtime_error("Unsupported catalog schema; database was left unchanged.");
            query.finish();
            if (version < 2) {
                if (!db.transaction())
                    throw std::runtime_error("Cannot begin catalog migration.");
                if (version == 0) {
                    check(query.exec(QStringLiteral("CREATE TABLE items (id TEXT PRIMARY KEY, name TEXT NOT "
                                                    "NULL, target TEXT NOT NULL)")),
                          query);
                    check(query.exec(QStringLiteral(
                              "CREATE TABLE usage (id TEXT PRIMARY KEY, launches INTEGER NOT NULL DEFAULT 0, "
                              "last_used INTEGER NOT NULL DEFAULT 0, pinned INTEGER NOT NULL DEFAULT 0, "
                              "alias TEXT NOT NULL DEFAULT '')")),
                          query);
                } else {
                    // Application was kind 0. Retire file results and their history
                    // atomically, retaining application pins, aliases and launches.
                    check(query.exec(QStringLiteral(
                              "DELETE FROM usage WHERE id IN (SELECT id FROM items WHERE kind <> 0)")),
                          query);
                    check(query.exec(QStringLiteral("DELETE FROM items WHERE kind <> 0")), query);
                    check(query.exec(QStringLiteral("ALTER TABLE items DROP COLUMN kind")), query);
                }
                check(query.exec(QStringLiteral("PRAGMA user_version=2")), query);
                if (!db.commit())
                    throw std::runtime_error("Cannot commit catalog migration.");
            }
        } catch (...) {
            db.rollback();
            db = {};
            QSqlDatabase::removeDatabase(name_);
            throw;
        }
    }

    ~Connection()
    {
        db.close();
        db = {};
        QSqlDatabase::removeDatabase(name_);
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    QSqlDatabase db;

private:
    QString name_;
};

} // namespace

CatalogStore::CatalogStore(QString databasePath) : path_(std::move(databasePath)) {}

Catalog CatalogStore::load() const
{
    Connection connection(path_);
    QSqlQuery query(connection.db);
    check(query.exec(QStringLiteral(
              "SELECT i.id,i.name,i.target,COALESCE(u.launches,0),COALESCE(u.last_used,0),"
              "COALESCE(u.pinned,0),COALESCE(u.alias,'') FROM items i LEFT JOIN usage u ON i.id=u.id")),
          query);
    Catalog items;
    while (query.next()) {
        CatalogItem item;
        item.id = query.value(0).toString();
        item.name = query.value(1).toString();
        item.target = query.value(2).toString();
        item.launches = query.value(3).toInt();
        item.lastUsed = query.value(4).toLongLong();
        item.pinned = query.value(5).toBool();
        item.alias = query.value(6).toString();
        items.push_back(std::move(item));
    }
    return items;
}

void CatalogStore::replace(const Catalog& catalog) const
{
    Connection connection(path_);
    if (!connection.db.transaction())
        throw std::runtime_error("Cannot begin catalog transaction.");
    try {
        QSqlQuery query(connection.db);
        check(query.exec(QStringLiteral("DELETE FROM items")), query);
        check(query.prepare(QStringLiteral("INSERT INTO items(id,name,target) VALUES(?,?,?)")), query);
        for (const auto& item : catalog) {
            query.bindValue(0, item.id);
            query.bindValue(1, item.name);
            query.bindValue(2, item.target);
            check(query.exec(), query);
        }
        if (!connection.db.commit())
            throw std::runtime_error("Cannot commit catalog transaction.");
    } catch (...) {
        connection.db.rollback();
        throw;
    }
}

void CatalogStore::recordLaunch(const QString& id) const
{
    Connection connection(path_);
    QSqlQuery query(connection.db);
    check(query.prepare(QStringLiteral(
              "INSERT INTO usage(id,launches,last_used) VALUES(?,1,?) ON CONFLICT(id) DO UPDATE SET "
              "launches=usage.launches+1,last_used=excluded.last_used")),
          query);
    query.addBindValue(id);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    check(query.exec(), query);
}

void CatalogStore::setPersonalization(const QString& id, bool pinned, const QString& alias) const
{
    Connection connection(path_);
    QSqlQuery query(connection.db);
    check(query.prepare(
              QStringLiteral("INSERT INTO usage(id,pinned,alias) VALUES(?,?,?) ON CONFLICT(id) DO UPDATE SET "
                             "pinned=excluded.pinned,alias=excluded.alias")),
          query);
    query.addBindValue(id);
    query.addBindValue(pinned);
    query.addBindValue(alias.isNull() ? QStringLiteral("") : alias);
    check(query.exec(), query);
}

void CatalogStore::clearHistory() const
{
    Connection connection(path_);
    QSqlQuery query(connection.db);
    check(query.exec(QStringLiteral("UPDATE usage SET launches=0,last_used=0")), query);
}

} // namespace searchapp
