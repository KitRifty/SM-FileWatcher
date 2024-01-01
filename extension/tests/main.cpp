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

#include <iostream>
#include <fstream>
#include <filesystem>
#include <random>
#include <sstream>
#include <gtest/gtest.h>
#include "runner.h"

namespace fs = std::filesystem;

void WatchEventCollector::OnProcessEvent(const NotifyEvent &event)
{
    events.emplace_back(event);
}

std::string generate_random_string(size_t length)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static const std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::uniform_int_distribution<> distribution(0, chars.size() - 1);

    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i)
    {
        result += chars[distribution(gen)];
    }
    return result;
}

TempDir::TempDir()
{
#ifdef __linux
    char temp[] = "/tmp/watchertestXXXXXX";
    char *p = mkdtemp(temp);

    if (p != nullptr)
    {
        path = p;
    }
#else
    fs::path tempDir;
    while (true)
    {
        tempDir = fs::temp_directory_path() / ("watchertest" + generate_random_string(6));
        if (!fs::exists(tempDir))
        {
            if (fs::create_directories(tempDir))
            {
                path = tempDir;
            }

            break;
        }
    }
#endif
}

TempDir::~TempDir()
{
    if (!path.empty())
    {
        fs::remove_all(path);
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

/*
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
    options.notifyFilterFlags = DirectoryWatcher::kNotifyAll;

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
*/