#include "app/application.h"
#include "core/search.h"
#include "platform/directory_monitor.h"
#include "platform/windows_integration.h"
#include "ui/result_model.h"
#include "ui/search_window.h"
#include "ui/settings_window.h"
#include "ui/theme.h"

#include <QAbstractEventDispatcher>
#include <QAbstractItemModelTester>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QInputMethodEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QtTest>
#include <Windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

using namespace searchapp;

class UiTests final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qApp->setQuitOnLastWindowClosed(false);
        applyTheme(*qApp, Theme::Light);
    }

    void resultModelContract()
    {
        ResultModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        CatalogItem item{QStringLiteral("id"), QStringLiteral("记事本"),
                         QStringLiteral("C:/Windows/notepad.exe")};
        model.replace({{item, item.target, 1}});
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), Qt::AccessibleTextRole).toString(), item.name);
        QVERIFY(!model.resultAt(-1));
        model.replace({});
        QCOMPARE(model.rowCount(), 0);
    }

    void applicationIconsFollowResults()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const auto cleanup = qScopeGuard([&] {
            if (SUCCEEDED(initialized))
                CoUninitialize();
        });
        const auto shortcut = [&](const QString& name, const QColor& color) {
            const QString iconPath =
                QDir::toNativeSeparators(directory.filePath(name + QStringLiteral(".ico")));
            QImage image(64, 64, QImage::Format_ARGB32);
            image.fill(color);
            if (!image.save(iconPath, "ICO"))
                return QString{};
            Microsoft::WRL::ComPtr<IShellLinkW> link;
            Microsoft::WRL::ComPtr<IPersistFile> file;
            const auto executable =
                qEnvironmentVariable("SystemRoot") + QStringLiteral("\\System32\\notepad.exe");
            if (FAILED(
                    CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) ||
                FAILED(link->SetPath(reinterpret_cast<LPCWSTR>(executable.utf16()))) ||
                FAILED(link->SetIconLocation(reinterpret_cast<LPCWSTR>(iconPath.utf16()), 0)) ||
                FAILED(link.As(&file)))
                return QString{};
            const auto path = QDir::toNativeSeparators(directory.filePath(name + QStringLiteral(".lnk")));
            return SUCCEEDED(file->Save(reinterpret_cast<LPCWSTR>(path.utf16()), TRUE)) ? path : QString{};
        };
        const auto red = shortcut(QStringLiteral("Red"), Qt::red);
        const auto blue = shortcut(QStringLiteral("Blue"), Qt::blue);
        QVERIFY(!red.isEmpty() && !blue.isEmpty());
        const auto customIcon = applicationIcon(red, 64);
        QVERIFY(!customIcon.isNull());
        const auto redPixel = customIcon.pixelColor(customIcon.width() / 2, customIcon.height() / 2);
        QVERIFY(redPixel.red() > 240 && redPixel.green() < 10 && redPixel.blue() < 10);

        ResultModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.replace({{{QStringLiteral("red"), QStringLiteral("Red"), red}, red, 0}});
        // Replace the result while the first Shell lookup is still in flight.
        model.replace({{{QStringLiteral("blue"), QStringLiteral("Blue"), blue}, blue, 0}});
        QTRY_VERIFY_WITH_TIMEOUT(!qvariant_cast<QIcon>(model.index(0).data(Qt::DecorationRole)).isNull(),
                                 5000);
        const auto loaded =
            qvariant_cast<QIcon>(model.index(0).data(Qt::DecorationRole)).pixmap(32, 32).toImage();
        const auto bluePixel = loaded.pixelColor(loaded.width() / 2, loaded.height() / 2);
        QVERIFY(bluePixel.blue() > 240 && bluePixel.red() < 10 && bluePixel.green() < 10);
        // Exercise the AppsFolder image factory against a registered package,
        // rather than pretending a packaged app is a filesystem shortcut.
        const auto applications = discoverApplications(std::make_shared<std::atomic_bool>(false));
        const auto packaged = std::find_if(applications.begin(), applications.end(), [](const auto& item) {
            return item.target.startsWith(QStringLiteral("shell:AppsFolder\\"));
        });
        if (packaged != applications.end())
            QVERIFY2(!applicationIcon(packaged->target, 96).isNull(), qPrintable(packaged->target));
        QCOMPARE(model.index(0).data().toString(), QStringLiteral("Blue"));
        model.replace({});
        QTest::qWait(20);
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(applicationIcon(directory.filePath(QStringLiteral("missing.lnk")), 64).isNull());
    }

    void inputMethodAndKeyboard()
    {
        SearchWindow window;
        window.setHideOnFocusLoss(false);
        window.summon();
        auto* input = window.findChild<QLineEdit*>(QStringLiteral("searchInput"));
        QVERIFY(input);
        int actions = 0;
        connect(&window, &SearchWindow::actionRequested, this, [&](const CatalogItem&, bool) { ++actions; });
        QInputMethodEvent preedit(QStringLiteral("zhong"), {});
        QApplication::sendEvent(input, &preedit);
        QVERIFY(window.composing());
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(actions, 0);
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("中"));
        QApplication::sendEvent(input, &commit);
        QVERIFY(!window.composing());
        QCOMPARE(input->text(), QStringLiteral("中"));
        QTest::qWait(40);
        CatalogItem item{QStringLiteral("app"), QStringLiteral("中文程序"),
                         QStringLiteral("C:/中文程序.exe")};
        window.setResults({{item, item.target, 1}});
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(actions, 1);
        QTest::keyClick(input, Qt::Key_Escape);
        QVERIFY(!window.isVisible());
    }

    void searchSurfaceRepaintsAfterScrollingAndFiltering()
    {
        SearchWindow window;
        window.setWindowFlag(Qt::WindowStaysOnTopHint);
        window.setHideOnFocusLoss(false);
        window.summon();
        auto* input = window.findChild<QLineEdit*>(QStringLiteral("searchInput"));
        auto* list = window.findChild<QListView*>(QStringLiteral("results"));
        QVERIFY(input && list);
        const auto originalCursor = QCursor::pos();
        const auto restoreCursor = qScopeGuard([&] { QCursor::setPos(originalCursor); });
        QCursor::setPos(window.mapToGlobal(QPoint(-30, -30)));
        Results results;
        for (int i = 0; i < 24; ++i) {
            const auto name = QStringLiteral("Old application %1").arg(i);
            results.push_back(
                {{QString::number(i), name, QStringLiteral("C:/Applications/") + name}, name, 0});
        }
        window.setResults(results);
        QTest::qWait(80);
        const int unfilteredHeight = window.height();
        list->verticalScrollBar()->setValue(136);
        QTest::qWait(40);
        QInputMethodEvent preedit(QStringLiteral("weixin"), {});
        QApplication::sendEvent(input, &preedit);
        QTest::qWait(40);
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("微信"));
        QApplication::sendEvent(input, &commit);
        window.setResults(
            {{{QStringLiteral("wechat"), QStringLiteral("微信"), QStringLiteral("C:/WeChat.lnk")},
              QStringLiteral("C:/WeChat.lnk"),
              0},
             {{QStringLiteral("work"), QStringLiteral("企业微信"), QStringLiteral("C:/WeCom.lnk")},
              QStringLiteral("C:/WeCom.lnk"),
              0}});
        QTest::qWait(80);
        QVERIFY(window.height() < unfilteredHeight);
        const auto origin = window.mapToGlobal(QPoint(0, 0));
        const auto displayed =
            window.screen()->grabWindow(0, origin.x(), origin.y(), window.width(), window.height()).toImage();
        // A complete render is the reference for what incremental repainting
        // should already have put on screen. Inspect pixels, not only model data.
        const auto rendered = window.grab().toImage();
        QCOMPARE(displayed.size(), rendered.size());
        const qreal ratio = displayed.devicePixelRatio();
        const QRect region(
            QPoint(qRound(40 * ratio), qRound(95 * ratio)),
            QPoint(qRound((window.width() - 40) * ratio), qRound((window.height() - 50) * ratio)));
        int different = 0;
        for (int y = region.top(); y <= region.bottom(); ++y)
            for (int x = region.left(); x <= region.right(); ++x) {
                const auto actual = displayed.pixelColor(x, y);
                const auto expected = rendered.pixelColor(x, y);
                if (std::abs(actual.red() - expected.red()) > 8 ||
                    std::abs(actual.green() - expected.green()) > 8 ||
                    std::abs(actual.blue() - expected.blue()) > 8)
                    ++different;
            }
        if (different >= region.width() * region.height() / 100) {
            displayed.save(QStringLiteral("refresh-displayed.png"));
            rendered.save(QStringLiteral("refresh-rendered.png"));
        }
        QVERIFY2(different < region.width() * region.height() / 100,
                 qPrintable(QStringLiteral("%1 stale pixels after incremental repaint").arg(different)));
    }

    void settingsCollectsEditedValues()
    {
        SettingsWindow window;
        Settings initial;
        window.setSettings(initial);
        window.show();
        auto* theme = window.findChild<QComboBox*>(QStringLiteral("themeSelector"));
        auto* shortcut = window.findChild<QKeySequenceEdit*>();
        auto* save = window.findChild<QPushButton*>(QStringLiteral("primary"));
        QVERIFY(theme && shortcut && save);
        theme->setCurrentIndex(static_cast<int>(Theme::Dark));
        shortcut->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+K")));
        bool received = false;
        connect(&window, &SettingsWindow::saveRequested, this, [&](const Settings& value) {
            received = true;
            QCOMPARE(value.theme, Theme::Dark);
            QCOMPARE(value.hotkey, QStringLiteral("Ctrl+Alt+K"));
        });
        QTest::mouseClick(save, Qt::LeftButton);
        QVERIFY(received);
    }

    void directoryNotifications()
    {
        QTemporaryDir directory;
        DirectoryMonitor monitor;
        QSignalSpy changes(&monitor, &DirectoryMonitor::changed);
        monitor.setRoots({directory.path()});
        QTest::qWait(100);
        QFile file(directory.filePath(QStringLiteral("new-file.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("test");
        file.close();
        QTRY_VERIFY_WITH_TIMEOUT(changes.count() > 0, 5000);
        monitor.setRoots({}); // Stopping an outstanding overlapped read must terminate.
    }

    void threadHotkeyWithoutNativeResult()
    {
        GlobalHotkey hotkey;
        QString error;
        QVERIFY2(hotkey.setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Shift+F23")), &error),
                 qPrintable(error));
        QSignalSpy activations(&hotkey, &GlobalHotkey::activated);
        MSG message{};
        message.message = WM_HOTKEY;
        // The first thread registration uses this identifier. Qt dispatches
        // thread messages with no result pointer, unlike HWND messages.
        message.wParam = 0x4b51;
        QVERIFY(QAbstractEventDispatcher::instance()->filterNativeEvent("windows_dispatcher_MSG", &message,
                                                                        nullptr));
        QCOMPARE(activations.count(), 1);

        qintptr result = 42;
        QVERIFY(hotkey.nativeEventFilter("windows_generic_MSG", &message, &result));
        QCOMPARE(result, 0);
        QCOMPARE(activations.count(), 2);

        message.message = WM_MOUSEMOVE;
        result = 42;
        QVERIFY(!hotkey.nativeEventFilter("windows_generic_MSG", &message, &result));
        QCOMPARE(result, 42);
    }

    void unregisteredHotkeyDoesNotConsumeMessages()
    {
        GlobalHotkey hotkey;
        QSignalSpy activations(&hotkey, &GlobalHotkey::activated);
        MSG message{};
        message.message = WM_HOTKEY;
        QVERIFY(!hotkey.nativeEventFilter("windows_dispatcher_MSG", &message, nullptr));
        QCOMPARE(activations.count(), 0);
    }

    void startMenuProgramsAndSystemAliases()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("nested"))));
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const auto cleanup = qScopeGuard([&] {
            if (SUCCEEDED(initialized))
                CoUninitialize();
        });
        const auto shortcut = [&](const QString& name, const QString& target) {
            Microsoft::WRL::ComPtr<IShellLinkW> link;
            Microsoft::WRL::ComPtr<IPersistFile> file;
            HRESULT result =
                CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
            if (FAILED(result))
                return result;
            const auto nativeTarget = QDir::toNativeSeparators(target);
            result = link->SetPath(reinterpret_cast<LPCWSTR>(nativeTarget.utf16()));
            if (FAILED(result))
                return result;
            result = link.As(&file);
            if (FAILED(result))
                return result;
            const auto path = QDir::toNativeSeparators(directory.filePath(name + QStringLiteral(".lnk")));
            return file->Save(reinterpret_cast<LPCWSTR>(path.utf16()), TRUE);
        };
        const QString notepad = qEnvironmentVariable("SystemRoot") + QStringLiteral("/System32/notepad.exe");
        QFile thirdParty(directory.filePath(QStringLiteral("WeChat.exe")));
        QVERIFY(thirdParty.open(QIODevice::WriteOnly));
        thirdParty.close();
        QVERIFY(SUCCEEDED(shortcut(QStringLiteral("nested/NoteTool"), notepad)));
        QVERIFY(
            SUCCEEDED(shortcut(QStringLiteral("微信"), directory.filePath(QStringLiteral("WeChat.exe")))));
        QVERIFY(SUCCEEDED(
            shortcut(QStringLiteral("Document"), directory.filePath(QStringLiteral("Readme.txt")))));
        QVERIFY(SUCCEEDED(shortcut(QStringLiteral("Folder"), directory.path())));
        QFile looseExecutable(directory.filePath(QStringLiteral("Loose.exe")));
        QVERIFY(looseExecutable.open(QIODevice::WriteOnly));
        looseExecutable.close();
        auto canceled = std::make_shared<std::atomic_bool>(false);
        auto items = discoverStartMenuApplications({directory.path(), directory.path()}, canceled);
        QCOMPARE(items.size(),
                 2); // Recursive shortcuts only; no documents, directories or loose executables.
        prepareApplications(items);
        for (const auto& query : {QStringLiteral("记事本"), QStringLiteral("Notepad"),
                                  QStringLiteral("jishiben"), QStringLiteral("jsb")}) {
            const auto results = search(items, query);
            QVERIFY2(!results.isEmpty(), qPrintable(query));
            QCOMPARE(results.front().item.name, QStringLiteral("NoteTool"));
        }
        QVERIFY(!search(items, QStringLiteral("微信")).isEmpty());
        QVERIFY(!search(items, QStringLiteral("weixin")).isEmpty());
        QVERIFY(!search(items, QStringLiteral("wx")).isEmpty());
        QVERIFY(search(items, QStringLiteral("WeChat")).isEmpty());
        QVERIFY(systemApplicationAliases(directory.filePath(QStringLiteral("notepad.exe"))).isEmpty());
        Catalog packaged{{QStringLiteral("calculator"), QStringLiteral("Calculator"),
                          QStringLiteral("shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App")}};
        prepareApplications(packaged);
        QVERIFY(!search(packaged, QStringLiteral("计算器")).isEmpty());
        QVERIFY(!search(packaged, QStringLiteral("jisuanqi")).isEmpty());
        QVERIFY(!search(packaged, QStringLiteral("jsq")).isEmpty());
        canceled->store(true);
        QVERIFY(discoverStartMenuApplications({directory.path()}, canceled).isEmpty());
    }

    void rapidSearchAndSelectionDuringCatalogRefresh()
    {
        // Exercise the real coordinator with isolated storage, not a duplicate
        // of its cancellation logic or the user's saved settings and history.
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Settings settings;
        settings.hotkey = QStringLiteral("Ctrl+Alt+Shift+F22");
        settings.hideOnFocusLoss = false;
        QVERIFY(SettingsStore(directory.path()).save(settings));
        Catalog items;
        for (int i = 0; i < 20000; ++i) {
            const QString name = QStringLiteral("程序-code-%1").arg(i);
            items.push_back({QString::number(i), name, QStringLiteral("shell:AppsFolder\\Fixture!") + name});
        }
        CatalogStore(directory.filePath(QStringLiteral("catalog.sqlite"))).replace(items);

        Application application(directory.path());
        application.start(false);
        SearchWindow* window = nullptr;
        for (auto* candidate : QApplication::topLevelWidgets()) {
            if (auto* searchWindow = qobject_cast<SearchWindow*>(candidate)) {
                window = searchWindow;
                break;
            }
        }
        QVERIFY(window);
        auto* input = window->findChild<QLineEdit*>(QStringLiteral("searchInput"));
        auto* list = window->findChild<QListView*>(QStringLiteral("results"));
        QVERIFY(input && list);
        int resultUpdates = 0;
        connect(list->model(), &QAbstractItemModel::modelReset, this, [&] { ++resultUpdates; });

        input->setText(QStringLiteral("code"));
        QTRY_VERIFY_WITH_TIMEOUT(list->model()->rowCount() > 1, 5000);
        QTest::keyClick(input, Qt::Key_Down);
        QCOMPARE(list->currentIndex().row(), 1);
        QTest::keyClick(input, Qt::Key_Up);
        QCOMPARE(list->currentIndex().row(), 0);

        for (int i = 0; i < 120; ++i) {
            input->selectAll();
            QTest::keyClicks(input, i % 2 ? "code" : "no-match");
            QTest::keyClick(input, Qt::Key_Down);
            QTest::keyClick(input, Qt::Key_Up);
            if (i % 3 == 0) {
                QInputMethodEvent preedit(QStringLiteral("xiangmu"), {});
                QApplication::sendEvent(input, &preedit);
                QTest::keyClick(input, Qt::Key_Down);
                QInputMethodEvent commit;
                commit.setCommitString(QStringLiteral("项目"));
                QApplication::sendEvent(input, &commit);
            }
            if (i % 4 == 0)
                QTest::qWait(30); // Let real background results race with editing and painting.
            if (i % 10 == 0) {
                window->hide();
                application.showSearch();
            }
        }

        // No web or calculator fallback may appear for unmatched input.
        const int previousUpdates = resultUpdates;
        input->setText(QStringLiteral("no-such-program-final-marker"));
        QTRY_VERIFY_WITH_TIMEOUT(resultUpdates > previousUpdates, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(list->model()->rowCount(), 0, 5000);
        QVERIFY(resultUpdates > 1);
        QVERIFY(window->isVisible());

        // Destruction while another query is pending must also safely join its worker.
        input->setText(QStringLiteral("code"));
        QTest::qWait(30);
    }

    void frequentApplicationsUpdateAndPersist()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto database = directory.filePath(QStringLiteral("catalog.sqlite"));
        Catalog items{
            {QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("shell:AppsFolder\\Fixture!A")},
            {QStringLiteral("b"), QStringLiteral("Beta"), QStringLiteral("shell:AppsFolder\\Fixture!B")}};
        CatalogStore(database).replace(items);
        {
            CatalogService catalog(database);
            catalog.start();
            QVERIFY(search(*catalog.snapshot(), {}).isEmpty());
            QSignalSpy updates(&catalog, &CatalogService::updated);
            const auto previous = catalog.snapshot();
            catalog.recordLaunch(QStringLiteral("b"));
            QCOMPARE(updates.count(), 1);
            auto frequent = search(*catalog.snapshot(), {});
            QCOMPARE(frequent.size(), 1);
            QCOMPARE(frequent.front().item.id, QStringLiteral("b"));
            QCOMPARE(frequent.front().item.launches, 1);
            QCOMPARE(previous->at(1).launches, 0); // Queries retain an immutable snapshot.
            catalog.recordLaunch(QStringLiteral("a"));
            catalog.recordLaunch(QStringLiteral("b"));
            frequent = search(*catalog.snapshot(), {});
            QCOMPARE(frequent.front().item.id, QStringLiteral("b"));
            QCOMPARE(frequent.front().item.launches, 2);
        }
        {
            CatalogService restarted(database);
            restarted.start();
            const auto frequent = search(*restarted.snapshot(), {});
            QCOMPARE(frequent.size(), 2);
            QCOMPARE(frequent.front().item.id, QStringLiteral("b"));
            QCOMPARE(frequent.front().item.launches, 2);
            restarted.personalize(QStringLiteral("a"), true, {});
            restarted.clearHistory();
            const auto cleared = search(*restarted.snapshot(), {});
            QCOMPARE(cleared.size(), 1);
            QCOMPARE(cleared.front().item.id, QStringLiteral("a"));
            QCOMPARE(cleared.front().item.launches, 0);
        }
    }

    void captureScreenshots()
    {
        const QString directory = qEnvironmentVariable("SEARCH_CAPTURE_DIR");
        if (directory.isEmpty())
            QSKIP("Set SEARCH_CAPTURE_DIR to export UI previews.");
        QDir().mkpath(directory);
        SearchWindow window;
        window.setHideOnFocusLoss(false);
        window.summon();
        Results sample{
            {{QStringLiteral("1"), QStringLiteral("Visual Studio Code"),
              QStringLiteral("C:/Program Files/Microsoft VS Code/Code.exe")},
             QStringLiteral("应用 · 代码编辑器"),
             10},
            {{QStringLiteral("2"), QStringLiteral("记事本"), QStringLiteral("C:/Windows/notepad.exe")},
             QStringLiteral("Windows 系统程序"),
             9},
            {{QStringLiteral("3"), QStringLiteral("计算器"),
              QStringLiteral("shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App")},
             QStringLiteral("Windows 系统程序"),
             8}};
        window.setResults(sample);
        window.setStatus(QStringLiteral("界面预览 · 示例结果"));
        QTest::qWait(60);
        QVERIFY(window.grab().save(directory + QStringLiteral("/search.png")));
        SettingsWindow settings;
        settings.setSettings(Settings{});
        settings.show();
        QTest::qWait(60);
        QVERIFY(settings.grab().save(directory + QStringLiteral("/settings.png")));
        auto* navigation = settings.findChild<QListWidget*>(QStringLiteral("navigation"));
        navigation->setCurrentRow(2);
        QTest::qWait(30);
        QVERIFY(settings.grab().save(directory + QStringLiteral("/settings-search.png")));
        applyTheme(*qApp, Theme::Dark);
        window.setResults(sample);
        QTest::qWait(30);
        QVERIFY(window.grab().save(directory + QStringLiteral("/search-dark.png")));
        applyTheme(*qApp, Theme::Light);
    }
};

QTEST_MAIN(UiTests)
#include "ui_tests.moc"
