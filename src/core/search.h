#pragma once

#include "types.h"

#include <atomic>
#include <memory>

namespace searchapp
{

QString normalized(QString text);
void prepareSearch(CatalogItem& item);
int matchScore(const QString& query, const QString& candidate);
Results search(const Catalog& catalog, QString query, const std::shared_ptr<std::atomic_bool>& canceled = {});

} // namespace searchapp
