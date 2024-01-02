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