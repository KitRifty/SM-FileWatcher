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
#include <os/am-path.h>

#ifdef KE_LINUX
#include <sys/eventfd.h>
#include <poll.h>
#endif

#include "helpers.h"

FileSystemWatcher::FileSystemWatcher(const std::string &relPath) :
	m_watching(false),
	m_includeSubdirectories(false),
	m_notifyFilter(FSW_NOTIFY_NONE),
	m_bufferSize(8192),
	m_Handle(0),
	m_owningContext(nullptr),
	m_onStarted(nullptr),
	m_onStopped(nullptr),
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
	m_threadRunning(false),
	m_processingEvents(false)
{
	char buffer[PLATFORM_MAX_PATH];
	ke::path::Format(buffer, sizeof(buffer), relPath.c_str());
	m_relPath = buffer;

	if (!m_relPath.empty() && (m_relPath.back() != '/' && m_relPath.back() != '\\'))
	{
#ifdef KE_WINDOWS
		m_relPath.push_back('\\');
#else
		m_relPath.push_back('/');
#endif
	}

	g_pSM->BuildPath(Path_Game, buffer, sizeof(buffer), "%s", m_relPath.c_str());

	m_path = buffer;
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

	std::unique_ptr<ThreadConfig> config = std::make_unique<ThreadConfig>();
	config->root_path = m_path;
	config->includeSubdirectories = m_includeSubdirectories;
	config->notifyFilters = m_notifyFilter;
	config->bufferSize = m_bufferSize;

#ifdef KE_WINDOWS
	if (!m_threadCancelEventHandle)
	{
		m_threadCancelEventHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	}
	else
	{
		ResetEvent(m_threadCancelEventHandle);
	}
#elif defined KE_LINUX
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
	m_thread = std::thread(&FileSystemWatcher::ThreadProc, this, std::move(config));

	return true;
}

size_t FileSystemWatcher::GetAbsolutePath(char* buffer, size_t bufferSize)
{
	return ke::SafeStrcpy(buffer, bufferSize, m_path.c_str());
}

size_t FileSystemWatcher::GetRelativePath(char* buffer, size_t bufferSize)
{
	return ke::SafeStrcpy(buffer, bufferSize, m_relPath.c_str());
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

	if (m_owningContext == context)
	{
		m_owningContext = nullptr;
		Stop();
	}

	if (m_onStarted && m_onStarted->GetParentContext() == context)
	{
		m_onStarted = nullptr;
	}

	if (m_onStopped && m_onStopped->GetParentContext() == context)
	{
		m_onStopped = nullptr;
	}

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

void FileSystemWatcher::ThreadProc(std::unique_ptr<ThreadConfig> config)
{
	class ThreadData
	{
	public:
#ifdef KE_WINDOWS
		HANDLE directory;
		HANDLE changeEvent;
#elif defined KE_LINUX
		int fd;
		int root_wd;
		std::map<int, std::string> wd;
		uint32_t mask;
#endif

		ThreadData()
		{
#ifdef KE_WINDOWS
			directory = INVALID_HANDLE_VALUE;
			changeEvent = nullptr;
#elif defined KE_LINUX
			fd = -1;
			root_wd = -1;
			mask = 0;
#endif
		}

		~ThreadData()
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
			for (auto it = wd.begin(); it != wd.end(); it++)
			{
				inotify_rm_watch(fd, it->first);
			}

			close(fd);
		}
#endif
		}

#if defined KE_LINUX
		int AddWatch(const std::string &baseRelPath, uint32_t _mask = 0)
		{
			int _wd = -1;
			_mask |= mask;
			
			std::string absPath(root_path);
			absPath.append(relPath);

			_wd = inotify_add_watch(fd, absPath.c_str(), _mask);
			if (_wd != -1)
			{
				auto it = wd.find(_wd);
				if (it != wd.end())
				{
					wd.erase(it);
				}
				
				wd.emplace(_wd, relPath);
			}

			if (includeSubdirectories)
			{
				DIR *dir;
				dirent *ent;
				if ((dir = opendir(absPath.c_str())) != nullptr) 
				{
					while ((ent = readdir(dir)) != nullptr)
					{
						if (ent->d_type == DT_DIR && ent->d_name[0] != '.')
						{
							std::string dirPath(relPath);
							dirPath.append(ent->d_name);
							dirPath.push_back('/');

							AddWatch(dirPath, mask);
						}
					}

					closedir(dir);
				}
			}

			return _wd;
		}
#endif
	};

	SetThreadRunning(true);

	{
		std::lock_guard<std::mutex> lock(m_changeEventsMutex);

		auto ev = std::make_unique<NotifyEvent>();
		ev->type = NotifyEvent::NotifyEventType::START;
		m_changeEvents.push(std::move(ev));
	}

	std::unique_ptr<ThreadData> data = std::make_unique<ThreadData>();
	std::unique_ptr<char[]> buffer(new char[config->bufferSize]);

	if (!ke::file::IsDirectory(config->root_path.c_str()))
	{
		goto terminate;
	}

#ifdef KE_WINDOWS
	DWORD dwNotifyFilter = 0;

	if (config->notifyFilters & (FSW_NOTIFY_RENAMED | FSW_NOTIFY_CREATED | FSW_NOTIFY_DELETED))
	{
		dwNotifyFilter |= FILE_NOTIFY_CHANGE_FILE_NAME;
		dwNotifyFilter |= FILE_NOTIFY_CHANGE_DIR_NAME;
	}
	
	if (config->notifyFilters & FSW_NOTIFY_MODIFIED)
	{
		dwNotifyFilter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
	}
	
	data->directory = CreateFileA(
		config->root_path.c_str(),
		FILE_LIST_DIRECTORY | GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		nullptr);

	if (data->directory == INVALID_HANDLE_VALUE)
	{
		goto terminate;
	}

	data->changeEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

	HANDLE waitHandles[2];
	waitHandles[0] = data->changeEvent;
	waitHandles[1] = m_threadCancelEventHandle;

	OVERLAPPED overlapped;
	ZeroMemory(&overlapped, sizeof overlapped);
	overlapped.hEvent = data->changeEvent;

	DWORD bytesReturned;

	while (true)
	{
		if (!ReadDirectoryChangesW(data->directory,
			buffer.get(),
			config->bufferSize,
			config->includeSubdirectories,
			dwNotifyFilter,
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
				std::vector<std::unique_ptr<NotifyEvent>> queuedEvents;

				char *p = buffer.get();
				for (;;)
				{
					FILE_NOTIFY_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(p);

					std::wstring fileName(info->FileName, info->FileNameLength / sizeof(wchar_t));

					switch (info->Action)
					{
						case FILE_ACTION_ADDED:
						{
							if (config->notifyFilters & (FSW_NOTIFY_CREATED | FSW_NOTIFY_RENAMED))
							{
								std::string path = converter.to_bytes(fileName);

								if (!queuedEvents.empty())
								{
									bool isRenameEvent = false;

									for (auto it = queuedEvents.rbegin(); it != queuedEvents.rend(); it++)
									{
										auto &change = *it;

										if (change->flags == FSW_NOTIFY_DELETED)
										{
											std::string me = GetFileNameFromPath(path);
											std::string them = GetFileNameFromPath(change->path);

											if (me == them)
											{
												change->flags = FSW_NOTIFY_RENAMED;
												change->lastPath = change->path;
												change->path = path;

												isRenameEvent = true;
												break;
											}
										}
									}

									if (isRenameEvent)
									{
										break;
									}
								}

								auto change = std::make_unique<NotifyEvent>();
								change->type = NotifyEvent::NotifyEventType::FILESYSTEM;
								change->flags = FSW_NOTIFY_CREATED;
								change->path = path;

								queuedEvents.push_back(std::move(change));
							}

							break;
						}
						case FILE_ACTION_REMOVED:
						{
							if (config->notifyFilters & (FSW_NOTIFY_DELETED | FSW_NOTIFY_RENAMED))
							{
								auto change = std::make_unique<NotifyEvent>();
								change->type = NotifyEvent::NotifyEventType::FILESYSTEM;
								change->flags = FSW_NOTIFY_DELETED;
								change->path = converter.to_bytes(fileName);

								queuedEvents.push_back(std::move(change));
							}
							
							break;
						}
						case FILE_ACTION_MODIFIED:
						{
							if (config->notifyFilters & FSW_NOTIFY_MODIFIED)
							{
								std::string path = converter.to_bytes(fileName);

								std::string absPath(config->root_path);
								absPath.append(path);

								if (ke::file::IsDirectory(absPath.c_str()))
								{
									break;
								}

								auto change = std::make_unique<NotifyEvent>();
								change->type = NotifyEvent::NotifyEventType::FILESYSTEM;
								change->flags = FSW_NOTIFY_MODIFIED;
								change->path = path;

								queuedEvents.push_back(std::move(change));
							}

							break;
						}
						case FILE_ACTION_RENAMED_OLD_NAME:
						{
							if (config->notifyFilters & FSW_NOTIFY_RENAMED)
							{
								auto change = std::make_unique<NotifyEvent>();
								change->type = NotifyEvent::NotifyEventType::FILESYSTEM;
								change->flags = FSW_NOTIFY_RENAMED;
								change->lastPath = converter.to_bytes(fileName);

								queuedEvents.push_back(std::move(change));
							}
							
							break;
						}
						case FILE_ACTION_RENAMED_NEW_NAME:
						{
							if (config->notifyFilters & FSW_NOTIFY_RENAMED)
							{
								auto &change = queuedEvents.back();
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

				std::lock_guard<std::mutex> lock(m_changeEventsMutex);

				for (auto it = queuedEvents.begin(); it != queuedEvents.end(); it++)
				{
					auto &change = *it;
					if (change->flags & config->notifyFilters)
					{
						m_changeEvents.push(std::move(*it));
					}
				}

				break;
			}
			case WAIT_OBJECT_0 + 1:
			{
				goto terminate;
			}
			default:
			{
				goto terminate;
			}
		}
	}
#elif defined KE_LINUX
	data->fd = inotify_init1(IN_NONBLOCK);
	if (data->fd == -1)
	{
		goto terminate;
	}

	data->mask = 0;

	if (config->includeSubdirectories)
	{
		data->mask |= (IN_CREATE | IN_MOVE);
	}
	else
	{
		if (config->notifyFilters & FSW_NOTIFY_CREATED)
		{
			data->mask |= IN_CREATE;
		}

		if (config->notifyFilters & FSW_NOTIFY_RENAMED)
		{
			data->mask |= IN_MOVE;
		}
	}

	if (config->notifyFilters & FSW_NOTIFY_DELETED)
	{
		data->mask |= IN_DELETE;
	}

	if (config->notifyFilters & FSW_NOTIFY_MODIFIED)
	{
		data->mask |= IN_CLOSE_WRITE;
	}

	data->root_wd = data->AddWatch("", IN_DELETE_SELF | IN_MOVE_SELF);
	if (data->root_wd == -1)
	{
		goto terminate;
	}

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
			}

			if (fds[0].revents & POLLIN)
			{
				std::vector<std::unique_ptr<NotifyEvent>> queuedEvents;

				for (;;)
				{
					ssize_t len = read(data->fd, buffer.get(), config->bufferSize);
					
					if (len == -1 && errno != EAGAIN) 
					{
						goto terminate;
					}

					if (len <= 0)
					{
						break;
					}

					const inotify_event* event;
					for (char* p = buffer.get(); p < buffer.get() + len; p += sizeof(inotify_event) + event->len)
					{
						event = (const inotify_event*)p;

						if (event->mask & (IN_MOVE_SELF | IN_DELETE_SELF | IN_IGNORED))
						{
							auto it = data->wd.find(event->wd);
							if (it != data->wd.end())
							{
								if (event->mask & IN_MOVE_SELF)
								{
									inotify_rm_watch(data->fd, event->wd);
								}

								data->wd.erase(it);
							}

							if (event->wd == data->root_wd)
							{
								goto terminate;
							}
							else
							{
								continue;
							}
						}

						std::string &baseRelPath = data->wd.at(event->wd);

						if (event->mask & (IN_CREATE | IN_MOVED_TO))
						{
							if (config->includeSubdirectories && (event->mask & IN_ISDIR))
							{
								std::string relPath(baseRelPath);
								relPath.append(event->name);
								relPath.push_back('/');

								data->AddWatch(relPath);
							}

							if (event->mask & IN_MOVED_TO)
							{
								bool foundRenamedEvent = false;

								for (auto it = queuedEvents.rbegin(); it != queuedEvents.rend(); it++)
								{
									auto &change = *it;
									if (change->cookie == event->cookie)
									{
										change->flags = FSW_NOTIFY_RENAMED;
										change->cookie = 0;
										change->lastPath = change->path;
										change->path = baseRelPath;
										change->path.append(event->name);

										foundRenamedEvent = true;
										break;
									}
								}

								if (foundRenamedEvent)
								{
									continue;
								}
							}

							std::unique_ptr<NotifyEvent> change = std::make_unique<NotifyEvent>();
							change->type = NotifyEvent::NotifyEventType::FILESYSTEM;
							change->flags = FSW_NOTIFY_CREATED;
							change->cookie = event->cookie;
							change->path = baseRelPath;
							change->path.append(event->name);

							queuedEvents.push_back(std::move(change));
						}

						if (event->mask & (IN_DELETE | IN_MOVED_FROM))
						{
							if (event->mask & IN_MOVED_FROM)
							{
								bool foundRenamedEvent = false;

								for (auto it = queuedEvents.rbegin(); it != queuedEvents.rend(); it++)
								{
									auto &change = *it;
									if (change->cookie == event->cookie)
									{
										change->flags = FSW_NOTIFY_RENAMED;
										change->cookie = 0;
										change->lastPath = change->path;
										change->path = baseRelPath;
										change->path.append(event->name);

										foundRenamedEvent = true;
										break;
									}
								}

								if (foundRenamedEvent)
								{
									continue;
								}
							}

							std::unique_ptr<NotifyEvent> change = std::make_unique<NotifyEvent>();
							change->type = NotifyEvent::NotifyEventType::FILESYSTEM;
							change->flags = FSW_NOTIFY_DELETED;
							change->cookie = event->cookie;
							change->path = baseRelPath;
							change->path.append(event->name);

							queuedEvents.push_back(std::move(change));
						}

						if (event->mask & IN_CLOSE_WRITE)
						{
							if (config->notifyFilters & FSW_NOTIFY_MODIFIED)
							{
								std::unique_ptr<NotifyEvent> change = std::make_unique<NotifyEvent>();
								change->type = NotifyEvent::NotifyEventType::FILESYSTEM;
								change->flags = FSW_NOTIFY_MODIFIED;
								change->cookie = event->cookie;
								change->path = baseRelPath;
								change->path.append(event->name);

								queuedEvents.push_back(std::move(change));
							}
						}
					}
				}

				std::lock_guard<std::mutex> lock(m_changeEventsMutex);

				for (auto it = queuedEvents.begin(); it != queuedEvents.end(); it++)
				{
					auto &change = *it;
					if (change->flags & config->notifyFilters)
					{
						m_changeEvents.push(std::move(*it));
					}
				}
			}
		}
	}
#endif

terminate:
	SetThreadRunning(false);

	{
		std::lock_guard<std::mutex> lock(m_changeEventsMutex);

		auto ev = std::make_unique<NotifyEvent>();
		ev->type = NotifyEvent::NotifyEventType::EXIT;
		m_changeEvents.push(std::move(ev));
	}
}

void FileSystemWatcher::ProcessEvents()
{
	if (m_processingEvents)
	{
		return;
	}

	std::lock_guard<std::mutex> lock(m_changeEventsMutex);

	m_processingEvents = true;

	while (!m_changeEvents.empty())
	{
		std::unique_ptr<NotifyEvent> &front = m_changeEvents.front();

		switch (front->type)
		{
			case NotifyEvent::NotifyEventType::FILESYSTEM:
			{
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

				break;
			}
			case NotifyEvent::NotifyEventType::START:
			{
				if (m_onStarted && m_onStarted->IsRunnable())
				{
					m_onStarted->PushCell(m_Handle);
					m_onStarted->Execute(nullptr);
				}

				break;
			}
			case NotifyEvent::NotifyEventType::EXIT:
			{
				if (m_onStopped && m_onStopped->IsRunnable())
				{
					m_onStopped->PushCell(m_Handle);
					m_onStopped->Execute(nullptr);
				}

				break;
			}
		}

		m_changeEvents.pop();
	}

	m_processingEvents = false;
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

SourceMod::Handle_t FileSystemWatcherManager::CreateWatcher(SourcePawn::IPluginContext* context, const std::string &path)
{
	FileSystemWatcher* watcher = new FileSystemWatcher(path);
	watcher->m_owningContext = context;
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
	char* _path = nullptr;
	context->LocalToString(params[1], &_path);

	std::string path(_path);

	return g_FileSystemWatchers.CreateWatcher(context, path);
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

cell_t Native_FileSystemWatcher_InternalBufferSizeGet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	return (cell_t)watcher->m_bufferSize;
}

cell_t Native_FileSystemWatcher_InternalBufferSizeSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	watcher->m_bufferSize = params[2];
	return 0;
}

cell_t Native_FileSystemWatcher_OnStartedSet(SourcePawn::IPluginContext *context, const cell_t *params)
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

	watcher->m_onStarted = cb;
	return 0;
}

cell_t Native_FileSystemWatcher_OnStoppedSet(SourcePawn::IPluginContext *context, const cell_t *params)
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

	watcher->m_onStopped = cb;
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

cell_t Native_FileSystemWatcher_IsWatchingSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	bool state = params[2] != 0;

	if (state)
	{
		watcher->Start();
	}
	else
	{
		watcher->Stop();
	}

	return 0;
}

cell_t Native_FileSystemWatcher_GetPath(SourcePawn::IPluginContext *context, const cell_t *params)
{
	FileSystemWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	char* buffer = nullptr;
	context->LocalToString(params[2], &buffer);

	int bufferSize = params[3];

	return watcher->GetRelativePath(buffer, bufferSize);
}

sp_nativeinfo_s FileSystemWatcherManager::m_Natives[] =
{
	{"FileSystemWatcher.FileSystemWatcher",	Native_FileSystemWatcher},
	{"FileSystemWatcher.IsWatching.get", Native_FileSystemWatcher_IsWatchingGet},
	{"FileSystemWatcher.IsWatching.set", Native_FileSystemWatcher_IsWatchingSet},
	{"FileSystemWatcher.IncludeSubdirectories.get", Native_FileSystemWatcher_IncludeSubdirectoriesGet},
	{"FileSystemWatcher.IncludeSubdirectories.set", Native_FileSystemWatcher_IncludeSubdirectoriesSet},
	{"FileSystemWatcher.NotifyFilter.get", Native_FileSystemWatcher_NotifyFilterGet},
	{"FileSystemWatcher.NotifyFilter.set", Native_FileSystemWatcher_NotifyFilterSet},
	{"FileSystemWatcher.InternalBufferSize.get", Native_FileSystemWatcher_InternalBufferSizeGet},
	{"FileSystemWatcher.InternalBufferSize.set", Native_FileSystemWatcher_InternalBufferSizeSet},
	{"FileSystemWatcher.OnStarted.set", Native_FileSystemWatcher_OnStartedSet},
	{"FileSystemWatcher.OnStopped.set", Native_FileSystemWatcher_OnStoppedSet},
	{"FileSystemWatcher.OnCreated.set", Native_FileSystemWatcher_OnCreatedSet},
	{"FileSystemWatcher.OnDeleted.set", Native_FileSystemWatcher_OnDeletedSet},
	{"FileSystemWatcher.OnModified.set", Native_FileSystemWatcher_OnModifiedSet},
	{"FileSystemWatcher.OnRenamed.set", Native_FileSystemWatcher_OnRenamedSet},
	{"FileSystemWatcher.GetPath", Native_FileSystemWatcher_GetPath},
	{NULL,			NULL},
};