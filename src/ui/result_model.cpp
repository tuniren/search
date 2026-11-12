#include "result_model.h"

namespace searchapp
{

ResultModel::ResultModel(QObject* parent) : QAbstractListModel(parent)
{
    connect(&icons_, &ApplicationIcons::ready, this, [this](const QString& target) {
        for (int row = 0; row < rowCount(); ++row)
            if (results_[row].item.target == target)
                emit dataChanged(index(row), index(row), {Qt::DecorationRole});
    });
}

int ResultModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(results_.size());
}

QVariant ResultModel::data(const QModelIndex& index, int role) const
{
    const auto* result = resultAt(index.row());
    if (!index.isValid() || !result)
        return {};
    switch (role) {
    case Qt::DisplayRole:
        return result->item.name;
    case Qt::DecorationRole:
        return icons_.icon(result->item.target);
    case Qt::ToolTipRole:
        return result->subtitle;
    case Qt::AccessibleTextRole:
        return result->item.name;
    case Qt::AccessibleDescriptionRole:
        return result->subtitle;
    default:
        return {};
    }
}

void ResultModel::replace(Results results)
{
    beginResetModel();
    results_ = std::move(results);
    endResetModel();
    QStringList targets;
    for (const auto& result : results_)
        targets.push_back(result.item.target);
    icons_.request(targets);
}

const SearchResult* ResultModel::resultAt(int row) const
{
    return row >= 0 && row < results_.size() ? &results_[row] : nullptr;
}

int ResultModel::indexOf(const QString& id) const
{
    for (qsizetype i = 0; i < results_.size(); ++i)
        if (results_[i].item.id == id)
            return static_cast<int>(i);
    return -1;
}

} // namespace searchapp
