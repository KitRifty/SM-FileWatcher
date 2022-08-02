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

#ifndef _INCLUDE_FILESYSTEMWATCHER_H_
#define _INCLUDE_FILESYSTEMWATCHER_H_

#include <IPluginSys.h>
#include <am-platform.h>
#include <string>
#include <vector>
#include <memory>

#ifdef KE_WINDOWS
#include <Windows.h>
#include <windef.h>
#endif

class FileSystemWatcher
{
private:
	struct PathInfo
	{
		std::string m_path;
		bool m_watchSubTree;

		PathInfo(const char* path, bool watchSubTree)
		{
			m_path = path;
			m_watchSubTree = watchSubTree;
		}
	};

	std::vector<std::unique_ptr<PathInfo>> m_paths;

#ifdef KE_WINDOWS
	std::vector<HANDLE> m_changeHandles;
#endif

	FileSystemWatcher();

public:
	~FileSystemWatcher();

	bool AddPath(const char* path, bool watchSubTree);

	friend class FileSystemWatcherManager;
};

class FileSystemWatcherManager : public SourceMod::IHandleTypeDispatch
{
private:
	SourceMod::HandleType_t m_HandleType;

public:
	FileSystemWatcherManager();

	bool SDK_OnLoad(char* error, int errorSize);
	void SDK_OnUnload();

	SourceMod::Handle_t CreateWatcher(SourceMod::IPlugin* plugin);

	// IHandleTypeDispatch
	virtual void OnHandleDestroy(SourceMod::HandleType_t type, void *object) override;
};

extern FileSystemWatcherManager g_FileSystemWatchers;

#endif