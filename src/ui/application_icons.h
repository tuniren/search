#pragma once

#include <QCache>
#include <QFutureWatcher>
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QThreadPool>

namespace searchapp
{

class ApplicationIcons final : public QObject
{
    Q_OBJECT
public:
    explicit ApplicationIcons(QObject* parent = nullptr);
    ~ApplicationIcons() override;
    QIcon icon(const QString& target) const;
    void request(const QStringList& targets);

signals:
    void ready(const QString& target);

private:
    void startNext();

    QCache<QString, QIcon> cache_{256};
    QThreadPool pool_;
    QFutureWatcher<QImage> watcher_;
    QStringList pending_;
    QString active_;
};

} // namespace searchapp
