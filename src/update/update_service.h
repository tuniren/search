#pragma once

#include "release.h"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSaveFile>
#include <QTemporaryDir>
#include <functional>
#include <memory>

namespace searchapp
{

enum class UpdateState { Idle, Checking, Available, Downloading, Ready, Failed };

struct UpdateStatus {
    UpdateState state = UpdateState::Idle;
    QString message = QStringLiteral("可从 GitHub 检查新版本。");
    QString notes;
    qint64 received = 0;
    qint64 total = 0;
};

class UpdateService final : public QObject
{
    Q_OBJECT
public:
    explicit UpdateService(QString dataDirectory, QObject* parent = nullptr,
                           QNetworkAccessManager* network = nullptr, QString installationDirectory = {});
    ~UpdateService() override;
    void check(bool manual = true);
    void download();
    void cancel();
    void install();
    const UpdateStatus& status() const
    {
        return status_;
    }

signals:
    void changed(const UpdateStatus& status);
    void restartRequested();

private:
    void publish(UpdateState state, const QString& message);
    void fail(const QString& message);
    void fetchJson(const QUrl& url, std::function<void(QByteArray, int)> complete);
    QNetworkReply* request(const QUrl& url);
    void recordCheck();

    QString directory_;
    QString installationDirectory_;
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> reply_;
    UpdateStatus status_;
    Release release_;
    UpdatePackage package_;
    QString variant_;
    std::unique_ptr<QTemporaryDir> temporary_;
    std::unique_ptr<QSaveFile> file_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    QString transferError_;
};

} // namespace searchapp
