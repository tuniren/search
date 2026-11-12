#include "release.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>
#include <stdexcept>

namespace searchapp
{
namespace
{
QVersionNumber version(const QString& text)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})$"));
    if (!pattern.match(text).hasMatch())
        throw std::runtime_error("版本号格式无效。");
    return QVersionNumber::fromString(text);
}

QJsonObject object(const QByteArray& json)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw std::runtime_error("更新信息不是有效的 JSON。");
    return document.object();
}
} // namespace

bool newerVersion(const QString& candidate, const QString& current)
{
    return version(candidate) > version(current);
}

bool trustedUpdateUrl(const QUrl& url)
{
    static const QStringList hosts{QStringLiteral("api.github.com"), QStringLiteral("github.com"),
                                   QStringLiteral("release-assets.githubusercontent.com"),
                                   QStringLiteral("objects.githubusercontent.com"),
                                   QStringLiteral("github-releases.githubusercontent.com")};
    return url.isValid() && url.scheme() == QStringLiteral("https") && url.userInfo().isEmpty() &&
           (url.port() == -1 || url.port() == 443) && hosts.contains(url.host());
}

Release parseRelease(const QByteArray& json)
{
    const auto data = object(json);
    if (data.value(QStringLiteral("draft")).toBool() || data.value(QStringLiteral("prerelease")).toBool())
        throw std::runtime_error("只接受正式发布版本。");
    const auto tag = data.value(QStringLiteral("tag_name")).toString();
    if (!tag.startsWith(u'v'))
        throw std::runtime_error("发布标签应采用 v主版本.次版本.修订号。");
    Release release;
    release.version = tag.mid(1);
    version(release.version);
    release.notes = data.value(QStringLiteral("body")).toString().left(12000);
    const auto prefix = QStringLiteral("/tuniren/search/releases/download/%1/").arg(tag);
    for (const auto& value : data.value(QStringLiteral("assets")).toArray()) {
        const auto asset = value.toObject();
        const auto name = asset.value(QStringLiteral("name")).toString();
        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        if (name.isEmpty() || name.contains(u'/') || name.contains(u'\\') || !trustedUpdateUrl(url) ||
            url.host() != QStringLiteral("github.com") || url.path() != prefix + name ||
            !url.query().isEmpty() || !url.fragment().isEmpty() || release.assets.contains(name))
            throw std::runtime_error("发布附件地址或名称无效。");
        release.assets.insert(name, {url, asset.value(QStringLiteral("size")).toInteger()});
    }
    if (!release.assets.contains(QStringLiteral("update.json")))
        throw std::runtime_error("该版本尚未提供 update.json，可前往 GitHub 手动下载。");
    return release;
}

UpdatePackage selectPackage(const QByteArray& json, const Release& release, const QString& variant)
{
    const auto data = object(json);
    if (data.value(QStringLiteral("schema")).toInt() != 1 ||
        data.value(QStringLiteral("version")).toString() != release.version ||
        (variant != QStringLiteral("with-runtime") && variant != QStringLiteral("no-runtime")))
        throw std::runtime_error("更新清单版本或发行类型不匹配。");
    const auto root = QStringLiteral("search-%1-windows-x64-%2").arg(release.version, variant);
    const auto name = root + QStringLiteral(".zip");
    UpdatePackage selected;
    int matches = 0;
    for (const auto& value : data.value(QStringLiteral("packages")).toArray()) {
        const auto item = value.toObject();
        if (item.value(QStringLiteral("architecture")).toString() != QStringLiteral("windows-x64") ||
            item.value(QStringLiteral("variant")).toString() != variant)
            continue;
        ++matches;
        const auto size = item.value(QStringLiteral("size")).toInteger();
        const auto hash = item.value(QStringLiteral("sha256")).toString();
        static const QRegularExpression hashPattern(QStringLiteral("^[a-f0-9]{64}$"));
        if (item.value(QStringLiteral("name")).toString() != name || !release.assets.contains(name) ||
            size <= 0 || size > 128 * 1024 * 1024 || release.assets.value(name).size != size ||
            !hashPattern.match(hash).hasMatch())
            throw std::runtime_error("更新包名称、大小或校验值无效。");
        selected = {release.assets.value(name).url, root, hash.toLatin1(), size};
    }
    if (matches != 1)
        throw std::runtime_error("找不到唯一匹配的 Windows x64 更新包。");
    return selected;
}

} // namespace searchapp
