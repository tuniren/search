#include "pinyin.h"

#include <QBitArray>
#include <algorithm>
#include <iterator>

namespace searchapp
{
namespace
{

struct Reading {
    char32_t character;
    const char* syllables;
};

constexpr Reading readings[] = {
#include "pinyin_data.inc"
};

bool matches(const QString& query, const PinyinName& name, qsizetype start, bool initials)
{
    QBitArray positions(query.size() + 1);
    positions.setBit(0);
    for (qsizetype i = start; i < name.size(); ++i) {
        QBitArray next(query.size() + 1);
        for (qsizetype position = 0; position < query.size(); ++position) {
            if (!positions.testBit(position))
                continue;
            const auto remaining = QStringView(query).mid(position);
            for (const auto& syllable : name[i]) {
                const auto token = QStringView(syllable).left(initials ? 1 : syllable.size());
                if (token.startsWith(remaining))
                    return true;
                if (remaining.startsWith(token))
                    next.setBit(position + token.size());
            }
        }
        if (next.count(true) == 0)
            return false;
        positions = std::move(next);
    }
    return false;
}

} // namespace

PinyinName pinyinName(const QString& name)
{
    PinyinName result;
    bool hasChinese = false;
    for (const char32_t character : name.toCaseFolded().toUcs4()) {
        const auto found =
            std::lower_bound(std::begin(readings), std::end(readings), character,
                             [](const Reading& entry, char32_t code) { return entry.character < code; });
        if (found != std::end(readings) && found->character == character) {
            result.push_back(QString::fromLatin1(found->syllables).split(u','));
            hasChinese = true;
        } else if ((character >= U'a' && character <= U'z') || (character >= U'0' && character <= U'9')) {
            result.push_back({QString(QChar(static_cast<char16_t>(character)))});
        }
    }
    return hasChinese ? result : PinyinName{};
}

int pinyinScore(const QString& query, const PinyinName& name)
{
    if (name.isEmpty() || query.isEmpty())
        return 0;
    QString needle;
    for (const QChar character : query) {
        if (character.isSpace() || character == u'\'')
            continue;
        if ((character < u'a' || character > u'z') && (character < u'0' || character > u'9'))
            return 0;
        needle += character;
    }
    if (needle.isEmpty())
        return 0;
    if (matches(needle, name, 0, false))
        return 800;
    if (matches(needle, name, 0, true))
        return 750;
    for (qsizetype start = 1; start < name.size(); ++start) {
        if (matches(needle, name, start, false))
            return 600;
        if (matches(needle, name, start, true))
            return 550;
    }
    return 0;
}

} // namespace searchapp
