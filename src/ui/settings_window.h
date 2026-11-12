#pragma once

#include "storage/settings.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;

namespace searchapp
{

class SettingsWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget* parent = nullptr);
    void setSettings(const Settings& settings);
    void setStatus(const QString& status);
    void setError(const QString& error);
    void showSaved();

signals:
    void saveRequested(const Settings& settings);
    void refreshRequested();
    void clearHistoryRequested();

private:
    void collectAndSave();

    Settings original_;
    QKeySequenceEdit* shortcut_;
    QCheckBox* launchAtLogin_;
    QCheckBox* hideOnFocusLoss_;
    QComboBox* theme_;
    QLabel* status_;
    QLabel* error_;
};

} // namespace searchapp
