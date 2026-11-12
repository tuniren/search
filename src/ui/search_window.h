#pragma once

#include "core/types.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListView;
class QTimer;

namespace searchapp
{

class ResultModel;

class SearchWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit SearchWindow(QWidget* parent = nullptr);
    void summon();
    void clearQuery();
    void setResults(Results results);
    void setStatus(const QString& status);
    void setHotkeyLabel(const QString& shortcut);
    void setHideOnFocusLoss(bool enabled)
    {
        hideOnFocusLoss_ = enabled;
    }
    QString query() const;
    QString selectedId() const;
    bool composing() const;

signals:
    void queryChanged(const QString& query);
    void settingsRequested();
    void actionRequested(const CatalogItem& item, bool reveal);
    void personalizeRequested(const CatalogItem& item, bool pinned, const QString& alias);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool event(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void activateSelection(bool reveal = false);
    void contextMenu(const QPoint& point);
    void moveSelection(int delta);
    void resizeToResults();

    QLineEdit* input_;
    QListView* list_;
    ResultModel* model_;
    QLabel* status_;
    QLabel* empty_;
    QLabel* hotkey_;
    QTimer* queryTimer_;
    bool hideOnFocusLoss_ = true;
    bool autoHideSuspended_ = false;
    bool composing_ = false;
};

} // namespace searchapp
