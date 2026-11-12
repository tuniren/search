#include "search_window.h"

#include "platform/windows_integration.h"
#include "result_delegate.h"
#include "result_model.h"
#include "theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace searchapp
{

SearchWindow::SearchWindow(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint), input_(new QLineEdit(this)),
      list_(new QListView(this)), model_(new ResultModel(this)), status_(new QLabel(this)),
      empty_(new QLabel(this)), hotkey_(new QLabel(this)), queryTimer_(new QTimer(this))
{
    setWindowTitle(QStringLiteral("search"));
    setWindowIcon(glyphIcon(Glyph::Search));
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumWidth(380);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 18, 18, 22);
    auto* surface = new QFrame(this);
    surface->setObjectName(QStringLiteral("launcherSurface"));
    outer->addWidget(surface);
    auto* content = new QVBoxLayout(surface);
    content->setSpacing(0);
    content->setContentsMargins(10, 7, 10, 8);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(13, 4, 7, 4);
    header->setSpacing(14);
    auto* searchIcon = new QLabel(surface);
    searchIcon->setPixmap(glyphIcon(Glyph::Search).pixmap(25, 25));
    header->addWidget(searchIcon);
    input_->setObjectName(QStringLiteral("searchInput"));
    input_->setPlaceholderText(QStringLiteral("搜索程序、设置或系统工具，支持拼音"));
    input_->setAccessibleName(QStringLiteral("搜索程序和系统入口"));
    input_->setMaxLength(512);
    input_->setClearButtonEnabled(true);
    input_->installEventFilter(this);
    header->addWidget(input_, 1);
    auto* settings = new QToolButton(surface);
    settings->setObjectName(QStringLiteral("settingsButton"));
    settings->setIcon(glyphIcon(Glyph::Settings));
    settings->setIconSize({22, 22});
    settings->setToolTip(QStringLiteral("设置（Ctrl+,）"));
    settings->setAccessibleName(QStringLiteral("打开设置"));
    connect(settings, &QToolButton::clicked, this, &SearchWindow::settingsRequested);
    header->addWidget(settings);
    content->addLayout(header);
    auto* separator = new QFrame(surface);
    separator->setObjectName(QStringLiteral("separator"));
    separator->setFixedHeight(1);
    content->addWidget(separator);

    list_->setObjectName(QStringLiteral("results"));
    list_->setAccessibleName(QStringLiteral("搜索结果"));
    list_->setModel(model_);
    list_->setItemDelegate(new ResultDelegate(list_));
    list_->setUniformItemSizes(true);
    list_->setMouseTracking(true);
    list_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setContextMenuPolicy(Qt::CustomContextMenu);
    list_->installEventFilter(this);
    connect(list_, &QListView::doubleClicked, this, [this] { activateSelection(); });
    connect(list_, &QWidget::customContextMenuRequested, this, &SearchWindow::contextMenu);
    content->addWidget(list_);
    empty_->setObjectName(QStringLiteral("muted"));
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setFixedHeight(116);
    empty_->setText(QStringLiteral("搜索程序、设置或系统工具\n打开后将自动加入常用列表"));
    content->addWidget(empty_);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(12, 8, 8, 2);
    footer->setSpacing(8);
    status_->setObjectName(QStringLiteral("muted"));
    status_->setText(QStringLiteral("search · 准备就绪"));
    QFont small = font();
    small.setPointSizeF(8);
    status_->setFont(small);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    footer->addWidget(status_, 1);
    for (const QString& text :
         {QStringLiteral("↑↓ 选择"), QStringLiteral("↵ 打开"), QStringLiteral("esc 关闭")}) {
        auto* label = new QLabel(text, surface);
        label->setObjectName(QStringLiteral("keyCap"));
        label->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        footer->addWidget(label);
    }
    hotkey_->setObjectName(QStringLiteral("muted"));
    hotkey_->setFont(small);
    hotkey_->hide();
    footer->addWidget(hotkey_);
    content->addLayout(footer);
    queryTimer_->setSingleShot(true);
    queryTimer_->setInterval(25);
    connect(input_, &QLineEdit::textChanged, this, [this] {
        if (!composing())
            queryTimer_->start();
    });
    connect(queryTimer_, &QTimer::timeout, this, [this] { emit queryChanged(input_->text()); });
    resize(716, 260);
    resizeToResults();
}

bool SearchWindow::composing() const
{
    return composing_;
}
QString SearchWindow::query() const
{
    return input_->text();
}

QString SearchWindow::selectedId() const
{
    const auto* result = model_->resultAt(list_->currentIndex().row());
    return result ? result->item.id : QString{};
}

void SearchWindow::summon()
{
    const QRect work = activationWorkArea();
    resize(std::max(380, std::min(716, work.width() - 40)), height());
    resizeToResults();
    move(work.center().x() - width() / 2, work.top() + std::max(10, (work.height() - height()) / 3));
    show();
    raise();
    activateWindow();
    foregroundWindow(winId());
    input_->setFocus(Qt::ShortcutFocusReason);
    input_->selectAll();
    applyWindowTheme(winId(), isDarkTheme());
    emit queryChanged(input_->text());
}

void SearchWindow::setResults(Results results)
{
    const QString selected = selectedId();
    model_->replace(std::move(results));
    const int row = model_->indexOf(selected);
    if (model_->rowCount())
        list_->setCurrentIndex(model_->index(std::max(0, row)));
    empty_->setText(input_->text().isEmpty()
                        ? QStringLiteral("搜索程序、设置或系统工具\n打开后将自动加入常用列表")
                        : QStringLiteral("没有匹配的程序或系统入口\n可尝试中文、英文、全拼或首字母"));
    resizeToResults();
}

void SearchWindow::resizeToResults()
{
    const int count = model_->rowCount();
    list_->setVisible(count > 0);
    empty_->setVisible(count == 0);
    list_->setFixedHeight(std::min(count, 7) * 68 + 4);
    if (layout()) {
        // Recompute the panel's minimum before the outer layout asks for it;
        // otherwise the previous result count keeps the window stretched.
        list_->parentWidget()->layout()->activate();
        layout()->invalidate();
        layout()->activate();
        setFixedHeight(sizeHint().height());
    }
}

void SearchWindow::clearQuery()
{
    input_->clear();
}

void SearchWindow::setStatus(const QString& status)
{
    status_->setText(status);
    status_->setToolTip(status);
}

void SearchWindow::setHotkeyLabel(const QString& shortcut)
{
    hotkey_->setText(shortcut);
    setToolTip(QStringLiteral("search · %1").arg(shortcut));
}

void SearchWindow::moveSelection(int delta)
{
    if (!model_->rowCount())
        return;
    const int next = std::clamp(list_->currentIndex().row() + delta, 0, model_->rowCount() - 1);
    list_->setCurrentIndex(model_->index(next));
    list_->scrollTo(model_->index(next));
}

void SearchWindow::activateSelection(bool reveal)
{
    if (composing())
        return;
    if (const auto* result = model_->resultAt(list_->currentIndex().row())) {
        // Copy before emitting: the receiver may synchronously replace the model.
        const auto item = result->item;
        if (reveal && !canRevealTarget(item))
            return;
        emit actionRequested(item, reveal);
    }
}

bool SearchWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == input_ && event->type() == QEvent::InputMethod) {
        const auto* input = static_cast<QInputMethodEvent*>(event);
        composing_ = !input->preeditString().isEmpty();
        if (!composing_)
            queryTimer_->start();
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (watched == input_ && composing())
            return QWidget::eventFilter(watched, event);
        if (key->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
        if (key->key() == Qt::Key_Comma && key->modifiers().testFlag(Qt::ControlModifier)) {
            emit settingsRequested();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            // Commit the current query first when the debounce has not elapsed.
            if (queryTimer_->isActive()) {
                queryTimer_->stop();
                emit queryChanged(input_->text());
                return true;
            }
            activateSelection(key->modifiers().testFlag(Qt::ShiftModifier));
            return true;
        }
        if (watched == input_ && key->key() == Qt::Key_Down) {
            moveSelection(1);
            return true;
        }
        if (watched == input_ && key->key() == Qt::Key_Up) {
            moveSelection(-1);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool SearchWindow::event(QEvent* event)
{
    if (event->type() == QEvent::WindowDeactivate && hideOnFocusLoss_) {
        QTimer::singleShot(120, this, [this] {
            if (!autoHideSuspended_ && !isActiveWindow() && !QApplication::activePopupWidget())
                hide();
        });
    }
    return QWidget::event(event);
}

void SearchWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
    hide();
}

void SearchWindow::paintEvent(QPaintEvent*)
{
    // Keep the shadow outside the live widget tree. A graphics effect on the
    // whole panel caches input and list pixels and breaks layered-window dirty
    // regions when scrolling or resizing on Windows.
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const QRectF panel = QRectF(rect()).adjusted(18, 18, -18, -22).translated(0, 6);
    for (int spread = 16; spread > 0; --spread) {
        painter.setBrush(QColor(0, 0, 0, spread > 8 ? 2 : 4));
        painter.drawRoundedRect(panel.adjusted(-spread, -spread, spread, spread), 16 + spread, 16 + spread);
    }
}

void SearchWindow::contextMenu(const QPoint& point)
{
    const auto index = list_->indexAt(point);
    const auto* selected = model_->resultAt(index.row());
    if (!selected)
        return;
    list_->setCurrentIndex(index);
    const CatalogItem item = selected->item;
    autoHideSuspended_ = true;
    QMenu menu(this);
    auto* open = menu.addAction(QStringLiteral("打开"));
    auto* reveal = menu.addAction(QStringLiteral("打开所在位置"));
    auto* copy = menu.addAction(canRevealTarget(item) ? QStringLiteral("复制程序路径")
                                                      : QStringLiteral("复制入口地址"));
    reveal->setEnabled(canRevealTarget(item));
    menu.addSeparator();
    auto* pin = menu.addAction(item.pinned ? QStringLiteral("取消固定") : QStringLiteral("固定到常用"));
    auto* alias = menu.addAction(QStringLiteral("设置搜索别名…"));
    const auto* chosen = menu.exec(list_->viewport()->mapToGlobal(point));
    if (chosen == open)
        emit actionRequested(item, false);
    else if (chosen == reveal)
        emit actionRequested(item, true);
    else if (chosen == copy)
        QApplication::clipboard()->setText(item.target);
    else if (chosen == pin)
        emit personalizeRequested(item, !item.pinned, item.alias);
    else if (chosen == alias) {
        bool ok = false;
        const auto value = QInputDialog::getText(
            this, QStringLiteral("搜索别名"), QStringLiteral("为 %1 输入别名（留空清除）：").arg(item.name),
            QLineEdit::Normal, item.alias, &ok);
        if (ok)
            emit personalizeRequested(item, item.pinned, value.trimmed().left(128));
    }
    autoHideSuspended_ = false;
}

} // namespace searchapp
