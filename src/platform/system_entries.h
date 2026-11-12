#pragma once

#include "core/types.h"

namespace searchapp
{

struct SystemEntry {
    QString id;
    QString name;
    QString target;
    QStringList aliases;
    ItemKind kind;
};

const SystemEntry* systemEntry(const QString& target);
Catalog discoverSystemEntries();

} // namespace searchapp
