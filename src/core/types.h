#pragma once

#include "pinyin.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace searchapp
{

struct CatalogItem {
    QString id;
    QString name;
    QString target;
    int launches = 0;
    qint64 lastUsed = 0;
    bool pinned = false;
    QString alias;
    QStringList searchAliases;
    QVector<PinyinName> pinyinNames;
};

struct SearchResult {
    CatalogItem item;
    QString subtitle;
    int score = 0;
};

using Catalog = QVector<CatalogItem>;
using Results = QVector<SearchResult>;

} // namespace searchapp
