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

#include "filesystemwatcher.h"
#include "smsdk_ext.h"
#include "am-string.h"

FileSystemWatcher::FileSystemWatcher(const char* path, const char* filter) :
	m_watching(false),
	m_path(path),
	m_includeSubdirectories(false),
	m_notifyFilter(None),
#ifdef KE_WINDOWS
	m_threadCancelEventHandle(nullptr),
#elif defined KE_LINUX
#endif
	m_thread(),
	m_threadRunning(false)
{
}

FileSystemWatcher::~FileSystemWatcher()
{
	Stop();

#ifdef KE_WINDOWS
	if (m_threadCancelEventHandle)
	{
		CloseHandle(m_threadCancelEventHandle);
	}
#endif
}

void FileSystemWatcher::AddPluginCallback(SourcePawn::IPluginFunction* cb)
{
	m_pluginCallbacks.push_back(cb);
}

void FileSystemWatcher::RemovePluginCallback(SourcePawn::IPluginFunction* cb)
{
	for (auto it = m_pluginCallbacks.begin(); it != m_pluginCallbacks.end(); it++)
	{
		if (*it == cb)
		{
			m_pluginCallbacks.erase(it);
			break;
		}
	}
}

bool FileSystemWatcher::Start()
{
	if (IsWatching())
	{
		return true;
	}

#ifdef KE_WINDOWS
	DWORD notifyFilter = 0; 
	if (m_notifyFilter & FileName)
	{
		notifyFilter |= FILE_NOTIFY_CHANGE_FILE_NAME;
	}

	if (m_notifyFilter & DirectoryName)
	{
		notifyFilter |= FILE_NOTIFY_CHANGE_DIR_NAME;
	}
	
	if (m_notifyFilter & Attributes)
	{
		notifyFilter |= FILE_NOTIFY_CHANGE_ATTRIBUTES;
	}

	if (m_notifyFilter & Size)
	{
		notifyFilter |= FILE_NOTIFY_CHANGE_SIZE;
	}

	if (m_notifyFilter & LastWrite)
	{
		notifyFilter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
	}

	if (m_notifyFilter & LastAccess)
	{
		notifyFilter |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
	}

	if (m_notifyFilter & CreationTime)
	{
		notifyFilter |= FILE_NOTIFY_CHANGE_CREATION;
	}
	
	if (m_notifyFilter & Security)
	{
		notifyFilter |= FILE_NOTIFY_CHANGE_SECURITY;
	}
	
	HANDLE changeHandle = FindFirstChangeNotificationA(m_path.c_str(), m_includeSubdirectories, notifyFilter);
	if (changeHandle == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	if (!m_threadCancelEventHandle)
	{
		m_threadCancelEventHandle = CreateEvent(nullptr, true, false, nullptr);

		if (!m_threadCancelEventHandle)
		{
			FindCloseChangeNotification(changeHandle);
			return false;
		}
	}
	else
	{
		ResetEvent(m_threadCancelEventHandle);
	}

#endif

	m_watching = true;

	m_threadRunning = true;
	m_thread = std::thread(&FileSystemWatcher::ThreadProc, this, changeHandle);

	return true;
}

void FileSystemWatcher::Stop()
{
	if (!IsWatching())
	{
		return;
	}

	m_watching = false;

#ifdef KE_WINDOWS
	SetEvent(m_threadCancelEventHandle);
#elif defined KE_LINUX
	m_mutex.lock();

	// TODO

	m_mutex.unlock();
#endif

	if (m_thread.joinable())
	{
		m_thread.join();
	}

	m_changeEvents = std::queue<int>();
}

void FileSystemWatcher::OnGameFrame(bool simulating)
{
	if (IsWatching())
	{
		m_mutex.lock();

		while (!m_changeEvents.empty())
		{
			m_changeEvents.pop();
		}

		bool threadRunning = m_threadRunning;

		m_mutex.unlock();

		if (!threadRunning)
		{
			Stop();
		}
	}
}

void FileSystemWatcher::OnPluginUnloaded(SourceMod::IPlugin* plugin)
{
	IPluginContext* context = plugin->GetBaseContext();

	for (auto it = m_pluginCallbacks.begin(); it != m_pluginCallbacks.end(); )
	{
		if ((*it)->GetParentContext() == context)
		{
			it = m_pluginCallbacks.erase(it);
		}
		else
		{
			it++;
		}
	}
}

#ifdef KE_WINDOWS
void FileSystemWatcher::ThreadProc(HANDLE changeHandle)
#elif defined KE_LINUX
void FileSystemWatcher::ThreadProc()
#endif
{
#ifdef KE_WINDOWS
	HANDLE waitHandles[2];
	waitHandles[0] = changeHandle;
	waitHandles[1] = m_threadCancelEventHandle;

	while (true)
	{
		DWORD waitStatus = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);

		switch (waitStatus) 
		{
			case WAIT_TIMEOUT:
			{
				break;
			}
			case WAIT_OBJECT_0:
			{
				m_mutex.lock();
				m_changeEvents.push(0);
				m_mutex.unlock();

				if (FindNextChangeNotification(waitHandles[0]) == FALSE)
				{
					goto terminate;
				}

				break;
			}
			case WAIT_OBJECT_0 + 1:
			{
				goto terminate;
				break;
			}
			default:
			{
				break;
			}
		}
	}
#elif defined KE_LINUX

	// TODO

#endif

terminate:
#ifdef KE_WINDOWS
	FindCloseChangeNotification(waitHandles[0]);
#endif

	m_mutex.lock();
	m_threadRunning = false;
	m_mutex.unlock();
}

FileSystemWatcherManager g_FileSystemWatchers;

FileSystemWatcherManager::FileSystemWatcherManager() : 
	m_HandleType(0)
{
}

static void GameFrameHook(bool simulating)
{
	g_FileSystemWatchers.OnGameFrame(simulating);
}

bool FileSystemWatcherManager::SDK_OnLoad(char* error, int errorSize)
{
	m_HandleType = g_pHandleSys->CreateType("FileSystemWatcher", this, 0, nullptr, nullptr, myself->GetIdentity(), nullptr);
	if (!m_HandleType)
	{
		ke::SafeStrcpy(error, errorSize, "Failed to create FileSystemWatcher handle type.");
		return false;
	}

	plsys->AddPluginsListener(this);

	smutils->AddGameFrameHook(&GameFrameHook);

	sharesys->AddNatives(myself, m_Natives);

	return true;
}

void FileSystemWatcherManager::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(&GameFrameHook);

	plsys->RemovePluginsListener(this);

	if (m_HandleType)
	{
		g_pHandleSys->RemoveType(m_HandleType, myself->GetIdentity());
	}
}

void FileSystemWatcherManager::OnGameFrame(bool simulating)
{
	for (auto it = m_watchers.begin(); it != m_watchers.end(); it++)
	{
		(*it)->OnGameFrame(simulating);
	}
}

SourceMod::Handle_t FileSystemWatcherManager::CreateWatcher(SourcePawn::IPluginContext* context, const char* path, const char* filter)
{
	FileSystemWatcher* watcher = new FileSystemWatcher(path, filter);
	m_watchers.push_back(watcher);
	return handlesys->CreateHandle(m_HandleType, watcher, context->GetIdentity(), myself->GetIdentity(), nullptr);
}

FileSystemWatcher* FileSystemWatcherManager::GetWatcher(SourceMod::Handle_t handle)
{
	FileSystemWatcher* watcher = nullptr;
	HandleSecurity sec(nullptr, myself->GetIdentity());

	SourceMod::HandleError err = g_pHandleSys->ReadHandle(handle, m_HandleType, &sec, (void**)(&watcher));
	return (err == HandleError_None) ? watcher : nullptr;
}

void FileSystemWatcherManager::OnHandleDestroy(SourceMod::HandleType_t type, void *object)
{
	if (type == m_HandleType)
	{
		FileSystemWatcher* watcher = (FileSystemWatcher*)object;

		for (auto it = m_watchers.begin(); it != m_watchers.end();)
		{
			if (*it == watcher)
			{
				it = m_watchers.erase(it);
			}
			else
			{
				it++;
			}
		}

		delete object;
	}
}

void FileSystemWatcherManager::OnPluginUnloaded(SourceMod::IPlugin* plugin)
{
	for (auto it = m_watchers.begin(); it != m_watchers.end(); it++)
	{
		(*it)->OnPluginUnloaded(plugin);
	}
}

cell_t Native_FileSystemWatcher(SourcePawn::IPluginContext *context, const cell_t *params)
{
	char* path = nullptr;
	context->LocalToString(params[1], &path);

	char* filter = nullptr;
	context->LocalToString(params[2], &filter);

	char realPath[PLATFORM_MAX_PATH];
	g_pSM->BuildPath(Path_Game, realPath, sizeof(realPath), "%s", path);

	return g_FileSystemWatchers.CreateWatcher(context, realPath, filter);
}

cell_t Native_FileSystemWatcher_IncludeSubdirectoriesGet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	return watcher->m_includeSubdirectories;
}

cell_t Native_FileSystemWatcher_IncludeSubdirectoriesSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	watcher->m_includeSubdirectories = params[2] != 0;
	return 0;
}

cell_t Native_FileSystemWatcher_NotifyFilterGet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	return (cell_t)watcher->m_notifyFilter;
}

cell_t Native_FileSystemWatcher_NotifyFilterSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	watcher->m_notifyFilter = (FileSystemWatcher::NotifyFilters)params[2];
	return 0;
}

cell_t Native_FileSystemWatcher_IsWatchingGet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	return watcher->IsWatching();
}

cell_t Native_FileSystemWatcher_StartWatching(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	return watcher->Start();
}

cell_t Native_FileSystemWatcher_StopWatching(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	watcher->Stop();
	return 0;
}

sp_nativeinfo_s FileSystemWatcherManager::m_Natives[] =
{
	{"FileSystemWatcher.FileSystemWatcher",	Native_FileSystemWatcher},
	{"FileSystemWatcher.IncludeSubdirectories.get", Native_FileSystemWatcher_IncludeSubdirectoriesGet},
	{"FileSystemWatcher.IncludeSubdirectories.set", Native_FileSystemWatcher_IncludeSubdirectoriesSet},
	{"FileSystemWatcher.NotifyFilter.get", Native_FileSystemWatcher_NotifyFilterGet},
	{"FileSystemWatcher.NotifyFilter.set", Native_FileSystemWatcher_NotifyFilterSet},
	{"FileSystemWatcher.IsWatching.get", Native_FileSystemWatcher_IsWatchingGet},
	{"FileSystemWatcher.StartWatching", Native_FileSystemWatcher_StartWatching},
	{"FileSystemWatcher.StopWatching", Native_FileSystemWatcher_StopWatching},
	{NULL,			NULL},
};