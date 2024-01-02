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

TEST(SymbolicLinks, CreateRenameDeleteDir)
{
    WatchEventCollector watcher;
    TempDir dir;
    TempDir symDir;

    EXPECT_TRUE(watcher.Watch(dir.GetPath(), {true, true, DirectoryWatcher::NotifyFilterFlags::kNotifyAll, 8192}));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    fs::create_directory_symlink(symDir.GetPath(), dir.GetPath() / "sym_link");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto file = std::ofstream(symDir.GetPath() / "existing_file");
    file << "Hello world";
    file.close();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    watcher.StopWatching();
    watcher.ProcessEvents();

    ASSERT_EQ(watcher.events.size(), 5);

    size_t i = 0;

    ASSERT_EQ(watcher.events[i].type, DirectoryWatcher::NotifyEventType::kStart);
    ASSERT_EQ(watcher.events[i].path, dir.GetPath());

    i++;

    ASSERT_EQ(watcher.events[i].type, DirectoryWatcher::NotifyEventType::kFilesystem);
    ASSERT_EQ(watcher.events[i].flags, DirectoryWatcher::NotifyFilterFlags::kCreated);
    ASSERT_EQ(watcher.events[i].path, dir.GetPath() / "sym_link");

    i++;

    ASSERT_EQ(watcher.events[i].type, DirectoryWatcher::NotifyEventType::kFilesystem);
    ASSERT_EQ(watcher.events[i].flags, DirectoryWatcher::NotifyFilterFlags::kCreated);
    ASSERT_EQ(watcher.events[i].path, dir.GetPath() / "sym_link" / "existing_file");

    i++;

    ASSERT_EQ(watcher.events[i].type, DirectoryWatcher::NotifyEventType::kFilesystem);
    ASSERT_EQ(watcher.events[i].flags, DirectoryWatcher::NotifyFilterFlags::kModified);
    ASSERT_EQ(watcher.events[i].path, dir.GetPath() / "sym_link" / "existing_file");

    i++;

    ASSERT_EQ(watcher.events[i].type, DirectoryWatcher::NotifyEventType::kStop);
    ASSERT_EQ(watcher.events[i].path, dir.GetPath());

    i++;
}