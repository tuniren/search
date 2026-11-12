#pragma once

#include "application_icons.h"
#include "core/types.h"

#include <QAbstractListModel>

namespace searchapp
{

class ResultModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit ResultModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    void replace(Results results);
    const SearchResult* resultAt(int row) const;
    int indexOf(const QString& id) const;

private:
    Results results_;
    ApplicationIcons icons_;
};

} // namespace searchapp
