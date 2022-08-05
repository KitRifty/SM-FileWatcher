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

#include <am-platform.h>
#include <string>
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <map>

#ifdef KE_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windef.h>
#elif defined KE_LINUX
#include <sys/inotify.h>
#include <fcntl.h>
#endif

#include <IPluginSys.h>
#include <sp_vm_api.h>

class FileSystemWatcher
{
public:
	enum NotifyFilters
	{
		FSW_NOTIFY_NONE = 0,
		FSW_NOTIFY_CREATED = (1 << 0),
		FSW_NOTIFY_DELETED = (1 << 1),
		FSW_NOTIFY_MODIFIED = (1 << 2),
		FSW_NOTIFY_RENAMED = (1 << 3)
	};

private:
	class NotifyEvent
	{
	public:
		enum NotifyEventType
		{
			FILESYSTEM = 0,
			START,
			EXIT
		};

		NotifyEventType type;
		NotifyFilters flags;
#ifdef KE_LINUX
		uint32_t cookie;
#endif
		std::string lastPath;
		std::string path;
	};

	bool m_watching;
	std::string m_relPath;
	std::string m_path;

public:
	bool m_includeSubdirectories;
	NotifyFilters m_notifyFilter;
	SourceMod::Handle_t m_Handle;

	SourcePawn::IPluginContext* m_owningContext;
	SourcePawn::IPluginFunction* m_onStarted;
	SourcePawn::IPluginFunction* m_onStopped;
	SourcePawn::IPluginFunction* m_onCreated;
	SourcePawn::IPluginFunction* m_onDeleted;
	SourcePawn::IPluginFunction* m_onModified;
	SourcePawn::IPluginFunction* m_onRenamed;

private:
#ifdef KE_WINDOWS
	HANDLE m_threadCancelEventHandle;
#elif defined KE_LINUX
	int m_threadCancelEventHandle;
#endif

	class ThreadData
	{
	public:
		std::string root_path;
		bool includeSubdirectories;
		NotifyFilters notifyFilters;
#ifdef KE_WINDOWS
		DWORD dwNotifyFilter;
		HANDLE directory;
		HANDLE changeEvent;
#elif defined KE_LINUX
		int fd;
		int root_wd;
		std::map<int, std::string> wd;
		uint32_t mask;
#endif

		ThreadData();
		~ThreadData();

#if defined KE_LINUX
		int AddWatch(const std::string &baseRelPath, uint32_t _mask = 0);
#endif
	};

	std::thread m_thread;
	std::mutex m_threadRunningMutex;
	bool m_threadRunning;
	std::mutex m_changeEventsMutex;
	std::queue<std::unique_ptr<NotifyEvent>> m_changeEvents;
	bool m_processingEvents;

	FileSystemWatcher(const std::string &relPath);

public:
	~FileSystemWatcher();

	bool IsWatching() const { return m_watching; }
	size_t GetAbsolutePath(char* buffer, size_t bufferSize);
	size_t GetRelativePath(char* buffer, size_t bufferSize);
	bool Start();
	void Stop();

private:
	void OnGameFrame(bool simulating);
	void OnPluginUnloaded(SourceMod::IPlugin* plugin);

	bool IsThreadRunning();
	void SetThreadRunning(bool state);

	void RequestCancelThread();

	void ThreadProc(std::unique_ptr<ThreadData> data);

	void ProcessEvents();

	friend class FileSystemWatcherManager;
};

class FileSystemWatcherManager : 
	public SourceMod::IHandleTypeDispatch,
	public SourceMod::IPluginsListener
{
private:
	static SourceMod::HandleType_t m_HandleType;
	static sp_nativeinfo_t m_Natives[];

	std::vector<FileSystemWatcher*> m_watchers;

public:
	FileSystemWatcherManager();

	bool SDK_OnLoad(char* error, int errorSize);
	void SDK_OnUnload();
	void OnGameFrame(bool simulating);

	SourceMod::Handle_t CreateWatcher(SourcePawn::IPluginContext* context, const std::string &path);
	FileSystemWatcher* GetWatcher(SourceMod::Handle_t handle);

	// IHandleTypeDispatch
	virtual void OnHandleDestroy(SourceMod::HandleType_t type, void *object) override;

	// IPluginsListener
	virtual void OnPluginUnloaded(SourceMod::IPlugin* plugin) override;
};

extern FileSystemWatcherManager g_FileSystemWatchers;

#endif