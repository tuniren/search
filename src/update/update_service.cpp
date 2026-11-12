#include "update_service.h"

#include "version.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QTemporaryFile>
#include <QTimer>

namespace searchapp
{
namespace
{
const QUrl latestRelease(QStringLiteral("https://api.github.com/repos/tuniren/search/releases/latest"));

QString installationVariant(const QString& directory)
{
    QFile file(directory + QStringLiteral("/package-info.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4096)
        return {};
    const auto value =
        QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("variant")).toString();
    return value == QStringLiteral("with-runtime") || value == QStringLiteral("no-runtime") ? value
                                                                                            : QString{};
}
} // namespace

UpdateService::UpdateService(QString dataDirectory, QObject* parent, QNetworkAccessManager* network,
                             QString installationDirectory)
    : QObject(parent), directory_(std::move(dataDirectory)),
      installationDirectory_(installationDirectory.isEmpty() ? QCoreApplication::applicationDirPath()
                                                             : std::move(installationDirectory)),
      network_(network ? network : new QNetworkAccessManager(this)),
      variant_(installationVariant(installationDirectory_))
{
    // Keep only recent downloads; transaction backups live in the installation directory.
    const QDir cache(directory_ + QStringLiteral("/updates"));
    for (const auto& entry :
         cache.entryInfoList({QStringLiteral("download-*")}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!entry.isSymLink() && entry.lastModified().daysTo(QDateTime::currentDateTime()) > 7)
            QDir(entry.absoluteFilePath()).removeRecursively();
    }
}

UpdateService::~UpdateService()
{
    if (reply_) {
        disconnect(reply_, nullptr, this, nullptr);
        reply_->abort();
        reply_->deleteLater();
    }
}

void UpdateService::publish(UpdateState state, const QString& message)
{
    status_.state = state;
    status_.message = message;
    emit changed(status_);
}

void UpdateService::fail(const QString& message)
{
    if (file_) {
        file_->cancelWriting();
        file_.reset();
    }
    temporary_.reset();
    publish(UpdateState::Failed, message + QStringLiteral(" 可重试，或打开 GitHub 发布页手动下载。"));
}

QNetworkReply* UpdateService::request(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", QByteArray("search/") + appVersion);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setTransferTimeout(30000);
    auto* reply = network_->get(request);
    reply->setReadBufferSize(256 * 1024);
    connect(reply, &QNetworkReply::redirected, this, [reply](const QUrl& redirect) {
        if (trustedUpdateUrl(redirect))
            reply->redirectAllowed();
        else
            reply->abort();
    });
    reply_ = reply;
    return reply;
}

void UpdateService::fetchJson(const QUrl& url, std::function<void(QByteArray, int)> complete)
{
    auto* reply = request(url);
    auto data = std::make_shared<QByteArray>();
    connect(reply, &QIODevice::readyRead, this, [reply, data] {
        data->append(reply->readAll());
        if (data->size() > 1024 * 1024)
            reply->abort();
    });
    QTimer::singleShot(45000, reply, [reply] {
        if (!reply->isFinished())
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, data, complete = std::move(complete)] {
        reply_ = nullptr;
        reply->deleteLater();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (data->size() > 1024 * 1024) {
            fail(QStringLiteral("更新信息超过大小限制。"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError && !(http == 404 && reply->url() == latestRelease)) {
            fail(http == 403 || http == 429
                     ? QStringLiteral("GitHub 请求受限，请稍后再试。")
                     : QStringLiteral("无法获取更新信息：%1").arg(reply->errorString()));
            return;
        }
        try {
            complete(*data, http);
        } catch (const std::exception& error) {
            fail(QString::fromUtf8(error.what()));
        }
    });
}

void UpdateService::recordCheck()
{
    QSettings file(directory_ + QStringLiteral("/updates.ini"), QSettings::IniFormat);
    file.setValue(QStringLiteral("lastCheck"), QDateTime::currentDateTimeUtc());
}

void UpdateService::check(bool manual)
{
    if (reply_ || status_.state == UpdateState::Ready)
        return;
    QSettings cache(directory_ + QStringLiteral("/updates.ini"), QSettings::IniFormat);
    const auto elapsed =
        cache.value(QStringLiteral("lastCheck")).toDateTime().secsTo(QDateTime::currentDateTimeUtc());
    if (!manual && cache.contains(QStringLiteral("lastCheck")) && elapsed >= 0 && elapsed < 24 * 60 * 60)
        return;
    temporary_.reset();
    release_ = {};
    package_ = {};
    status_.notes.clear();
    recordCheck();
    publish(UpdateState::Checking, QStringLiteral("正在检查 GitHub Releases…"));
    fetchJson(latestRelease, [this](QByteArray data, int http) {
        if (http == 404) {
            publish(UpdateState::Idle, QStringLiteral("尚无可访问的正式发布版本。"));
            return;
        }
        release_ = parseRelease(data);
        if (!newerVersion(release_.version, QString::fromLatin1(appVersion))) {
            publish(UpdateState::Idle,
                    QStringLiteral("当前已是最新版本（%1）。").arg(QString::fromLatin1(appVersion)));
            return;
        }
        status_.notes = release_.notes;
        if (variant_.isEmpty()) {
            fail(QStringLiteral("发现新版本 %1，但当前目录缺少发行包标记，请手动下载完整包。")
                     .arg(release_.version));
            return;
        }
        fetchJson(release_.assets.value(QStringLiteral("update.json")).url, [this](QByteArray manifest, int) {
            package_ = selectPackage(manifest, release_, variant_);
            publish(UpdateState::Available, QStringLiteral("发现新版本 %1（%2 MB）。")
                                                .arg(release_.version)
                                                .arg(package_.size / 1e6, 0, 'f', 2));
        });
    });
}

void UpdateService::download()
{
    if (status_.state != UpdateState::Available || reply_)
        return;
    const auto updates = directory_ + QStringLiteral("/updates");
    if (!QDir().mkpath(updates)) {
        fail(QStringLiteral("无法创建更新缓存目录。"));
        return;
    }
    temporary_ = std::make_unique<QTemporaryDir>(updates + QStringLiteral("/download-XXXXXX"));
    if (!temporary_->isValid()) {
        fail(QStringLiteral("无法创建更新临时目录。"));
        return;
    }
    file_ = std::make_unique<QSaveFile>(temporary_->filePath(QStringLiteral("package.zip")));
    if (!file_->open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("无法保存更新包。"));
        return;
    }
    hash_.reset();
    transferError_.clear();
    status_.received = 0;
    status_.total = package_.size;
    publish(UpdateState::Downloading, QStringLiteral("正在下载 %1…").arg(release_.version));
    auto* reply = request(package_.url);
    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        const auto data = reply->readAll();
        if (status_.received + data.size() > package_.size) {
            transferError_ = QStringLiteral("下载内容超过清单中声明的大小。");
            reply->abort();
            return;
        }
        if (file_->write(data) != data.size()) {
            transferError_ = QStringLiteral("更新包写入失败，请检查剩余磁盘空间。");
            reply->abort();
            return;
        }
        hash_.addData(data);
        status_.received += data.size();
        emit changed(status_);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply_ = nullptr;
        reply->deleteLater();
        if (!transferError_.isEmpty() || reply->error() != QNetworkReply::NoError) {
            fail(!transferError_.isEmpty() ? transferError_
                                           : QStringLiteral("下载失败：%1").arg(reply->errorString()));
            return;
        }
        if (status_.received != package_.size || hash_.result().toHex() != package_.sha256) {
            fail(QStringLiteral("更新包大小或 SHA-256 校验失败，已丢弃下载。"));
            return;
        }
        if (!file_->commit()) {
            fail(QStringLiteral("无法保存已校验的更新包。"));
            return;
        }
        file_.reset();
        publish(UpdateState::Ready,
                QStringLiteral("%1 已下载并校验，点击“重启更新”安装。").arg(release_.version));
    });
}

void UpdateService::cancel()
{
    if (status_.state != UpdateState::Downloading || !reply_)
        return;
    auto* reply = reply_.data();
    reply_ = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
    file_->cancelWriting();
    file_.reset();
    temporary_.reset();
    publish(UpdateState::Available, QStringLiteral("下载已取消，可重新下载。"));
}

void UpdateService::install()
{
    if (status_.state != UpdateState::Ready || !temporary_)
        return;
    const auto target = installationDirectory_;
    QTemporaryFile probe(target + QStringLiteral("/.search-write-XXXXXX"));
    if (!probe.open()) {
        fail(QStringLiteral("当前程序目录不可写，请将完整包解压到有写入权限的目录。"));
        return;
    }
    const auto helper = temporary_->filePath(QStringLiteral("search-updater.exe"));
    QFile::remove(helper);
    if (!QFile::copy(target + QStringLiteral("/search-updater.exe"), helper)) {
        fail(QStringLiteral("无法准备独立更新程序。"));
        return;
    }
    const QStringList arguments{QStringLiteral("--apply"),
                                QString::number(QCoreApplication::applicationPid()),
                                QDir::toNativeSeparators(target),
                                QDir::toNativeSeparators(temporary_->filePath(QStringLiteral("package.zip"))),
                                QString::fromLatin1(package_.sha256),
                                package_.root};
    if (!QProcess::startDetached(helper, arguments, temporary_->path())) {
        fail(QStringLiteral("无法启动更新程序，当前版本仍可继续使用。"));
        return;
    }
    temporary_->setAutoRemove(false);
    emit restartRequested();
}

} // namespace searchapp
