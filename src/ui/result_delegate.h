#pragma once

#include <QStyledItemDelegate>

namespace searchapp
{

class ResultDelegate final : public QStyledItemDelegate
{
public:
    explicit ResultDelegate(QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override;
};

} // namespace searchapp
