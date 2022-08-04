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
#include <locale>
#include <codecvt>
#include <am-string.h>
#include <os/am-fsutil.h>

#ifdef KE_LINUX
#include <sys/eventfd.h>
#include <poll.h>
#endif

FileSystemWatcher::FileSystemWatcher(const char* path, const char* filter) :
	m_watching(false),
	m_path(path),
	m_includeSubdirectories(false),
	m_notifyFilter(FSW_NOTIFY_NONE),
	m_Handle(0),
	m_onCreated(nullptr),
	m_onDeleted(nullptr),
	m_onModified(nullptr),
	m_onRenamed(nullptr),
#ifdef KE_WINDOWS
	m_threadCancelEventHandle(nullptr),
#elif defined KE_LINUX
	m_threadCancelEventHandle(-1),
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
#elif defined KE_LINUX
	if (fcntl(m_threadCancelEventHandle, F_GETFD) != -1)
	{
		close(m_threadCancelEventHandle);	
	}
#endif
}

bool FileSystemWatcher::Start()
{
	if (IsWatching())
	{
		return true;
	}

	if (!ke::file::IsDirectory(m_path.c_str()))
	{
		return false;
	}

	std::unique_ptr<ThreadData> data = std::make_unique<ThreadData>();
	data->includeSubdirectories = m_includeSubdirectories;
	data->notifyFilters = m_notifyFilter;

#ifdef KE_WINDOWS
	data->dwNotifyFilter = 0;

	if (m_notifyFilter & (FSW_NOTIFY_RENAMED | FSW_NOTIFY_CREATED | FSW_NOTIFY_DELETED))
	{
		data->dwNotifyFilter |= FILE_NOTIFY_CHANGE_FILE_NAME;
		data->dwNotifyFilter |= FILE_NOTIFY_CHANGE_DIR_NAME;
	}
	
	if (m_notifyFilter & FSW_NOTIFY_MODIFIED)
	{
		data->dwNotifyFilter |= FILE_NOTIFY_CHANGE_ATTRIBUTES;
		data->dwNotifyFilter |= FILE_NOTIFY_CHANGE_SIZE;
		data->dwNotifyFilter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
		data->dwNotifyFilter |= FILE_NOTIFY_CHANGE_SECURITY;
		data->dwNotifyFilter |= FILE_NOTIFY_CHANGE_CREATION;
	}
	
	data->directory = CreateFileA(
		m_path.c_str(),
		FILE_LIST_DIRECTORY | GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		nullptr);

	if (data->directory == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	data->changeEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

	if (!m_threadCancelEventHandle)
	{
		m_threadCancelEventHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	}
	else
	{
		ResetEvent(m_threadCancelEventHandle);
	}
#elif defined KE_LINUX
	data->fd = inotify_init1(IN_NONBLOCK);
	if (data->fd == -1)
	{
		return false;
	}

	data->mask = 0;
	if (m_notifyFilter & (FileName | DirectoryName))
	{
		data->mask |= IN_MOVE;
	}

	if (m_notifyFilter & (Attributes | Security))
	{
		data->mask |= IN_ATTRIB;
	}

	if (m_notifyFilter & LastWrite)
	{
		data->mask |= IN_MODIFY;
	}

	if (m_notifyFilter & LastAccess)
	{
		data->mask |= IN_ACCESS;
	}

	if (m_notifyFilter & CreationTime)
	{
		data->mask |= IN_CREATE;
	}

	data->wd = inotify_add_watch(data->fd, m_path.c_str(), data->mask);
	if (data->wd == -1)
	{
		return false;
	}

	if (fcntl(m_threadCancelEventHandle, F_GETFD) == -1)
	{
		m_threadCancelEventHandle = eventfd(0, 0);
	}
	else
	{
		uint64_t u;
		read(m_threadCancelEventHandle, &u, sizeof(u));
	}
#endif

	m_watching = true;

	SetThreadRunning(true);

	m_thread = std::thread(&FileSystemWatcher::ThreadProc, this, std::move(data));

	return true;
}

void FileSystemWatcher::Stop()
{
	if (!IsWatching())
	{
		return;
	}

	m_watching = false;

	RequestCancelThread();

	if (m_thread.joinable())
	{
		m_thread.join();
	}

	ProcessEvents();

	m_changeEvents = std::queue<std::unique_ptr<ChangeEvent>>();
}

void FileSystemWatcher::OnGameFrame(bool simulating)
{
	if (IsWatching())
	{
		ProcessEvents();
	}
}

void FileSystemWatcher::OnPluginUnloaded(SourceMod::IPlugin* plugin)
{
	IPluginContext* context = plugin->GetBaseContext();

	if (m_onCreated && m_onCreated->GetParentContext() == context)
	{
		m_onCreated = nullptr;
	}

	if (m_onDeleted && m_onDeleted->GetParentContext() == context)
	{
		m_onDeleted = nullptr;
	}

	if (m_onModified && m_onModified->GetParentContext() == context)
	{
		m_onModified = nullptr;
	}

	if (m_onRenamed && m_onRenamed->GetParentContext() == context)
	{
		m_onRenamed = nullptr;
	}
}

bool FileSystemWatcher::IsThreadRunning()
{
	std::lock_guard<std::mutex> lock(m_threadRunningMutex);
	return m_threadRunning;
}

void FileSystemWatcher::SetThreadRunning(bool state)
{
	std::lock_guard<std::mutex> lock(m_threadRunningMutex);
	m_threadRunning = state;
}

void FileSystemWatcher::RequestCancelThread()
{
#ifdef KE_WINDOWS
	SetEvent(m_threadCancelEventHandle);
#elif defined KE_LINUX
	uint64_t u = 1;
	write(m_threadCancelEventHandle, &u, sizeof(u));
#endif
}

FileSystemWatcher::ThreadData::ThreadData() : includeSubdirectories(false)
{
#ifdef KE_WINDOWS
	directory = INVALID_HANDLE_VALUE;
	changeEvent = nullptr;
#elif defined KE_LINUX
	fd = -1;
	wd = -1;
	mask = 0;
#endif
}

FileSystemWatcher::ThreadData::~ThreadData()
{
#ifdef KE_WINDOWS
	if (changeEvent && changeEvent != INVALID_HANDLE_VALUE)
	{
		CloseHandle(changeEvent);
	}

	if (directory && directory != INVALID_HANDLE_VALUE)
	{
		CloseHandle(directory);
	}
#elif defined KE_LINUX
	if (fcntl(fd, F_GETFD) != -1)
	{
		if (wd != -1)
		{
			inotify_rm_watch(fd, wd);
		}

		close(fd);
	}
#endif
}

void FileSystemWatcher::ThreadProc(std::unique_ptr<ThreadData> data)
{
#ifdef KE_WINDOWS
	HANDLE waitHandles[2];
	waitHandles[0] = data->changeEvent;
	waitHandles[1] = m_threadCancelEventHandle;

	OVERLAPPED overlapped;
	ZeroMemory(&overlapped, sizeof overlapped);
	overlapped.hEvent = data->changeEvent;

	DWORD bytesReturned;
	char buffer[4096];

	while (true)
	{
		if (!ReadDirectoryChangesW(data->directory,
			buffer,
			sizeof buffer,
			data->includeSubdirectories,
			data->dwNotifyFilter,
			&bytesReturned,
			&overlapped,
			nullptr))
		{
			goto terminate;
		}

		switch (WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE))
		{
			case WAIT_OBJECT_0:
			{
				DWORD dwBytes = 0;
				if (!GetOverlappedResult(data->directory, &overlapped, &dwBytes, TRUE))
				{
					goto terminate;
				}

				ResetEvent(data->changeEvent);

				if (dwBytes == 0)
				{
					break;
				}

				std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
				std::lock_guard<std::mutex> lock(m_changeEventsMutex);

				char *p = buffer;
				for (;;)
				{
					FILE_NOTIFY_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);

					std::wstring fileName(info->FileName, info->FileNameLength / sizeof(wchar_t));

					switch (info->Action)
					{
						case FILE_ACTION_ADDED:
						{
							if (data->notifyFilters & FSW_NOTIFY_CREATED)
							{
								auto change = std::make_unique<ChangeEvent>();
								change->flags = FSW_NOTIFY_CREATED;
								change->path = converter.to_bytes(fileName);

								m_changeEvents.push(std::move(change));
							}

							break;
						}
						case FILE_ACTION_REMOVED:
						{
							if (data->notifyFilters & FSW_NOTIFY_DELETED)
							{
								auto change = std::make_unique<ChangeEvent>();
								change->flags = FSW_NOTIFY_DELETED;
								change->path = converter.to_bytes(fileName);

								m_changeEvents.push(std::move(change));
							}
							
							break;
						}
						case FILE_ACTION_MODIFIED:
						{
							if (data->notifyFilters & FSW_NOTIFY_MODIFIED)
							{
								auto change = std::make_unique<ChangeEvent>();
								change->flags = FSW_NOTIFY_MODIFIED;
								change->path = converter.to_bytes(fileName);

								m_changeEvents.push(std::move(change));
							}

							break;
						}
						case FILE_ACTION_RENAMED_OLD_NAME:
						{
							if (data->notifyFilters & FSW_NOTIFY_RENAMED)
							{
								auto change = std::make_unique<ChangeEvent>();
								change->flags = FSW_NOTIFY_RENAMED;
								change->lastPath = converter.to_bytes(fileName);

								m_changeEvents.push(std::move(change));
							}
							
							break;
						}
						case FILE_ACTION_RENAMED_NEW_NAME:
						{
							if (data->notifyFilters & FSW_NOTIFY_RENAMED)
							{
								auto &change = m_changeEvents.back();
								change->path = converter.to_bytes(fileName);
							}
							
							break;
						}
					}

					if (!info->NextEntryOffset)
					{
						break;
					}

					p += info->NextEntryOffset;
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
				goto terminate;
				break;
			}
		}
	}
#elif defined KE_LINUX
	pollfd fds[2];

	fds[0].fd = data->fd;
	fds[0].events = POLLIN;

	fds[1].fd = m_threadCancelEventHandle;
	fds[1].events = POLLIN;

	while (true)
	{
		int poll_num = poll(fds, 2, -1);

		if (poll_num > 0)
		{
			if (fds[1].revents & POLLIN)
			{
				goto terminate;
				break;
			}

			if (fds[0].revents & POLLIN)
			{
				char buf[4096] __attribute__ ((aligned(__alignof__(inotify_event))));
				char *ptr;

				while (true)
				{
					ssize_t len = read(data->fd, buf, sizeof buf);
					
					if (len == -1 && errno != EAGAIN) 
					{
						goto terminate;
						break;
					}

					if (len <= 0)
					{
						break;
					}

					const inotify_event* event;
					for (char* ptr = buf; ptr < buf + len; ptr += sizeof(inotify_event) + event->len)
					{
						event = (const inotify_event*)ptr;

						if (event->mask & IN_IGNORED)
						{
							goto terminate;
							break;
						}

						std::unique_ptr<ChangeEvent> change = std::make_unique<ChangeEvent>();
						change->m_fullPath = event->name;

						m_changeEventsMutex.lock();
						m_changeEvents.push(std::move(change));
						m_changeEventsMutex.unlock();
					}
				}
			}
		}
	}
#endif

terminate:
	SetThreadRunning(false);
}

void FileSystemWatcher::ProcessEvents()
{
	std::lock_guard<std::mutex> lock(m_changeEventsMutex);

	while (!m_changeEvents.empty())
	{
		std::unique_ptr<ChangeEvent> &front = m_changeEvents.front();

		if (front->flags & FSW_NOTIFY_CREATED)
		{
			if (m_onCreated && m_onCreated->IsRunnable())
			{
				m_onCreated->PushCell(m_Handle);
				m_onCreated->PushString(front->path.c_str());
				m_onCreated->Execute(nullptr);
			}
		}

		if (front->flags & FSW_NOTIFY_DELETED)
		{
			if (m_onDeleted && m_onDeleted->IsRunnable())
			{
				m_onDeleted->PushCell(m_Handle);
				m_onDeleted->PushString(front->path.c_str());
				m_onDeleted->Execute(nullptr);
			}
		}

		if (front->flags & FSW_NOTIFY_MODIFIED)
		{
			if (m_onModified && m_onModified->IsRunnable())
			{
				m_onModified->PushCell(m_Handle);
				m_onModified->PushString(front->path.c_str());
				m_onModified->Execute(nullptr);
			}
		}

		if (front->flags & FSW_NOTIFY_RENAMED)
		{
			if (m_onRenamed && m_onRenamed->IsRunnable())
			{
				m_onRenamed->PushCell(m_Handle);
				m_onRenamed->PushString(front->lastPath.c_str());
				m_onRenamed->PushString(front->path.c_str());
				m_onRenamed->Execute(nullptr);
			}
		}

		m_changeEvents.pop();
	}
}

FileSystemWatcherManager g_FileSystemWatchers;
SourceMod::HandleType_t FileSystemWatcherManager::m_HandleType(0);

FileSystemWatcherManager::FileSystemWatcherManager()
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
	watcher->m_Handle = handlesys->CreateHandle(m_HandleType, watcher, context->GetIdentity(), myself->GetIdentity(), nullptr);
	m_watchers.push_back(watcher);
	return watcher->m_Handle;
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

		delete watcher;
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

cell_t Native_FileSystemWatcher_OnCreatedSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	SourcePawn::IPluginFunction* cb = context->GetFunctionById(params[2]);
	if (!cb && params[2] != -1)
	{
		context->ReportError("Invalid function id %x", params[2]);
		return 0;
	}

	watcher->m_onCreated = cb;
	return 0;
}

cell_t Native_FileSystemWatcher_OnDeletedSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	SourcePawn::IPluginFunction* cb = context->GetFunctionById(params[2]);
	if (!cb && params[2] != -1)
	{
		context->ReportError("Invalid function id %x", params[2]);
		return 0;
	}

	watcher->m_onDeleted = cb;
	return 0;
}

cell_t Native_FileSystemWatcher_OnModifiedSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	SourcePawn::IPluginFunction* cb = context->GetFunctionById(params[2]);
	if (!cb && params[2] != -1)
	{
		context->ReportError("Invalid function id %x", params[2]);
		return 0;
	}

	watcher->m_onModified = cb;
	return 0;
}

cell_t Native_FileSystemWatcher_OnRenamedSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	SourcePawn::IPluginFunction* cb = context->GetFunctionById(params[2]);
	if (!cb && params[2] != -1)
	{
		context->ReportError("Invalid function id %x", params[2]);
		return 0;
	}

	watcher->m_onRenamed = cb;
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
	{"FileSystemWatcher.OnCreated.set", Native_FileSystemWatcher_OnCreatedSet},
	{"FileSystemWatcher.OnDeleted.set", Native_FileSystemWatcher_OnDeletedSet},
	{"FileSystemWatcher.OnModified.set", Native_FileSystemWatcher_OnModifiedSet},
	{"FileSystemWatcher.OnRenamed.set", Native_FileSystemWatcher_OnRenamedSet},
	{"FileSystemWatcher.IsWatching.get", Native_FileSystemWatcher_IsWatchingGet},
	{"FileSystemWatcher.StartWatching", Native_FileSystemWatcher_StartWatching},
	{"FileSystemWatcher.StopWatching", Native_FileSystemWatcher_StopWatching},
	{NULL,			NULL},
};