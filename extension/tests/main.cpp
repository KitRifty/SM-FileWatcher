
#include <iostream>
#include <fstream>
#include <filesystem>
#include <shared_mutex>
#include "watcher.h"

namespace fs = std::filesystem;

class TestWatcher : public DirectoryWatcher
{
public:
    using DirectoryWatcher::DirectoryWatcher;

    fs::path basePath;

    virtual void OnProcessEvent(const NotifyEvent &event) override
    {
        switch (event.type)
        {
        case kFilesystem:
        {
            if (event.flags & kCreated)
            {
                std::cout << "Created " << fs::path(event.path).lexically_relative(basePath) << std::endl;
            }

            if (event.flags & kDeleted)
            {
                std::cout << "Deleted " << fs::path(event.path).lexically_relative(basePath) << std::endl;
            }

            if (event.flags & kModified)
            {
                std::cout << "Modified " << fs::path(event.path).lexically_relative(basePath) << std::endl;
            }

            if (event.flags & kRenamed)
            {
                std::cout << "Renamed " << fs::path(event.lastPath).lexically_relative(basePath) << " to " << fs::path(event.path).lexically_relative(basePath) << std::endl;
            }
            break;
        }
        case kStart:
        {
            std::cout << "Started watching " << fs::path(event.path).lexically_relative(basePath) << std::endl;
            break;
        }
        case kStop:
        {
            std::cout << "Stopped watching " << fs::path(event.path).lexically_relative(basePath) << std::endl;
            break;
        }
        }
    }
};

bool IsSubPath(const fs::path &base, const fs::path &child)
{
    auto relative = child.lexically_relative(base);
    return !relative.empty() && *relative.begin() != "..";
}

class TempDir
{
public:
    TempDir(const fs::path &filename) : path(filename)
    {
        if (IsSubPath(fs::temp_directory_path(), filename) && fs::create_directories(filename))
        {
            path = filename;
        }
        else
        {
            throw std::runtime_error("Failed to create directory " + filename.string());
        }
    }

    ~TempDir()
    {
        if (!path.empty())
        {
            fs::remove_all(path);
        }
    }

    fs::path path;
};

int main()
{
    TempDir watchedDir(std::tmpnam(nullptr));
    TempDir watchedSymDir(std::tmpnam(nullptr));

    DirectoryWatcher::WatchOptions options;
    options.subtree = true;
    options.symlinks = true;
    options.bufferSize = 8192;
    options.retryInterval = 1000;
    options.notifyFilterFlags = DirectoryWatcher::kNotifyAll;

    TestWatcher watcher;
    watcher.basePath = watchedDir.path;

    if (!watcher.Watch(watchedDir.path, options))
    {
        std::cerr << "Failed to start watching " << watchedDir.path << std::endl;
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    watcher.ProcessEvents();

    fs::create_directories(watchedDir.path / "subdir1");
    fs::create_directories(watchedDir.path / "subdir2" / "subdir21");
    fs::rename(watchedDir.path / "subdir2" / "subdir21", watchedDir.path / "subdir2" / "another_subdir");
    fs::create_directory_symlink(watchedSymDir.path, watchedDir.path / "sym_link_dir");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    watcher.ProcessEvents();

    fs::create_directories(watchedSymDir.path / "subdir");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    watcher.ProcessEvents();

    fs::remove(watchedSymDir.path / "subdir");

    watcher.StopWatching();

    // cannot remove watched symlinked subdirectories on Windows
    fs::remove(watchedDir.path / "sym_link_dir");

    watcher.Watch(watchedDir.path, options);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    watcher.ProcessEvents();

    return 0;
}