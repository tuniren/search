#pragma once

#include "pinyin.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace searchapp
{

enum class ItemKind { Application, Settings, ControlPanel, SystemTool };

inline QString itemKindName(ItemKind kind)
{
    switch (kind) {
    case ItemKind::Settings:
        return QStringLiteral("设置");
    case ItemKind::ControlPanel:
        return QStringLiteral("控制面板");
    case ItemKind::SystemTool:
        return QStringLiteral("系统工具");
    default:
        return QStringLiteral("程序");
    }
}

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
    ItemKind kind = ItemKind::Application;
};

struct SearchResult {
    CatalogItem item;
    QString subtitle;
    int score = 0;
};

using Catalog = QVector<CatalogItem>;
using Results = QVector<SearchResult>;

} // namespace searchapp
