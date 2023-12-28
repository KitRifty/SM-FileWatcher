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

#include "watcher.h"

#include <string>

#ifdef __linux__
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/eventfd.h>
#include <unistd.h>
#else
#include <codecvt>
#endif

namespace fs = std::filesystem;

DirectoryWatcher::DirectoryWatcher(const fs::path& absPath) :
	m_watching(false),
	m_includeSubdirectories(false),
	m_notifyFilter(FSW_NOTIFY_NONE),
	m_bufferSize(8192),
	m_retryInterval(1000),
#ifdef __linux__
	m_threadCancelEventHandle(-1),
#else
	m_threadCancelEventHandle(nullptr),
#endif
	m_thread(),
	m_threadRunning(false),
	m_processingEvents(false)
{
	m_path = absPath.lexically_normal();
}

DirectoryWatcher::~DirectoryWatcher()
{
	Stop();

#ifdef __linux__
	if (fcntl(m_threadCancelEventHandle, F_GETFD) != -1)
	{
		close(m_threadCancelEventHandle);
	}
#else
	if (m_threadCancelEventHandle)
	{
		CloseHandle(m_threadCancelEventHandle);
	}
#endif
}

bool DirectoryWatcher::Start()
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
	config->retryInterval = m_retryInterval;

#ifdef __linux__
	if (fcntl(m_threadCancelEventHandle, F_GETFD) == -1)
	{
		m_threadCancelEventHandle = eventfd(0, 0);
	}
	else
	{
		uint64_t u;
		read(m_threadCancelEventHandle, &u, sizeof(u));
	}
#else
	if (!m_threadCancelEventHandle)
	{
		m_threadCancelEventHandle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	}
	else
	{
		ResetEvent(m_threadCancelEventHandle);
	}
#endif

	m_watching = true;
	m_thread = std::thread(&DirectoryWatcher::ThreadProc, this, std::move(config));

	return true;
}

void DirectoryWatcher::Stop()
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

bool DirectoryWatcher::IsThreadRunning()
{
	std::lock_guard<std::mutex> lock(m_threadRunningMutex);
	return m_threadRunning;
}

void DirectoryWatcher::SetThreadRunning(bool state)
{
	std::lock_guard<std::mutex> lock(m_threadRunningMutex);
	m_threadRunning = state;
}

void DirectoryWatcher::RequestCancelThread()
{
#ifdef __linux__
	uint64_t u = 1;
	write(m_threadCancelEventHandle, &u, sizeof(u));
#else
	SetEvent(m_threadCancelEventHandle);
#endif
}

void DirectoryWatcher::ThreadProc(std::unique_ptr<ThreadConfig> config)
{
	class ThreadData
	{
	public:
#ifdef __linux__
		int inotify_fd;
		int inotify_root_wd;
		std::map<int, fs::path> wd;
		uint32_t mask;
#else
		HANDLE directory;
		HANDLE changeEvent;
#endif

		ThreadData()
		{
#ifdef __linux__
			inotify_fd = -1;
			inotify_root_wd = -1;
			mask = 0;
#else
			directory = INVALID_HANDLE_VALUE;
			changeEvent = nullptr;
#endif
		}

		~ThreadData()
		{
#ifdef __linux__
			if (fcntl(inotify_fd, F_GETFD) != -1)
			{
				for (auto it = wd.begin(); it != wd.end(); it++)
				{
					inotify_rm_watch(inotify_fd, it->first);
				}

				close(inotify_fd);
			}
#else
			if (changeEvent && changeEvent != INVALID_HANDLE_VALUE)
			{
				CloseHandle(changeEvent);
			}

			if (directory && directory != INVALID_HANDLE_VALUE)
			{
				CloseHandle(directory);
			}
#endif
		}

#ifdef __linux__
		int AddWatch(const std::unique_ptr<ThreadConfig> &config, const fs::path &path, uint32_t _mask = 0)
#else
		HANDLE AddWatch(const std::unique_ptr<ThreadConfig> &config, const fs::path &path, DWORD _dwNotifyFilter = 0)
#endif
		{
			fs::path lexicalAbsPath(config->root_path);
			lexicalAbsPath /= path;

#ifdef __linux__
			int _wd = -1;
			_mask |= mask;

			_wd = inotify_add_watch(inotify_fd, lexicalAbsPath.c_str(), _mask);
			if (_wd != -1)
			{
				auto it = wd.find(_wd);
				if (it != wd.end())
				{
					wd.erase(it);
				}

				wd.emplace(_wd, path.lexically_normal());
			}
#else
			// TODO
#endif

			if (config->includeSubdirectories)
			{
				for (const auto& entry : fs::recursive_directory_iterator(lexicalAbsPath))
				{
					fs::path initialPath = entry;
					fs::path entryPath = entry;

					if (entry.is_symlink())
					{
						entryPath = fs::read_symlink(entry);
						if (!fs::exists(entryPath))
						{
							continue;
						}
					}
#ifndef __linux__
					else
					{
						continue;
					}
#endif

					if (!fs::is_directory(entryPath))
					{
						continue;
					}

					bool old = config->includeSubdirectories;
					config->includeSubdirectories = false;
#ifdef __linux__
					AddWatch(config, initialPath.lexically_relative(config->root_path), mask);
#else
					// TODO
#endif
					config->includeSubdirectories = old;
				}
			}

#ifdef __linux__
			return _wd;
#else
			return INVALID_HANDLE_VALUE;
#endif
		}
	};

	bool isRunning = true;

	while (isRunning)
	{
		SetThreadRunning(true);

		bool didStart = false;

		std::unique_ptr<ThreadData> data = std::make_unique<ThreadData>();
		std::unique_ptr<char[]> buffer(new char[config->bufferSize]);

		if (!fs::is_directory(config->root_path))
		{
			goto terminate;
		}

#ifdef __linux__
		data->inotify_fd = inotify_init1(IN_NONBLOCK);
		if (data->inotify_fd == -1)
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

		data->inotify_root_wd = data->AddWatch(config, "", IN_DELETE_SELF | IN_MOVE_SELF);
		if (data->inotify_root_wd == -1)
		{
			goto terminate;
		}
#else
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
			config->root_path.string().c_str(),
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
#endif

		didStart = true;

		{
			std::lock_guard<std::mutex> lock(m_changeEventsMutex);

			auto ev = std::make_unique<NotifyEvent>();
			ev->type = NotifyEvent::NotifyEventType::START;
			m_changeEvents.push(std::move(ev));
		}

#ifdef __linux__
		pollfd fds[2];

		fds[0].fd = data->inotify_fd;
		fds[0].events = POLLIN;

		fds[1].fd = m_threadCancelEventHandle;
		fds[1].events = POLLIN;
#else
		data->changeEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

		HANDLE waitHandles[2];
		waitHandles[0] = data->changeEvent;
		waitHandles[1] = m_threadCancelEventHandle;

		OVERLAPPED overlapped;
		ZeroMemory(&overlapped, sizeof overlapped);
		overlapped.hEvent = data->changeEvent;
#endif

		while (true)
		{
#ifdef __linux__
			if (poll(fds, 2, -1) > 0)
			{
				if (fds[1].revents & POLLIN)
				{
					isRunning = false;
					goto terminate;
				}

				if (fds[0].revents & POLLIN)
				{
					std::vector<std::unique_ptr<NotifyEvent>> queuedEvents;

					for (;;)
					{
						ssize_t len = read(data->inotify_fd, buffer.get(), config->bufferSize);

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
										inotify_rm_watch(data->inotify_fd, event->wd);
									}

									data->wd.erase(it);
								}

								if (event->wd == data->inotify_root_wd)
								{
									goto terminate;
								}
								else
								{
									continue;
								}
							}

							auto &baseRelPath = data->wd.at(event->wd);

							if (event->mask & (IN_CREATE | IN_MOVED_TO))
							{
								if (config->includeSubdirectories && (event->mask & IN_ISDIR))
								{
									data->AddWatch(config, baseRelPath / event->name);
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
											change->path = baseRelPath / event->name;

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
								change->path = baseRelPath / event->name;

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
											change->path = baseRelPath / event->name;

											foundRenamedEvent = true;
											break;
										}
									}

									if (foundRenamedEvent)
									{
										continue;
									}
								}

								auto change = std::make_unique<NotifyEvent>();
								change->type = NotifyEvent::NotifyEventType::FILESYSTEM;
								change->flags = FSW_NOTIFY_DELETED;
								change->cookie = event->cookie;
								change->path = baseRelPath / event->name;

								queuedEvents.push_back(std::move(change));
							}

							if (event->mask & IN_CLOSE_WRITE)
							{
								if (config->notifyFilters & FSW_NOTIFY_MODIFIED)
								{
									auto change = std::make_unique<NotifyEvent>();
									change->type = NotifyEvent::NotifyEventType::FILESYSTEM;
									change->flags = FSW_NOTIFY_MODIFIED;
									change->cookie = event->cookie;
									change->path = baseRelPath / event->name;

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
#else
			if (!ReadDirectoryChangesExW(data->directory,
				buffer.get(),
				config->bufferSize,
				config->includeSubdirectories,
				dwNotifyFilter,
				nullptr,
				&overlapped,
				nullptr,
				ReadDirectoryNotifyExtendedInformation))
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
						FILE_NOTIFY_EXTENDED_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_EXTENDED_INFORMATION*>(p);

						std::wstring fileName(info->FileName, info->FileNameLength / sizeof(wchar_t));

						switch (info->Action)
						{
							case FILE_ACTION_ADDED:
							{
								if (config->notifyFilters & (FSW_NOTIFY_CREATED | FSW_NOTIFY_RENAMED))
								{
									fs::path path = converter.to_bytes(fileName);

									if (!queuedEvents.empty())
									{
										bool isRenameEvent = false;

										for (auto it = queuedEvents.rbegin(); it != queuedEvents.rend(); it++)
										{
											auto &change = *it;

											if (change->flags == FSW_NOTIFY_DELETED)
											{
												std::string me = path.filename().string();
												std::string them = fs::path(change->path).filename().string();

												if (me == them)
												{
													change->flags = FSW_NOTIFY_RENAMED;
													change->lastPath = change->path;
													change->path = path.string();

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
									change->path = path.string();

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
									if (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
									{
										break;
									}

									auto change = std::make_unique<NotifyEvent>();
									change->type = NotifyEvent::NotifyEventType::FILESYSTEM;
									change->flags = FSW_NOTIFY_MODIFIED;
									change->path = converter.to_bytes(fileName);

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
					isRunning = false;
					goto terminate;
				}
				default:
				{
					goto terminate;
				}
			}
#endif
		}

terminate:
		SetThreadRunning(false);

		if (didStart)
		{
			std::lock_guard<std::mutex> lock(m_changeEventsMutex);

			auto ev = std::make_unique<NotifyEvent>();
			ev->type = NotifyEvent::NotifyEventType::EXIT;
			m_changeEvents.push(std::move(ev));
		}

		if (isRunning)
		{
#ifdef __linux__
			pollfd retry_fds[1];

			retry_fds[0].fd = m_threadCancelEventHandle;
			retry_fds[0].events = POLLIN;

			if (poll(retry_fds, 1, config->retryInterval) > 0)
			{
				if (retry_fds[0].revents & POLLIN)
				{
					isRunning = false;
				}
			}
#else
			switch (WaitForSingleObject(m_threadCancelEventHandle, config->retryInterval))
			{
				case WAIT_OBJECT_0:
				{
					isRunning = false;
					break;
				}
			}
#endif
		}
	}
}

void DirectoryWatcher::ProcessEvents()
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
		OnProcessEvent(*front.get());
		m_changeEvents.pop();
	}

	m_processingEvents = false;
}

void DirectoryWatcher::OnProcessEvent(const NotifyEvent& event)
{
}