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

#include "watcher.h"

#include <IPluginSys.h>
#include <sp_vm_api.h>

class SMDirectoryWatcher : public DirectoryWatcher
{
public:
	SMDirectoryWatcher(const std::string &relPath);

	size_t GetPath(char* buffer, size_t bufferSize);

protected:
	virtual void OnProcessEvent(const NotifyEvent &event);
private:
	void OnGameFrame(bool simulating);
	void OnPluginUnloaded(SourceMod::IPlugin* plugin);

	friend class SMDirectoryWatcherManager;

public:
	SourceMod::Handle_t m_Handle;

	SourcePawn::IPluginContext* m_owningContext;
	SourcePawn::IPluginFunction* m_onStarted;
	SourcePawn::IPluginFunction* m_onStopped;
	SourcePawn::IPluginFunction* m_onCreated;
	SourcePawn::IPluginFunction* m_onDeleted;
	SourcePawn::IPluginFunction* m_onModified;
	SourcePawn::IPluginFunction* m_onRenamed;

private:
	std::filesystem::path m_relPath;
};

class SMDirectoryWatcherManager : 
	public SourceMod::IHandleTypeDispatch,
	public SourceMod::IPluginsListener
{
private:
	static SourceMod::HandleType_t m_HandleType;
	static sp_nativeinfo_t m_Natives[];

	std::vector<SMDirectoryWatcher*> m_watchers;

public:
	SMDirectoryWatcherManager();

	bool SDK_OnLoad(char* error, int errorSize);
	void SDK_OnUnload();
	void OnGameFrame(bool simulating);

	SourceMod::Handle_t CreateWatcher(SourcePawn::IPluginContext* context, const std::string &path);
	SMDirectoryWatcher* GetWatcher(SourceMod::Handle_t handle);

	// IHandleTypeDispatch
	virtual void OnHandleDestroy(SourceMod::HandleType_t type, void *object) override;

	// IPluginsListener
	virtual void OnPluginUnloaded(SourceMod::IPlugin* plugin) override;
};

extern SMDirectoryWatcherManager g_FileSystemWatchers;

#endif // #ifndef _INCLUDE_FILESYSTEMWATCHER_H_