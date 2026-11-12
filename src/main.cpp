#include "app/application.h"
#include "ui/theme.h"
#include "version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QSaveFile>
#include <QTimer>
#include <Windows.h>
#include <memory>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("search"));
    QCoreApplication::setApplicationName(QStringLiteral("search"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(searchapp::appVersion));
    app.setWindowIcon(searchapp::glyphIcon(searchapp::Glyph::Search));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("search — Windows 快捷启动与搜索"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("background"), QStringLiteral("在托盘中启动")});
    parser.addOption({QStringLiteral("settings"), QStringLiteral("打开设置窗口")});
    parser.addOption(
        {QStringLiteral("update-ready"), QStringLiteral("更新程序内部启动确认"), QStringLiteral("path")});
    parser.process(app);

    try {
        DWORD session = 0;
        ProcessIdToSessionId(GetCurrentProcessId(), &session);
        const auto identity = qEnvironmentVariable("USERNAME") + QString::number(session);
        const QString serverName = QStringLiteral("search-%1").arg(identity);
        const QString mutexName = QStringLiteral("Local\\search-%1").arg(identity);
        const HANDLE mutex = CreateMutexW(nullptr, FALSE, reinterpret_cast<LPCWSTR>(mutexName.utf16()));
        const bool alreadyRunning = GetLastError() == ERROR_ALREADY_EXISTS;
        const auto release = [](void* handle) {
            if (handle)
                CloseHandle(handle);
        };
        const std::unique_ptr<void, decltype(release)> instance(mutex, release);
        if (!mutex) {
            QMessageBox::critical(nullptr, QStringLiteral("search"), QStringLiteral("无法建立单实例标识。"));
            return 1;
        }
        if (alreadyRunning) {
            AllowSetForegroundWindow(ASFW_ANY);
            QLocalSocket socket;
            socket.connectToServer(serverName);
            if (socket.waitForConnected(2000)) {
                socket.write(parser.isSet(QStringLiteral("settings")) ? "settings\n" : "search\n");
                socket.flush();
                socket.waitForBytesWritten(1000);
                return 0;
            }
            QMessageBox::information(nullptr, QStringLiteral("search"),
                                     QStringLiteral("search 正在启动或已运行，请使用托盘图标打开。"));
            return 1;
        }

        QLocalServer::removeServer(serverName);
        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server.listen(serverName)) {
            QMessageBox::critical(nullptr, QStringLiteral("search"), server.errorString());
            return 1;
        }
        searchapp::Application application;
        QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
            while (auto* socket = server.nextPendingConnection()) {
                const auto receive = [&application, socket] {
                    if (!socket->canReadLine())
                        return;
                    const auto message = socket->readLine(32).trimmed();
                    if (message == "settings")
                        application.showSettings();
                    else if (message == "search")
                        application.showSearch();
                    socket->disconnectFromServer();
                };
                QObject::connect(socket, &QLocalSocket::readyRead, socket, receive);
                QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                QTimer::singleShot(3000, socket, &QLocalSocket::deleteLater);
                receive();
            }
        });
        application.start(parser.isSet(QStringLiteral("background")) ||
                          parser.isSet(QStringLiteral("settings")));
        if (parser.isSet(QStringLiteral("settings")))
            application.showSettings();
        if (parser.isSet(QStringLiteral("update-ready"))) {
            const auto path = parser.value(QStringLiteral("update-ready"));
            const auto expected =
                QCoreApplication::applicationDirPath() + QStringLiteral("/.search-update/started");
            if (QFileInfo(path).absoluteFilePath() == QFileInfo(expected).absoluteFilePath()) {
                QTimer::singleShot(1500, &app, [path] {
                    QSaveFile ready(path);
                    if (ready.open(QIODevice::WriteOnly)) {
                        ready.write("search-update-ready\n");
                        ready.commit();
                    }
                });
            }
        }
        return app.exec();
    } catch (const std::exception& error) {
        QMessageBox::critical(nullptr, QStringLiteral("search"), QString::fromUtf8(error.what()));
        return 1;
    }
}
