#include "result_delegate.h"

#include "result_model.h"
#include "theme.h"

#include <QPainter>

namespace searchapp
{

ResultDelegate::ResultDelegate(QObject* parent) : QStyledItemDelegate(parent) {}

QSize ResultDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return {400, 68};
}

void ResultDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                           const QModelIndex& index) const
{
    const auto* model = qobject_cast<const ResultModel*>(index.model());
    const auto* result = model ? model->resultAt(index.row()) : nullptr;
    if (!result)
        return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const bool dark = isDarkTheme();
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const QRect row = option.rect.adjusted(2, 2, -2, -2);
    if (selected || option.state.testFlag(QStyle::State_MouseOver)) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? (dark ? QColor("#2c3a5b") : QColor("#eef3ff"))
                                   : option.palette.color(QPalette::AlternateBase));
        painter->drawRoundedRect(row, 9, 9);
    }
    const QRect iconRect(row.left() + 12, row.top() + 12, 40, 40);
    const auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    if (!icon.isNull()) {
        icon.paint(painter, iconRect.adjusted(2, 2, -2, -2));
    } else {
        painter->setPen(Qt::NoPen);
        painter->setBrush(dark ? QColor("#333d51") : QColor("#e5ecff"));
        painter->drawRoundedRect(iconRect, 10, 10);
        glyphIcon(Glyph::Application, dark ? QColor("#96b3ff") : QColor("#4770eb"))
            .paint(painter, iconRect.adjusted(8, 8, -8, -8));
    }
    const int textLeft = iconRect.right() + 13;
    const int textWidth = std::max(0, row.width() - 155);
    QFont titleFont = option.font;
    titleFont.setPointSizeF(10.5);
    titleFont.setWeight(QFont::Medium);
    painter->setFont(titleFont);
    painter->setPen(option.palette.color(QPalette::Text));
    QString title = result->item.name;
    if (result->item.pinned)
        title.prepend(QStringLiteral("★ "));
    painter->drawText(QRect(textLeft, row.top() + 10, textWidth, 24), Qt::AlignVCenter,
                      QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, textWidth));
    QFont subtitleFont = option.font;
    subtitleFont.setPointSizeF(8.5);
    painter->setFont(subtitleFont);
    painter->setPen(option.palette.color(QPalette::PlaceholderText));
    painter->drawText(QRect(textLeft, row.top() + 35, textWidth, 20), Qt::AlignVCenter,
                      QFontMetrics(subtitleFont).elidedText(result->subtitle, Qt::ElideMiddle, textWidth));
    painter->drawText(row.adjusted(0, 0, -14, 0), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("程序"));
    painter->restore();
}

} // namespace searchapp
