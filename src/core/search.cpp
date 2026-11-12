#include "search.h"

#include <QDateTime>
#include <algorithm>
#include <cmath>

namespace searchapp
{

void prepareSearch(CatalogItem& item)
{
    item.pinyinNames.clear();
    for (const auto& name : QStringList{item.name} + item.searchAliases) {
        auto pinyin = pinyinName(normalized(name));
        if (!pinyin.isEmpty())
            item.pinyinNames.push_back(std::move(pinyin));
    }
}

QString normalized(QString text)
{
    return text.normalized(QString::NormalizationForm_KC).toCaseFolded().simplified();
}

int matchScore(const QString& query, const QString& candidate)
{
    if (query.isEmpty())
        return 1;
    if (query == candidate)
        return 1000;
    if (candidate.startsWith(query))
        return 850;
    const auto position = candidate.indexOf(query);
    if (position >= 0)
        return 650 - static_cast<int>(std::min<qsizetype>(position, 100));
    // Subsequence matching is deliberately bounded to short names, never paths.
    if (query.size() < 2 || query.size() > 32 || candidate.size() > 128)
        return 0;
    qsizetype cursor = 0;
    int gaps = 0;
    for (const QChar character : query) {
        const auto found = candidate.indexOf(character, cursor);
        if (found < 0)
            return 0;
        gaps += static_cast<int>(found - cursor);
        cursor = found + 1;
    }
    return std::max(100, 350 - gaps * 5);
}

Results search(const Catalog& catalog, QString query, const std::shared_ptr<std::atomic_bool>& canceled)
{
    Results results;
    const QString needle = normalized(query.trimmed().left(512));
    const auto now = QDateTime::currentSecsSinceEpoch();
    for (const auto& item : catalog) {
        if (canceled && canceled->load(std::memory_order_relaxed))
            return {};
        if (needle.isEmpty() && item.launches == 0 && !item.pinned)
            continue;
        int score = matchScore(needle, normalized(item.name));
        for (const auto& alias : item.searchAliases)
            score = std::max(score, matchScore(needle, normalized(alias)));
        for (const auto& name : item.pinyinNames)
            score = std::max(score, pinyinScore(needle, name));
        if (!item.alias.isEmpty()) {
            score = std::max(score, matchScore(needle, normalized(item.alias)));
            score = std::max(score, pinyinScore(needle, pinyinName(item.alias)));
        }
        if (score == 0)
            continue;
        score += item.pinned ? 80 : 0;
        score += static_cast<int>(std::min(30.0, std::log2(item.launches + 1.0) * 5));
        if (item.lastUsed > 0 && now - item.lastUsed < 86400 * 7)
            score += 15;
        results.push_back({item, item.target, score});
    }
    std::sort(results.begin(), results.end(), [frequent = needle.isEmpty()](const auto& a, const auto& b) {
        if (frequent) {
            if (a.item.pinned != b.item.pinned)
                return a.item.pinned;
            if (a.item.launches != b.item.launches)
                return a.item.launches > b.item.launches;
            if (a.item.lastUsed != b.item.lastUsed)
                return a.item.lastUsed > b.item.lastUsed;
        }
        if (a.score != b.score)
            return a.score > b.score;
        const int nameOrder = QString::compare(a.item.name, b.item.name, Qt::CaseInsensitive);
        return nameOrder != 0 ? nameOrder < 0 : a.item.id < b.item.id;
    });
    if (results.size() > 40)
        results.resize(40);
    return results;
}

} // namespace searchapp
