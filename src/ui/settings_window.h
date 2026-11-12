#pragma once

#include "storage/settings.h"
#include "update/update_service.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

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
    void setUpdateStatus(const UpdateStatus& status);
    void showUpdates();

signals:
    void saveRequested(const Settings& settings);
    void refreshRequested();
    void clearHistoryRequested();
    void checkUpdateRequested();
    void downloadUpdateRequested();
    void cancelUpdateRequested();
    void installUpdateRequested();

private:
    void collectAndSave();

    Settings original_;
    QKeySequenceEdit* shortcut_;
    QCheckBox* launchAtLogin_;
    QCheckBox* hideOnFocusLoss_;
    QCheckBox* checkForUpdates_;
    QComboBox* theme_;
    QLabel* status_;
    QLabel* error_;
    QLabel* updateMessage_;
    QPlainTextEdit* releaseNotes_;
    QProgressBar* updateProgress_;
    QPushButton* checkUpdate_;
    QPushButton* updateAction_;
    QPushButton* cancelUpdate_;
    UpdateState updateState_ = UpdateState::Idle;
};

} // namespace searchapp
