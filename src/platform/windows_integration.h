#pragma once

#include "core/types.h"

#include <QAbstractNativeEventFilter>
#include <QImage>
#include <QKeySequence>
#include <QObject>
#include <QRect>
#include <atomic>
#include <memory>

namespace searchapp
{

QString pathIdentity(const QString& path);
QStringList startMenuDirectories();
Catalog discoverStartMenuApplications(const QStringList& directories,
                                      const std::shared_ptr<std::atomic_bool>& canceled);
Catalog discoverApplications(const std::shared_ptr<std::atomic_bool>& canceled);
QStringList systemApplicationAliases(const QString& target);
void prepareApplications(Catalog& items);
QImage applicationIcon(const QString& target, int size);
bool openTarget(const CatalogItem& item);
bool canRevealTarget(const CatalogItem& item);
bool revealTarget(const CatalogItem& item);
bool setLaunchAtLogin(bool enabled, QString* error);
QRect activationWorkArea();
void foregroundWindow(quintptr windowId);
void applyWindowTheme(quintptr windowId, bool dark);

class GlobalHotkey final : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;
    bool setShortcut(const QKeySequence& shortcut, QString* error);
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void activated();

private:
    int activeId_ = 0;
    QKeySequence shortcut_;
};

} // namespace searchapp
