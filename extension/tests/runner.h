#include "watcher.h"

class WatchEventCollector : public DirectoryWatcher
{
public:
    using DirectoryWatcher::DirectoryWatcher;
    virtual void OnProcessEvent(const NotifyEvent &event) override;

    std::vector<NotifyEvent> events;
};

class TempDir
{
public:
    TempDir();
    ~TempDir();

    inline const std::filesystem::path& GetPath()
    {
        return path;
    }

private:
    std::filesystem::path path;
};