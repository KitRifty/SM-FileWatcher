
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
            if (event.path == basePath)
            {
                std::cout << "Started watching" << std::endl;
            }

            break;
        }
        case kStop:
        {
            if (event.path == basePath)
            {
                std::cout << "Stopped watching" << std::endl;
            }

            break;
        }
        }
    }
};

class TempDir
{
public:
#ifdef __linux__
    TempDir()
    {
        char temp[] = "/tmp/watchertestXXXXXX";
        char *p = mkdtemp(temp);

        if (p != nullptr)
        {
            path = p;
        }
    }
#else
    TempDir(const fs::path &filename) : path(filename)
    {
        if (IsSubPath(fs::temp_directory_path(), filename) && fs::create_directories(filename))
        {
            path = filename;
        }
        else
        {
            std::cerr << "Failed to create directory " << filename << std::endl;
            exit(1);
        }
    }
#endif

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
#ifdef __linux__
    std::string expected = "Started watching"
                           "\n"
                           "Created \"subdir1\""
                           "\n"
                           "Created \"subdir2\""
                           "\n"
                           "Created \"subdir2/subdir21\""
                           "\n"
                           "Renamed \"subdir2/subdir21\" to \"subdir2/another_subdir\""
                           "\n"
                           "Created \"subdir1/new_file\""
                           "\n"
                           "Modified \"subdir1/new_file\""
                           "\n"
                           "Created \"sym_link_dir\""
                           "\n"
                           "Deleted \"subdir1/new_file\""
                           "\n"
                           "Created \"sym_link_dir/subdir\""
                           "\n"
                           "Created \"sym_link_dir/subdir/new_file_in_sym\""
                           "\n"
                           "Modified \"sym_link_dir/subdir/new_file_in_sym\""
                           "\n"
                           "Deleted \"sym_link_dir\""
                           "\n"
                           "Stopped watching"
                           "\n";
#else
    std::string expected = "Started watching"
                           "\n"
                           "Created \"subdir1\""
                           "\n"
                           "Created \"subdir2\""
                           "\n"
                           "Created \"subdir2\\subdir21\""
                           "\n"
                           "Renamed \"subdir2\\subdir21\" to \"subdir2\\another_subdir\""
                           "\n"
                           "Created \"subdir1\\new_file\""
                           "\n"
                           "Modified \"subdir1\\new_file\""
                           "\n"
                           "Created \"sym_link_dir\""
                           "\n"
                           "Deleted \"subdir1\\new_file\""
                           "\n"
                           "Created \"sym_link_dir\\subdir\""
                           "\n"
                           "Created \"sym_link_dir\\subdir\\new_file_in_sym\""
                           "\n"
                           "Modified \"sym_link_dir\\subdir\\new_file_in_sym\""
                           "\n"
                           "Stopped watching"
                           "\n"
                           "Started watching"
                           "\n"
                           "Stopped watching"
                           "\n";
#endif

    std::stringstream output;
    std::streambuf *old_stdout = std::cout.rdbuf(output.rdbuf());

#ifdef __linux__
    TempDir watchedDir;
    TempDir watchedSymDir;
#else
    TempDir watchedDir(std::tmpnam(nullptr));
    TempDir watchedSymDir(std::tmpnam(nullptr));
#endif

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

    auto file = std::ofstream(watchedDir.path / "subdir1" / "new_file");
    file.close();

    fs::create_directory_symlink(watchedSymDir.path, watchedDir.path / "sym_link_dir");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    watcher.ProcessEvents();

    fs::remove(watchedDir.path / "subdir1" / "new_file");

    fs::create_directories(watchedSymDir.path / "subdir");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    watcher.ProcessEvents();

    file = std::ofstream(watchedSymDir.path / "subdir" / "new_file_in_sym");
    file.close();

#ifdef __linux__
    fs::remove(watchedDir.path / "sym_link_dir");
#else
    // cannot remove watched symlinked subdirectories on Windows
    watcher.StopWatching();
    fs::remove(watchedDir.path / "sym_link_dir");
    watcher.Watch(watchedDir.path, options);
#endif

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    watcher.ProcessEvents();
    watcher.StopWatching();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    watcher.ProcessEvents();

    std::string out = output.str();
    std::cout.rdbuf(old_stdout);

    if (out != expected)
    {
        std::cerr << "FAIL!" << std::endl
                  << "Expected:" << std::endl
                  << expected << std::endl
                  << "Got:" << std::endl
                  << out << std::endl;
        return 1;
    }

    std::cout << "PASS!" << std::endl;

    return 0;
}