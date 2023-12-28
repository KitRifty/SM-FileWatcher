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

#ifndef _INCLUDE_WATCHER_H_
#define _INCLUDE_WATCHER_H_
#pragma once

#include <filesystem>
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <map>

#ifdef __linux__
#else
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windef.h>
#endif

class DirectoryWatcher
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

protected:
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
#ifdef __linux__
		uint32_t cookie;
#endif
		std::string lastPath;
		std::string path;
	};

	bool m_watching;
	std::filesystem::path m_path;

public:
	bool m_includeSubdirectories;
	NotifyFilters m_notifyFilter;
	size_t m_bufferSize;
	int m_retryInterval;

private:
#ifdef __linux__
	int m_threadCancelEventHandle;
#else
	HANDLE m_threadCancelEventHandle;
#endif

	class ThreadConfig
	{
	public:
		std::filesystem::path root_path;
		bool includeSubdirectories;
		NotifyFilters notifyFilters;
		size_t bufferSize;
		int retryInterval;
	};

	std::thread m_thread;
	std::mutex m_threadRunningMutex;
	bool m_threadRunning;
	std::mutex m_changeEventsMutex;
	std::queue<std::unique_ptr<NotifyEvent>> m_changeEvents;
	bool m_processingEvents;

public:
	DirectoryWatcher(const std::filesystem::path& absPath);
	virtual ~DirectoryWatcher();

	inline bool IsWatching() const { return m_watching; }
	bool Start();
	void Stop();

private:
	bool IsThreadRunning();
	void SetThreadRunning(bool state);

	void RequestCancelThread();

	void ThreadProc(std::unique_ptr<ThreadConfig> data);

protected:
	void ProcessEvents();
	virtual void OnProcessEvent(const NotifyEvent &event);
};

#endif