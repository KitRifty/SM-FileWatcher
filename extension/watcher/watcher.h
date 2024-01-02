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

#ifndef WATCHER_H_
#define WATCHER_H_

#include <filesystem>
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <map>

#ifdef __linux__
#else
#include <Windows.h>
#endif

#include "helpers.h"

class DirectoryWatcher
{
public:
    enum NotifyFilterFlags : unsigned int
    {
        kNone = 0,
        kCreated = (1 << 0),
        kDeleted = (1 << 1),
        kModified = (1 << 2),
        kRenamed = (1 << 3),
        kNotifyAll = (kNone - 1)
    };

    struct WatchOptions
    {
        bool subtree;
        bool symlinks;
        NotifyFilterFlags notifyFilterFlags;
        size_t bufferSize;
    };

    enum NotifyEventType
    {
        kFilesystem = 0,
        kStart,
        kStop
    };

    struct NotifyEvent
    {
    public:
        NotifyEventType type;
        NotifyFilterFlags flags;
        std::string lastPath;
        std::string path;

#ifdef __linux__
        uint32_t cookie;
#endif
    };

    typedef std::queue<std::unique_ptr<NotifyEvent>> EventQueue;

public:
    DirectoryWatcher();
    virtual ~DirectoryWatcher();
    bool Watch(const std::filesystem::path &absPath, const WatchOptions &options);
    bool IsWatching(const std::filesystem::path &absPath) const;
    void StopWatching();

    void ProcessEvents();
    virtual void OnProcessEvent(const NotifyEvent &event);

private:
    std::unique_ptr<EventQueue> eventsBuffer;
    std::unique_ptr<std::mutex> eventsBufferMutex;

    class Worker
    {
    public:
        Worker(bool isRoot, const std::filesystem::path &path, const WatchOptions &options, EventQueue *eventsBuffer, std::mutex *eventsBufferMutex);
        ~Worker();
        inline bool IsRunning() const { return thread.joinable(); }

    private:
#ifdef __linux__
        int AddDirectory(const std::filesystem::path &path);
#endif

        void ThreadProc();

    public:
        bool isRootWorker;
        const std::filesystem::path basePath;

    private:
        EventQueue *eventsBuffer;
        std::mutex *eventsBufferMutex;

        const WatchOptions options;
        std::thread thread;

#ifdef __linux__
        int fileDescriptor;
        std::map<int, std::filesystem::path> watchDescriptors;
        int cancelEvent;
#else
        std::vector<std::unique_ptr<Worker>> workers;
        ScopedHandle directory;
        ScopedHandle cancelEvent;
#endif
    };

    std::vector<std::unique_ptr<Worker>> workers;
};

#endif // WATCHER_H_