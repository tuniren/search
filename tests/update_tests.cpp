#include "update/release.h"
#include "update/update_service.h"
#include "version.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <cstring>
#include <deque>

using namespace searchapp;

namespace
{
const QString root = QStringLiteral("search-99.0.0-windows-x64-no-runtime");
const QByteArray payload("test update archive");

QByteArray releaseJson()
{
    QJsonArray assets;
    for (const auto& name : {QStringLiteral("update.json"), root + QStringLiteral(".zip")}) {
        assets.append(QJsonObject{
            {"name", name},
            {"size", name.endsWith(u".zip") ? payload.size() : 2048},
            {"browser_download_url",
             QStringLiteral("https://github.com/tuniren/search/releases/download/v99.0.0/") + name}});
    }
    return QJsonDocument(QJsonObject{{"tag_name", "v99.0.0"}, {"body", "Release notes"}, {"assets", assets}})
        .toJson();
}

QByteArray manifestJson(
    const QByteArray& checksum = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex())
{
    const QJsonObject package{{"architecture", "windows-x64"},
                              {"variant", "no-runtime"},
                              {"name", root + QStringLiteral(".zip")},
                              {"size", payload.size()},
                              {"sha256", QString::fromLatin1(checksum)}};
    return QJsonDocument(QJsonObject{{"schema", 1}, {"version", "99.0.0"}, {"packages", QJsonArray{package}}})
        .toJson();
}

struct Response {
    QByteArray body;
    int status = 200;
    bool hold = false;
};

class Reply final : public QNetworkReply
{
public:
    Reply(const QNetworkRequest& request, Response response, QObject* parent)
        : QNetworkReply(parent), response_(std::move(response))
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(QNetworkAccessManager::GetOperation);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        QTimer::singleShot(0, this, [this] {
            if (isFinished())
                return;
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response_.status);
            if (response_.status >= 400)
                setError(QNetworkReply::ContentNotFoundError, QStringLiteral("HTTP error"));
            emit readyRead();
            if (!response_.hold && !isFinished()) {
                setFinished(true);
                emit finished();
            }
        });
    }
    void abort() override
    {
        if (isFinished())
            return;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("Canceled"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override
    {
        return response_.body.size() - position_ + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char* destination, qint64 size) override
    {
        const auto count = std::min(size, response_.body.size() - position_);
        if (!count)
            return -1;
        std::memcpy(destination, response_.body.constData() + position_, static_cast<size_t>(count));
        position_ += count;
        return count;
    }

private:
    Response response_;
    qint64 position_ = 0;
};

class Network final : public QNetworkAccessManager
{
public:
    std::deque<Response> responses;
    int requests = 0;

protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override
    {
        ++requests;
        if (responses.empty())
            return new Reply(request, {{}, 404, false}, this);
        auto response = std::move(responses.front());
        responses.pop_front();
        return new Reply(request, std::move(response), this);
    }
};

void markInstallation(const QString& directory)
{
    QFile file(directory + QStringLiteral("/package-info.json"));
    if (file.open(QIODevice::WriteOnly))
        file.write("{\"variant\":\"no-runtime\"}");
}
} // namespace

class UpdateTests final : public QObject
{
    Q_OBJECT
private slots:
    void numericVersionsAndUrls()
    {
        QVERIFY(newerVersion(QStringLiteral("0.10.0"), QStringLiteral("0.9.9")));
        QVERIFY(!newerVersion(QStringLiteral("0.2.0"), QStringLiteral("0.2.0")));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 newerVersion(QStringLiteral("0.3.0-beta"), QString::fromLatin1(appVersion)));
        QVERIFY(trustedUpdateUrl(
            QUrl(QStringLiteral("https://release-assets.githubusercontent.com/file?token=value"))));
        QVERIFY(!trustedUpdateUrl(QUrl(QStringLiteral("http://github.com/file"))));
        QVERIFY(!trustedUpdateUrl(QUrl(QStringLiteral("https://github.com.evil.test/file"))));
    }

    void manifestValidation()
    {
        const auto release = parseRelease(releaseJson());
        const auto package = selectPackage(manifestJson(), release, QStringLiteral("no-runtime"));
        QCOMPARE(package.size, payload.size());
        QCOMPARE(package.root, root);
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 selectPackage(manifestJson(), release, QStringLiteral("with-runtime")));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 selectPackage(manifestJson("wrong"), release, QStringLiteral("no-runtime")));
        auto json = QJsonDocument::fromJson(manifestJson()).object();
        auto packages = json["packages"].toArray();
        packages.append(packages.first());
        json["packages"] = packages;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, selectPackage(QJsonDocument(json).toJson(), release,
                                                                   QStringLiteral("no-runtime")));
        auto wrongSize = release;
        wrongSize.assets[root + QStringLiteral(".zip")].size++;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                                 selectPackage(manifestJson(), wrongSize, QStringLiteral("no-runtime")));
    }

    void downloadVerifiedAndDailyCheckCached()
    {
        QTemporaryDir directory;
        markInstallation(directory.path());
        Network network;
        network.responses = {{releaseJson()}, {manifestJson()}, {payload}};
        UpdateService service(directory.path(), nullptr, &network, directory.path());
        service.check();
        service.check(); // A repeated click must not start a second request.
        QTRY_COMPARE(service.status().state, UpdateState::Available);
        QCOMPARE(network.requests, 2);
        service.check(false);
        QCOMPARE(network.requests, 2);
        service.download();
        QTRY_COMPARE(service.status().state, UpdateState::Ready);
        QCOMPARE(service.status().received, payload.size());
        const QDir cache(directory.filePath(QStringLiteral("updates")));
        const auto directories = cache.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(directories.size(), 1);
        QFile archive(cache.filePath(directories.front() + QStringLiteral("/package.zip")));
        QVERIFY(archive.open(QIODevice::ReadOnly));
        QCOMPARE(archive.readAll(), payload);
    }

    void badDownloadIsDiscarded()
    {
        QTemporaryDir directory;
        markInstallation(directory.path());
        Network network;
        network.responses = {{releaseJson()}, {manifestJson()}, {QByteArray(payload.size(), 'x')}};
        UpdateService service(directory.path(), nullptr, &network, directory.path());
        service.check();
        QTRY_COMPARE(service.status().state, UpdateState::Available);
        service.download();
        QTRY_COMPARE(service.status().state, UpdateState::Failed);
        QVERIFY(QDir(directory.filePath(QStringLiteral("updates")))
                    .entryList(QDir::Dirs | QDir::NoDotAndDotDot)
                    .isEmpty());
    }

    void cancelAndRetry()
    {
        QTemporaryDir directory;
        markInstallation(directory.path());
        Network network;
        network.responses = {{releaseJson()}, {manifestJson()}, {payload.left(3), 200, true}, {payload}};
        UpdateService service(directory.path(), nullptr, &network, directory.path());
        service.check();
        QTRY_COMPARE(service.status().state, UpdateState::Available);
        service.download();
        QTRY_COMPARE(service.status().received, 3);
        service.cancel();
        QCOMPARE(service.status().state, UpdateState::Available);
        service.download();
        QTRY_COMPARE(service.status().state, UpdateState::Ready);
    }

    void noReleaseAndRateLimit()
    {
        QTemporaryDir directory;
        Network network;
        network.responses = {{{}, 404}, {{}, 403}};
        UpdateService service(directory.path(), nullptr, &network, directory.path());
        service.check();
        QTRY_COMPARE(service.status().state, UpdateState::Idle);
        service.check(false);
        QCOMPARE(network.requests, 1);
        service.check();
        QTRY_COMPARE(service.status().state, UpdateState::Failed);
        QVERIFY(service.status().message.contains(QStringLiteral("受限")));
    }

    void liveReleaseCheck()
    {
        if (!qEnvironmentVariableIsSet("SEARCH_LIVE_UPDATE_TEST"))
            QSKIP("Set SEARCH_LIVE_UPDATE_TEST to check the published release over HTTPS.");
        QVERIFY(QSslSocket::supportsSsl());
        QTemporaryDir directory;
        UpdateService service(directory.path());
        service.check();
        QTRY_VERIFY_WITH_TIMEOUT(service.status().state != UpdateState::Checking, 55000);
        QVERIFY2(service.status().state == UpdateState::Idle, qPrintable(service.status().message));
        QVERIFY(service.status().message.contains(QString::fromLatin1(appVersion)));
    }
};

QTEST_GUILESS_MAIN(UpdateTests)
#include "update_tests.moc"
