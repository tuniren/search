#pragma once

#include <QObject>
#include <QStringList>
#include <memory>
#include <vector>

namespace searchapp
{

// Windows subtree notifications. No per-subdirectory Qt watcher handles.
class DirectoryMonitor final : public QObject
{
    Q_OBJECT
public:
    explicit DirectoryMonitor(QObject* parent = nullptr);
    ~DirectoryMonitor() override;
    void setRoots(const QStringList& roots);

signals:
    void changed();

private:
    struct Watch;
    std::vector<std::unique_ptr<Watch>> watches_;
};

} // namespace searchapp
