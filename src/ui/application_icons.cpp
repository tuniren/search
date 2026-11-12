#include "application_icons.h"

#include "platform/windows_integration.h"

#include <QPixmap>
#include <QtConcurrentRun>
#include <utility>

namespace searchapp
{

ApplicationIcons::ApplicationIcons(QObject* parent) : QObject(parent)
{
    pool_.setMaxThreadCount(1);
    connect(&watcher_, &QFutureWatcher<QImage>::finished, this, [this] {
        const auto target = std::exchange(active_, {});
        const auto image = watcher_.result();
        // QPixmap and QIcon are created only on the GUI thread. Cache misses
        // too, so broken shortcuts do not repeatedly invoke Shell extensions.
        cache_.insert(target, new QIcon(image.isNull() ? QIcon{} : QIcon(QPixmap::fromImage(image))));
        emit ready(target);
        startNext();
    });
}

ApplicationIcons::~ApplicationIcons()
{
    pool_.waitForDone();
}

QIcon ApplicationIcons::icon(const QString& target) const
{
    const auto* cached = cache_.object(target);
    return cached ? *cached : QIcon{};
}

void ApplicationIcons::request(const QStringList& targets)
{
    // Replace queued requests with the current results, rather than allowing
    // rapid typing to build a backlog of obsolete Shell lookups.
    pending_.clear();
    for (const auto& target : targets)
        if (target != active_ && !cache_.contains(target) && !pending_.contains(target))
            pending_.push_back(target);
    startNext();
}

void ApplicationIcons::startNext()
{
    if (!active_.isEmpty() || pending_.isEmpty())
        return;
    active_ = pending_.takeFirst();
    watcher_.setFuture(QtConcurrent::run(&pool_, [target = active_] { return applicationIcon(target, 96); }));
}

} // namespace searchapp
