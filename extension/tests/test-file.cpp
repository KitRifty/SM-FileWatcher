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

#include <gtest/gtest.h>
#include <fstream>
#include "runner.h"

namespace fs = std::filesystem;

TEST(File, CreateUpdateDeleteFile)
{
    WatchEventCollector watcher;
    TempDir dir;

    DirectoryWatcher::WatchOptions options;
    options.subtree = false;
    options.symlinks = false;
    options.bufferSize = 8192;
    options.notifyFilterFlags = DirectoryWatcher::NotifyFilterFlags::kNotifyAll;

    EXPECT_TRUE(watcher.Watch(dir.GetPath(), {false, false, DirectoryWatcher::NotifyFilterFlags::kNotifyAll, 8192}));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto file = std::ofstream(dir.GetPath() / "new_file");
    file << "Hello world";
    file.close();
    fs::remove(dir.GetPath() / "new_file");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    watcher.StopWatching();
    watcher.ProcessEvents();

    ASSERT_EQ(watcher.events.size(), 5);
    ASSERT_EQ(watcher.events[0].type, DirectoryWatcher::NotifyEventType::kStart);
    ASSERT_EQ(watcher.events[0].path, dir.GetPath());

    ASSERT_EQ(watcher.events[1].type, DirectoryWatcher::NotifyEventType::kFilesystem);
    ASSERT_EQ(watcher.events[1].flags, DirectoryWatcher::NotifyFilterFlags::kCreated);
    ASSERT_EQ(watcher.events[1].path, dir.GetPath() / "new_file");

    ASSERT_EQ(watcher.events[2].type, DirectoryWatcher::NotifyEventType::kFilesystem);
    ASSERT_EQ(watcher.events[2].flags, DirectoryWatcher::NotifyFilterFlags::kModified);
    ASSERT_EQ(watcher.events[2].path, dir.GetPath() / "new_file");

    ASSERT_EQ(watcher.events[3].type, DirectoryWatcher::NotifyEventType::kFilesystem);
    ASSERT_EQ(watcher.events[3].flags, DirectoryWatcher::NotifyFilterFlags::kDeleted);
    ASSERT_EQ(watcher.events[3].path, dir.GetPath() / "new_file");

    ASSERT_EQ(watcher.events[4].type, DirectoryWatcher::NotifyEventType::kStop);
    ASSERT_EQ(watcher.events[4].path, dir.GetPath());
}

TEST(File, RenameFile)
{
    WatchEventCollector watcher;
    TempDir dir;

    auto file = std::ofstream(dir.GetPath() / "new_file");
    file << "Hello world";
    file.close();

    EXPECT_TRUE(watcher.Watch(dir.GetPath(), {false, false, DirectoryWatcher::NotifyFilterFlags::kNotifyAll, 8192}));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    fs::rename(dir.GetPath() / "new_file", dir.GetPath() / "my_new_file");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    watcher.StopWatching();
    watcher.ProcessEvents();

    ASSERT_EQ(watcher.events.size(), 3);
    ASSERT_EQ(watcher.events[0].type, DirectoryWatcher::NotifyEventType::kStart);
    ASSERT_EQ(watcher.events[0].path, dir.GetPath());

    ASSERT_EQ(watcher.events[1].type, DirectoryWatcher::NotifyEventType::kFilesystem);
    ASSERT_EQ(watcher.events[1].flags, DirectoryWatcher::NotifyFilterFlags::kRenamed);
    ASSERT_EQ(watcher.events[1].lastPath, dir.GetPath() / "new_file");
    ASSERT_EQ(watcher.events[1].path, dir.GetPath() / "my_new_file");

    ASSERT_EQ(watcher.events[2].type, DirectoryWatcher::NotifyEventType::kStop);
    ASSERT_EQ(watcher.events[2].path, dir.GetPath());
}

TEST(File, MoveFileInAndOut)
{
    WatchEventCollector watcher;
    TempDir dir;
    TempDir otherDir;

    auto file = std::ofstream(otherDir.GetPath() / "existing_file");
    file << "Hello world";
    file.close();

    EXPECT_TRUE(watcher.Watch(dir.GetPath(), {false, false, DirectoryWatcher::NotifyFilterFlags::kNotifyAll, 8192}));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    fs::rename(otherDir.GetPath() / "existing_file", dir.GetPath() / "existing_file");
    fs::rename(dir.GetPath() / "existing_file", otherDir.GetPath() / "existing_file");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    watcher.StopWatching();
    watcher.ProcessEvents();

    ASSERT_EQ(watcher.events.size(), 4);
    ASSERT_EQ(watcher.events[0].type, DirectoryWatcher::NotifyEventType::kStart);
    ASSERT_EQ(watcher.events[0].path, dir.GetPath());

    ASSERT_EQ(watcher.events[1].type, DirectoryWatcher::NotifyEventType::kFilesystem);
    ASSERT_EQ(watcher.events[1].flags, DirectoryWatcher::NotifyFilterFlags::kCreated);
    ASSERT_EQ(watcher.events[1].path, dir.GetPath() / "existing_file");

    ASSERT_EQ(watcher.events[2].type, DirectoryWatcher::NotifyEventType::kFilesystem);
    ASSERT_EQ(watcher.events[2].flags, DirectoryWatcher::NotifyFilterFlags::kDeleted);
    ASSERT_EQ(watcher.events[2].path, dir.GetPath() / "existing_file");

    ASSERT_EQ(watcher.events[3].type, DirectoryWatcher::NotifyEventType::kStop);
    ASSERT_EQ(watcher.events[3].path, dir.GetPath());
}