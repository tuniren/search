#include "core/pinyin.h"
#include "core/search.h"
#include "storage/catalog_store.h"
#include "storage/settings.h"

#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using namespace searchapp;

class CoreTests final : public QObject
{
    Q_OBJECT
private slots:
    void pinyinReadings()
    {
        const auto calculator = pinyinName(QStringLiteral("计算器"));
        QVERIFY(pinyinScore(QStringLiteral("jisuanqi"), calculator) > 0);
        QVERIFY(pinyinScore(QStringLiteral("jsq"), calculator) > 0);
        QVERIFY(pinyinScore(QStringLiteral("ji suan qi"), calculator) > 0);
        QVERIFY(pinyinScore(QStringLiteral("suanq"), calculator) > 0);
        QCOMPARE(pinyinScore(QStringLiteral("jishiqi"), calculator), 0);
        QCOMPARE(pinyinScore(QStringLiteral("???"), calculator), 0);
        const auto polyphonic = pinyinName(QStringLiteral("重庆银行"));
        QVERIFY(pinyinScore(QStringLiteral("chongqingyinhang"), polyphonic) > 0);
        QVERIFY(pinyinScore(QStringLiteral("cqyh"), polyphonic) > 0);
        QVERIFY(pinyinScore(QStringLiteral("zhongqingyinxing"), polyphonic) > 0);
        QVERIFY(pinyinScore(QStringLiteral("lv"), pinyinName(QStringLiteral("绿"))) > 0);
        QVERIFY(pinyinScore(QStringLiteral("weixinpc"), pinyinName(QStringLiteral("微信 PC"))) > 0);
        QVERIFY(pinyinScore(QStringLiteral("chong").repeated(20), pinyinName(QString(20, u'重'))) > 0);
    }

    void matchingAndRanking()
    {
        Catalog catalog{{
                            QStringLiteral("1"),
                            QStringLiteral("Visual Studio Code"),
                            QStringLiteral("C:/code.lnk"),
                        },
                        {QStringLiteral("2"), QStringLiteral("Code"), QStringLiteral("C:/Code.exe")},
                        {QStringLiteral("3"), QStringLiteral("微信"), QStringLiteral("C:/WeChat.exe")}};
        for (auto& item : catalog)
            prepareSearch(item);
        auto found = search(catalog, QStringLiteral("code"));
        QCOMPARE(found.size(), 2);
        QCOMPARE(found.front().item.id, QStringLiteral("2"));
        QCOMPARE(search(catalog, QStringLiteral("微信")).front().item.id, QStringLiteral("3"));
        QCOMPARE(search(catalog, QStringLiteral("weixin")).front().item.id, QStringLiteral("3"));
        QCOMPARE(search(catalog, QStringLiteral("wx")).front().item.id, QStringLiteral("3"));
        QVERIFY(search(catalog, QStringLiteral("WeChat"))
                    .isEmpty()); // No third-party translation or path matching.
        QCOMPARE(normalized(QStringLiteral("ＣＯＤＥ")), QStringLiteral("code"));
        QCOMPARE(search(catalog, QStringLiteral("vsc")).front().item.id, QStringLiteral("1"));
        catalog[0].alias = QStringLiteral("编辑器");
        QCOMPARE(search(catalog, QStringLiteral("编辑器")).front().item.id, QStringLiteral("1"));
        QCOMPARE(search(catalog, QStringLiteral("bjq")).front().item.id, QStringLiteral("1"));
    }

    void applicationsOnlyAndCancellation()
    {
        QVERIFY(search({}, QStringLiteral("=2*(3+4)")).isEmpty());
        QVERIFY(search({}, QStringLiteral("web C++")).isEmpty());
        Catalog catalog{{QStringLiteral("1"), QStringLiteral("Test"), {}}};
        auto canceled = std::make_shared<std::atomic_bool>(true);
        QVERIFY(search(catalog, QStringLiteral("Test"), canceled).isEmpty());
    }

    void frequentApplications()
    {
        Catalog catalog{
            {QStringLiteral("unused"), QStringLiteral("A unused"), QStringLiteral("C:/unused.lnk")},
            {QStringLiteral("recent"), QStringLiteral("A recent"), QStringLiteral("C:/recent.lnk"), 2, 200},
            {QStringLiteral("frequent"), QStringLiteral("Z frequent"), QStringLiteral("C:/frequent.lnk"), 20,
             100},
            {QStringLiteral("older"), QStringLiteral("Z older"), QStringLiteral("C:/older.lnk"), 2, 100},
            {QStringLiteral("pinned"), QStringLiteral("Pinned"), QStringLiteral("C:/pinned.lnk"), 0, 0,
             true}};
        const auto defaults = search(catalog, QStringLiteral("  "));
        QCOMPARE(defaults.size(), 4);
        QCOMPARE(defaults[0].item.id, QStringLiteral("pinned"));
        QCOMPARE(defaults[1].item.id, QStringLiteral("frequent"));
        QCOMPARE(defaults[2].item.id, QStringLiteral("recent"));
        QCOMPARE(defaults[3].item.id, QStringLiteral("older"));
        QCOMPARE(search(catalog, QStringLiteral("A unused")).front().item.id, QStringLiteral("unused"));
        QVERIFY(search({catalog.front()}, {}).isEmpty());

        // Heat must not override the relevance of a typed program name.
        catalog[1].launches = 100000;
        catalog[2].name = QStringLiteral("A");
        QCOMPARE(search(catalog, QStringLiteral("A")).front().item.id, QStringLiteral("frequent"));
    }

    void settingsRoundTrip()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SettingsStore store(directory.path());
        Settings value;
        value.hotkey = QStringLiteral("Ctrl+Alt+K");
        value.theme = Theme::Dark;
        QSettings legacy(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        legacy.setValue(QStringLiteral("index/roots"),
                        QStringList{QStringLiteral("C:/Users/example/Documents")});
        legacy.setValue(QStringLiteral("search/web"), true);
        legacy.sync();
        QVERIFY(store.save(value));
        const auto loaded = store.load();
        QCOMPARE(loaded.hotkey, value.hotkey);
        QCOMPARE(loaded.theme, value.theme);
        legacy.sync();
        QVERIFY(!legacy.contains(QStringLiteral("index/roots")));
        QVERIFY(!legacy.contains(QStringLiteral("search/web")));
    }

    void legacyFileCacheIsRemoved()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("catalog.sqlite"));
        CatalogStore store(path);
        const QString connectionName = QStringLiteral("legacy-fixture");
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
            db.setDatabaseName(path);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral(
                "CREATE TABLE items(id TEXT PRIMARY KEY,name TEXT,target TEXT,kind INTEGER)")));
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE usage(id TEXT PRIMARY KEY,launches "
                                              "INTEGER,last_used INTEGER,pinned INTEGER,alias TEXT)")));
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=1")));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO items VALUES('app','Test','C:/Test.lnk',0)")));
            QVERIFY(query.prepare(QStringLiteral("INSERT INTO usage VALUES('app',7,12345,1,?)")));
            query.addBindValue(QStringLiteral("常用程序"));
            QVERIFY(query.exec());
            QVERIFY(
                query.exec(QStringLiteral("INSERT INTO items VALUES('file','Test.txt','C:/Test.txt',1)")));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO items VALUES('folder','Folder','C:/Folder',2)")));
        }
        QSqlDatabase::removeDatabase(connectionName);
        const auto items = store.load();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.front().id, QStringLiteral("app"));
        QVERIFY(items.front().pinned);
        QCOMPARE(items.front().launches, 7);
        QCOMPARE(items.front().alias, QStringLiteral("常用程序"));
    }

    void catalogPersistence()
    {
        QTemporaryDir directory;
        CatalogStore store(directory.filePath(QStringLiteral("catalog.sqlite")));
        Catalog catalog{{QStringLiteral("file-1"), QStringLiteral("程序"), QStringLiteral("C:/程序.lnk")}};
        store.replace(catalog);
        store.recordLaunch(QStringLiteral("file-1"));
        store.setPersonalization(QStringLiteral("file-1"), true, QStringLiteral("说明"));
        store.replace(catalog);
        auto loaded = store.load();
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.front().launches, 1);
        QVERIFY(loaded.front().pinned);
        QCOMPARE(loaded.front().alias, QStringLiteral("说明"));
        store.recordLaunch(QStringLiteral("file-1"));
        loaded = CatalogStore(directory.filePath(QStringLiteral("catalog.sqlite"))).load();
        QCOMPARE(loaded.front().launches, 2);
        QVERIFY(loaded.front().lastUsed > 0);
        store.clearHistory();
        loaded = store.load();
        QCOMPARE(loaded.front().launches, 0);
        QVERIFY(loaded.front().pinned);
        QCOMPARE(loaded.front().alias, QStringLiteral("说明"));
        store.replace({});
        QVERIFY(store.load().isEmpty());
    }
};

QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
