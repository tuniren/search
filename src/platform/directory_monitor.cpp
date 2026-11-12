#include "directory_monitor.h"

#include <QDir>
#include <QMetaObject>
#include <Windows.h>
#include <array>
#include <thread>

namespace searchapp
{

struct DirectoryMonitor::Watch {
    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread thread;

    ~Watch()
    {
        if (stop)
            SetEvent(stop);
        if (thread.joinable())
            thread.join();
        if (directory != INVALID_HANDLE_VALUE)
            CloseHandle(directory);
        if (stop)
            CloseHandle(stop);
    }
};

DirectoryMonitor::DirectoryMonitor(QObject* parent) : QObject(parent) {}
DirectoryMonitor::~DirectoryMonitor() = default;

void DirectoryMonitor::setRoots(const QStringList& roots)
{
    watches_.clear();
    for (const auto& root : roots) {
        auto watch = std::make_unique<Watch>();
        const QString path = QDir::toNativeSeparators(root);
        watch->directory =
            CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), FILE_LIST_DIRECTORY,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (watch->directory == INVALID_HANDLE_VALUE || !watch->stop)
            continue;
        auto* state = watch.get();
        watch->thread = std::thread([this, state] {
            alignas(DWORD) std::array<std::byte, 32 * 1024> buffer{};
            OVERLAPPED operation{};
            operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!operation.hEvent)
                return;
            HANDLE events[] = {state->stop, operation.hEvent};
            while (WaitForSingleObject(state->stop, 0) != WAIT_OBJECT_0) {
                ResetEvent(operation.hEvent);
                if (!ReadDirectoryChangesW(state->directory, buffer.data(), static_cast<DWORD>(buffer.size()),
                                           TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME,
                                           nullptr, &operation, nullptr))
                    break;
                const DWORD signaled = WaitForMultipleObjects(2, events, FALSE, INFINITE);
                if (signaled != WAIT_OBJECT_0 + 1) {
                    CancelIoEx(state->directory, &operation);
                    DWORD transferred = 0;
                    GetOverlappedResult(state->directory, &operation, &transferred, TRUE);
                    break;
                }
                DWORD transferred = 0;
                GetOverlappedResult(state->directory, &operation, &transferred, FALSE);
                // Both normal changes and buffer overflow invalidate the snapshot.
                QMetaObject::invokeMethod(this, [this] { emit changed(); }, Qt::QueuedConnection);
            }
            CloseHandle(operation.hEvent);
        });
        watches_.push_back(std::move(watch));
    }
}

} // namespace searchapp
