#pragma once

#include <QHash>
#include <QString>
#include <QUrl>

namespace searchapp
{

struct ReleaseAsset {
    QUrl url;
    qint64 size = 0;
};

struct Release {
    QString version;
    QString notes;
    QHash<QString, ReleaseAsset> assets;
};

struct UpdatePackage {
    QUrl url;
    QString root;
    QByteArray sha256;
    qint64 size = 0;
};

bool newerVersion(const QString& candidate, const QString& current);
Release parseRelease(const QByteArray& json);
UpdatePackage selectPackage(const QByteArray& json, const Release& release, const QString& variant);
bool trustedUpdateUrl(const QUrl& url);

} // namespace searchapp
