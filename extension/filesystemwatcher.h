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

#ifdef KE_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windef.h>
#elif defined KE_LINUX
#include <sys/eventfd.h>
#include <sys/inotify.h>
#endif

#include <IPluginSys.h>
#include <sp_vm_api.h>

class FileSystemWatcher
{
public:
	enum NotifyFilters
	{
		None = 0,
		FileName = (1 << 0),         // The name of the file.
		DirectoryName = (1 << 1),    // The name of the directory.
		Attributes = (1 << 2),       // The attributes of the file or folder.
		Size = (1 << 3),             // The size of the file or folder.
		LastWrite = (1 << 4),        // The date the file or folder last had anything written on it.
		LastAccess = (1 << 5),       // The date the file or folder was last opened.
		CreationTime = (1 << 6),     // The time the file or folder was created.
		Security = (1 << 8)          // The security settings of the file or folder.
	};

private:
	class ChangeEvent
	{
	public:
		NotifyFilters m_flags;
		std::string m_fullPath;
	};

	bool m_watching;
	std::string m_path;

public:
	bool m_includeSubdirectories;
	NotifyFilters m_notifyFilter;
	SourcePawn::IPluginFunction* m_onChanged;

private:
#ifdef KE_WINDOWS
	HANDLE m_threadCancelEventHandle;
#elif defined KE_LINUX
	int m_threadCancelEventHandle;
#endif

	struct ThreadData
	{
#ifdef KE_WINDOWS
		BOOL watchSubTree;
		DWORD notifyFilter;
		HANDLE directory;
		HANDLE waitHandle;
		OVERLAPPED overlapped;
		DWORD bytesReturned;
		char buffer[4096];
#elif defined KE_LINUX
		int fd;
		int wd;
#endif

		ThreadData()
		{
#ifdef KE_WINDOWS
			directory = INVALID_HANDLE_VALUE;
			waitHandle = nullptr;
			ZeroMemory(&overlapped, sizeof OVERLAPPED);
#endif
		}

		~ThreadData()
		{
#ifdef KE_WINDOWS
			if (directory && directory != INVALID_HANDLE_VALUE)
			{
				CloseHandle(directory);
			}

			if (waitHandle && waitHandle != INVALID_HANDLE_VALUE)
			{
				CloseHandle(waitHandle);
			}
#elif defined KE_LINUX
			inotify_rm_watch(_inotify_fd, _inotify_wd);
			close(_inotify_fd);
#endif
		}
	};

	std::thread m_thread;
	std::mutex m_threadRunningMutex;
	bool m_threadRunning;
	std::mutex m_changeEventsMutex;
	std::queue<std::unique_ptr<ChangeEvent>> m_changeEvents;

	FileSystemWatcher(const char* path = "", const char* filter = "");

public:
	~FileSystemWatcher();

	bool IsWatching() const { return m_watching; }
	bool Start();
	void Stop();

private:
	void OnGameFrame(bool simulating);
	void OnPluginUnloaded(SourceMod::IPlugin* plugin);

	bool IsThreadRunning();
	void SetThreadRunning(bool state);

	void RequestCancelThread();

	void ThreadProc(std::unique_ptr<ThreadData> &data);

	friend class FileSystemWatcherManager;
};

class FileSystemWatcherManager : 
	public SourceMod::IHandleTypeDispatch,
	public SourceMod::IPluginsListener
{
private:
	SourceMod::HandleType_t m_HandleType;
	static sp_nativeinfo_t m_Natives[];

	std::vector<FileSystemWatcher*> m_watchers;

public:
	FileSystemWatcherManager();

	bool SDK_OnLoad(char* error, int errorSize);
	void SDK_OnUnload();
	void OnGameFrame(bool simulating);

	SourceMod::Handle_t CreateWatcher(SourcePawn::IPluginContext* context, const char* path, const char* filter);
	FileSystemWatcher* GetWatcher(SourceMod::Handle_t handle);

	// IHandleTypeDispatch
	virtual void OnHandleDestroy(SourceMod::HandleType_t type, void *object) override;

	// IPluginsListener
	virtual void OnPluginUnloaded(SourceMod::IPlugin* plugin) override;
};

extern FileSystemWatcherManager g_FileSystemWatchers;

#endif