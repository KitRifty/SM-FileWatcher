/**
 * vim: set ts=4 :
 * =============================================================================
 * FileWatcher Extension
 * Copyright (C) 2022 KitRifty  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, AlliedModders LLC gives you permission to link the
 * code of this program (as well as its derivative works) to "Half-Life 2," the
 * "Source Engine," the "SourcePawn JIT," and any Game MODs that run on software
 * by the Valve Corporation.  You must obey the GNU General Public License in
 * all respects for all other code used.  Additionally, AlliedModders LLC grants
 * this exception to all derivative works.  AlliedModders LLC defines further
 * exceptions, found in LICENSE.txt (as of this writing, version JULY-31-2007),
 * or <http://www.sourcemod.net/license.php>.
 */

#include "watcher.h"

#include <string>

#ifdef __linux__
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/eventfd.h>
#include <unistd.h>
#else
#endif

#include <iostream>

namespace fs = std::filesystem;

DirectoryWatcher::Worker::Worker(
    bool isRoot,
    const std::filesystem::path &path,
    const WatchOptions &_options,
    EventQueue *eventsBuffer,
    std::mutex *eventsBufferMutex) : isRootWorker(isRoot),
                                     basePath(path.lexically_normal()),
                                     eventsBuffer(eventsBuffer),
                                     eventsBufferMutex(eventsBufferMutex),
                                     options(_options)
{
#ifdef __linux__
    fileDescriptor = inotify_init1(IN_NONBLOCK);
    cancelEvent = eventfd(0, 0);

    if (fileDescriptor != -1)
    {
        AddDirectory(basePath);
    }

    thread = std::thread(&DirectoryWatcher::Worker::ThreadProc, this);

#else
    auto actualBasePath = fs::path(basePath);
    if (fs::is_symlink(actualBasePath))
    {
        actualBasePath = fs::read_symlink(actualBasePath);
    }

    directory = CreateFileA(
        actualBasePath.string().c_str(),
        FILE_LIST_DIRECTORY | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (directory == INVALID_HANDLE_VALUE)
    {
        cancelEvent = 0;
        return;
    }

    cancelEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    if (options.subtree)
    {
        if (options.symlinks)
        {
            std::queue<fs::path> dirsToTraverse;
            dirsToTraverse.push(basePath);

            while (!dirsToTraverse.empty())
            {
                fs::path currentDir = dirsToTraverse.front();
                dirsToTraverse.pop();

                for (const auto &entry : fs::directory_iterator(currentDir))
                {
                    if (fs::is_directory(entry))
                    {
                        if (fs::is_symlink(entry))
                        {
                            workers.push_back(std::make_unique<Worker>(false, fs::path(entry), options, eventsBuffer, eventsBufferMutex));
                        }
                        else
                        {
                            dirsToTraverse.push(entry);
                        }
                    }
                }
            }
        }
    }

    thread = std::thread(&DirectoryWatcher::Worker::ThreadProc, this);
#endif
}

DirectoryWatcher::Worker::~Worker()
{
#ifdef __linux__
#else
    workers.clear();
#endif

#ifdef __linux__
    uint64_t u = 1;
    write(cancelEvent, &u, sizeof(u));
#else
    SetEvent(cancelEvent);
#endif

    if (thread.joinable())
    {
        thread.join();
    }

#ifdef __linux__
    if (fcntl(cancelEvent, F_GETFD) != -1)
    {
        close(cancelEvent);
    }

    if (fcntl(fileDescriptor, F_GETFD) != -1)
    {
        for (auto it = watchDescriptors.begin(); it != watchDescriptors.end(); it++)
        {
            inotify_rm_watch(fileDescriptor, it->first);
        }

        close(fileDescriptor);
    }
#endif
}

#ifdef __linux__
int DirectoryWatcher::Worker::AddDirectory(const std::filesystem::path &path)
{
    int wd = inotify_add_watch(fileDescriptor, path.string().c_str(), IN_CREATE | IN_MOVE | IN_DELETE | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF);
    if (wd != -1)
    {
        watchDescriptors.insert_or_assign(wd, fs::path(path));

        if (options.subtree)
        {
            for (const auto &entry : fs::directory_iterator(path, fs::directory_options::skip_permission_denied))
            {
                if (entry.is_directory())
                {
                    if (!options.symlinks && entry.is_symlink())
                    {
                        continue;
                    }

                    AddDirectory(entry);
                }
            }
        }
    }

    return wd;
}
#endif

void DirectoryWatcher::Worker::ThreadProc()
{
#ifdef __linux__
    auto buffer = std::make_unique<char[]>(options.bufferSize);

    pollfd fds[2];

    fds[0].fd = fileDescriptor;
    fds[0].events = POLLIN;

    fds[1].fd = cancelEvent;
    fds[1].events = POLLIN;

#else
    auto buffer = std::make_unique<char[]>(options.bufferSize);
    ScopedHandle watchEvent(CreateEvent(nullptr, TRUE, FALSE, nullptr));

    HANDLE waitHandles[2];
    waitHandles[0] = cancelEvent;
    waitHandles[1] = watchEvent;

    OVERLAPPED overlapped;
    ZeroMemory(&overlapped, sizeof overlapped);
    overlapped.hEvent = watchEvent;
#endif

    if (isRootWorker)
    {
        std::lock_guard<std::mutex> lock(*eventsBufferMutex);

        auto change = std::make_unique<NotifyEvent>();
        change->type = kStart;
        change->path = basePath.string();

        eventsBuffer->push(std::move(change));
    }

#ifdef __linux__
    while (poll(fds, 2, -1) >= 0)
    {
        if (fds[0].revents & POLLERR || fds[1].revents & POLLERR)
        {
            break;
        }

        if (fds[1].revents & POLLIN)
        {
            break;
        }

        if (fds[0].revents & POLLIN)
        {
            std::vector<std::unique_ptr<NotifyEvent>> queuedEvents;

            for (;;)
            {
                ssize_t len = read(fileDescriptor, buffer.get(), options.bufferSize);

                if (len == -1 && errno != EAGAIN)
                {
                    goto end_event_loop;
                }

                if (len <= 0)
                {
                    break;
                }

                const inotify_event *event;
                for (char *p = buffer.get(); p < buffer.get() + len; p += sizeof(inotify_event) + event->len)
                {
                    event = (const inotify_event *)p;

                    if (event->mask & (IN_MOVE_SELF | IN_DELETE_SELF))
                    {
                        auto it = watchDescriptors.find(event->wd);
                        if (it != watchDescriptors.end())
                        {
                            fs::path watchPath(it->second);

                            for (auto jt = watchDescriptors.begin(); jt != watchDescriptors.end();)
                            {
                                if (jt->first == event->wd || IsSubPath(watchPath, jt->second))
                                {
                                    inotify_rm_watch(fileDescriptor, jt->first);
                                    jt = watchDescriptors.erase(jt);
                                }

                                if (jt != watchDescriptors.end())
                                {
                                    jt++;
                                }
                            }
                        }

                        continue;
                    }

                    if (event->mask & IN_IGNORED)
                    {
                        continue;
                    }

                    auto &baseRelPath = watchDescriptors.at(event->wd);

                    if (event->mask & (IN_CREATE | IN_MOVED_TO))
                    {
                        if (options.subtree)
                        {
                            fs::path relPath = baseRelPath / event->name;

                            if ((event->mask & IN_ISDIR) ||
                                (options.symlinks && fs::is_symlink(relPath) && fs::is_directory(relPath)))
                            {
                                AddDirectory(relPath);
                            }
                        }

                        if (event->mask & IN_MOVED_TO)
                        {
                            bool foundRenamedEvent = false;

                            for (auto it = queuedEvents.rbegin(); it != queuedEvents.rend(); it++)
                            {
                                auto &change = *it;
                                if (change->cookie == event->cookie)
                                {
                                    change->flags = kRenamed;
                                    change->cookie = 0;
                                    change->lastPath = change->path;
                                    change->path = baseRelPath / event->name;

                                    foundRenamedEvent = true;
                                    break;
                                }
                            }

                            if (foundRenamedEvent)
                            {
                                continue;
                            }
                        }

                        std::unique_ptr<NotifyEvent> change = std::make_unique<NotifyEvent>();
                        change->type = kFilesystem;
                        change->flags = kCreated;
                        change->cookie = event->cookie;
                        change->path = baseRelPath / event->name;

                        queuedEvents.push_back(std::move(change));
                    }

                    if (event->mask & (IN_DELETE | IN_MOVED_FROM))
                    {
                        if (event->mask & IN_MOVED_FROM)
                        {
                            bool foundRenamedEvent = false;

                            for (auto it = queuedEvents.rbegin(); it != queuedEvents.rend(); it++)
                            {
                                auto &change = *it;
                                if (change->cookie == event->cookie)
                                {
                                    change->flags = kRenamed;
                                    change->cookie = 0;
                                    change->lastPath = change->path;
                                    change->path = baseRelPath / event->name;

                                    foundRenamedEvent = true;
                                    break;
                                }
                            }

                            if (foundRenamedEvent)
                            {
                                continue;
                            }
                        }

                        std::unique_ptr<NotifyEvent> change = std::make_unique<NotifyEvent>();
                        change->type = kFilesystem;
                        change->flags = kDeleted;
                        change->cookie = event->cookie;
                        change->path = baseRelPath / event->name;

                        queuedEvents.push_back(std::move(change));
                    }

                    if (event->mask & IN_CLOSE_WRITE)
                    {
                        std::unique_ptr<NotifyEvent> change = std::make_unique<NotifyEvent>();
                        change->type = kFilesystem;
                        change->flags = kModified;
                        change->cookie = event->cookie;
                        change->path = baseRelPath / event->name;

                        queuedEvents.push_back(std::move(change));
                    }
                }
            }

            std::lock_guard<std::mutex> lock(*eventsBufferMutex);

            for (auto it = queuedEvents.begin(); it != queuedEvents.end(); it++)
            {
                auto &change = *it;
                if (change->flags & options.notifyFilterFlags)
                {
                    eventsBuffer->push(std::move(*it));
                }
            }

            if (watchDescriptors.size() == 0)
            {
                break;
            }
        }
    }

end_event_loop:

#else
    bool running = true;

    while (running)
    {
        if (!ReadDirectoryChangesExW(
                directory,
                buffer.get(),
                options.bufferSize,
                options.subtree,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                nullptr,
                &overlapped,
                nullptr,
                ReadDirectoryNotifyExtendedInformation))
        {
            running = false;
            break;
        }

        switch (WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE))
        {
        case WAIT_OBJECT_0 + 1:
        {
            DWORD dwBytes = 0;
            if (!GetOverlappedResult(directory, &overlapped, &dwBytes, TRUE))
            {
                running = false;
                break;
            }

            ResetEvent(watchEvent);

            if (dwBytes == 0)
            {
                break;
            }

            std::vector<std::unique_ptr<NotifyEvent>> queuedEvents;

            char *p = buffer.get();
            for (;;)
            {
                FILE_NOTIFY_EXTENDED_INFORMATION *info = reinterpret_cast<FILE_NOTIFY_EXTENDED_INFORMATION *>(p);
                std::wstring fileName(info->FileName, info->FileNameLength / sizeof(wchar_t));
                fs::path path = basePath / fileName;

                switch (info->Action)
                {
                case FILE_ACTION_ADDED:
                {
                    auto change = std::make_unique<NotifyEvent>();
                    change->type = kFilesystem;
                    change->flags = kCreated;
                    change->path = path.string();

                    queuedEvents.push_back(std::move(change));

                    if (options.subtree && options.symlinks && fs::is_symlink(path) && fs::is_directory(path))
                    {
                        workers.push_back(std::make_unique<Worker>(false, path, options, eventsBuffer, eventsBufferMutex));
                    }

                    break;
                }
                case FILE_ACTION_REMOVED:
                {
                    auto change = std::make_unique<NotifyEvent>();
                    change->type = kFilesystem;
                    change->flags = kDeleted;
                    change->path = path.string();

                    queuedEvents.push_back(std::move(change));

                    if (options.subtree)
                    {
                        for (auto it = workers.begin(); it != workers.end(); it++)
                        {
                            auto &worker = *it;

                            if (!worker->IsRunning() || worker->basePath == path)
                            {
                                it = workers.erase(it);
                            }
                        }
                    }

                    break;
                }
                case FILE_ACTION_MODIFIED:
                {
                    if (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    {
                        break;
                    }

                    auto change = std::make_unique<NotifyEvent>();
                    change->type = kFilesystem;
                    change->flags = kModified;
                    change->path = path.string();

                    queuedEvents.push_back(std::move(change));

                    break;
                }
                case FILE_ACTION_RENAMED_OLD_NAME:
                {
                    auto change = std::make_unique<NotifyEvent>();
                    change->type = kFilesystem;
                    change->flags = kRenamed;
                    change->lastPath = path.string();

                    queuedEvents.push_back(std::move(change));

                    break;
                }
                case FILE_ACTION_RENAMED_NEW_NAME:
                {
                    auto &change = queuedEvents.back();
                    change->path = path.string();

                    if (options.subtree)
                    {
                        for (auto it = workers.begin(); it != workers.end(); it++)
                        {
                            auto &worker = *it;

                            if (!worker->IsRunning() || worker->basePath == change->lastPath)
                            {
                                it = workers.erase(it);
                            }
                        }

                        if (options.symlinks && fs::is_symlink(path) && fs::is_directory(path))
                        {
                            workers.push_back(std::make_unique<Worker>(false, path, options, eventsBuffer, eventsBufferMutex));
                        }
                    }

                    break;
                }
                }

                if (!info->NextEntryOffset)
                {
                    break;
                }

                p += info->NextEntryOffset;
            }

            std::lock_guard<std::mutex> lock(*eventsBufferMutex);

            for (auto it = queuedEvents.begin(); it != queuedEvents.end(); it++)
            {
                auto &change = *it;
                if (change->flags & options.notifyFilterFlags)
                {
                    eventsBuffer->push(std::move(*it));
                }
            }

            break;
        }
        default:
        {
            running = false;
            break;
        }
        }
    }
#endif

    if (isRootWorker)
    {
        std::lock_guard<std::mutex> lock(*eventsBufferMutex);

        auto change = std::make_unique<NotifyEvent>();
        change->type = kStop;
        change->path = basePath.string();

        eventsBuffer->push(std::move(change));
    }
}

DirectoryWatcher::DirectoryWatcher()
{
    eventsBuffer = std::make_unique<EventQueue>();
    eventsBufferMutex = std::make_unique<std::mutex>();
}

DirectoryWatcher::~DirectoryWatcher()
{
    StopWatching();
}

bool DirectoryWatcher::Watch(const std::filesystem::path &absPath, const WatchOptions &options)
{
    if (!fs::is_directory(absPath))
    {
        return false;
    }

    auto worker = std::make_unique<Worker>(true, absPath, options, eventsBuffer.get(), eventsBufferMutex.get());
    workers.push_back(std::move(worker));

    return true;
}

bool DirectoryWatcher::IsWatching(const std::filesystem::path &absPath) const
{
    for (auto it = workers.begin(); it != workers.end(); it++)
    {
        auto &worker = *it;
        if (worker->IsRunning() && worker->basePath == absPath)
        {
            return true;
        }
    }

    return false;
}

void DirectoryWatcher::StopWatching()
{
    workers.clear();
}

void DirectoryWatcher::ProcessEvents()
{
    std::lock_guard<std::mutex> lock(*eventsBufferMutex.get());

    while (!eventsBuffer->empty())
    {
        auto &front = eventsBuffer->front();
        OnProcessEvent(*front.get());
        eventsBuffer->pop();
    }
}

void DirectoryWatcher::OnProcessEvent(const NotifyEvent &event)
{
}