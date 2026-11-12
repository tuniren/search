#pragma once

#include <QStringList>
#include <QVector>

namespace searchapp
{

// Retain all character readings without expanding their Cartesian product.
using PinyinName = QVector<QStringList>;
PinyinName pinyinName(const QString& name);
int pinyinScore(const QString& query, const PinyinName& name);

} // namespace searchapp
