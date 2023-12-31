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
#endif

#include <iostream>

namespace fs = std::filesystem;

DirectoryWatcher::Worker::Worker(
	const std::filesystem::path &path,
	const WatchOptions &_options,
	EventQueue *eventsBuffer,
	std::mutex *eventsBufferMutex) : eventsBuffer(eventsBuffer),
									 eventsBufferMutex(eventsBufferMutex),
									 basePath(path),
									 options(_options)
{
#ifdef __linux__
#else
	auto actualBasePath = fs::path(basePath);
	if (fs::is_symlink(actualBasePath))
	{
		actualBasePath = fs::read_symlink(actualBasePath);
	}

	directory = CreateFileA(
		actualBasePath.string().c_str(),
		FILE_LIST_DIRECTORY | GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
		nullptr);

	if (directory == INVALID_HANDLE_VALUE)
	{
		cancelEvent = 0;
		return;
	}
#endif

	if (options.subtree)
	{
#ifdef __linux__
#else
		if (options.symlinks)
		{
			std::queue<fs::path> dirsToTraverse;
			dirsToTraverse.push(basePath);

			while (!dirsToTraverse.empty())
			{
				fs::path currentDir = dirsToTraverse.front();
				dirsToTraverse.pop();

				for (const auto &entry : fs::directory_iterator(currentDir))
				{
					if (fs::is_directory(entry))
					{
						if (fs::is_symlink(entry))
						{
							workers.push_back(std::make_unique<Worker>(fs::path(entry), options, eventsBuffer, eventsBufferMutex));
						}
						else
						{
							dirsToTraverse.push(entry);
						}
					}
				}
			}
		}
#endif
	}

#ifdef __linux__
#else
	cancelEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
#endif

	thread = std::thread(&DirectoryWatcher::Worker::ThreadProc, this);
}

DirectoryWatcher::Worker::~Worker()
{
	workers.clear();

#ifdef __linux__
#else
	SetEvent(cancelEvent);
#endif

	if (thread.joinable())
	{
		thread.join();
	}
}

void DirectoryWatcher::Worker::ThreadProc()
{
#ifdef __linux__
#else
	auto buffer = std::make_unique<char[]>(options.bufferSize);
	ScopedHandle watchEvent(CreateEvent(nullptr, TRUE, FALSE, nullptr));

	HANDLE waitHandles[2];
	waitHandles[0] = cancelEvent;
	waitHandles[1] = watchEvent;

	OVERLAPPED overlapped;
	ZeroMemory(&overlapped, sizeof overlapped);
	overlapped.hEvent = watchEvent;
#endif

	bool running = true;

	{
		std::lock_guard<std::mutex> lock(*eventsBufferMutex);

		auto change = std::make_unique<NotifyEvent>();
		change->type = kStart;
		change->path = basePath.string();

		eventsBuffer->push(std::move(change));
	}

	while (running)
	{
#ifdef __linux__
#else
		if (!ReadDirectoryChangesExW(
				directory,
				buffer.get(),
				options.bufferSize,
				options.subtree,
				FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
				nullptr,
				&overlapped,
				nullptr,
				ReadDirectoryNotifyExtendedInformation))
		{
			running = false;
			break;
		}

		switch (WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE))
		{
		case WAIT_OBJECT_0 + 1:
		{
			DWORD dwBytes = 0;
			if (!GetOverlappedResult(directory, &overlapped, &dwBytes, TRUE))
			{
				running = false;
				break;
			}

			ResetEvent(watchEvent);

			if (dwBytes == 0)
			{
				break;
			}

			std::vector<std::unique_ptr<NotifyEvent>> queuedEvents;

			char *p = buffer.get();
			for (;;)
			{
				FILE_NOTIFY_EXTENDED_INFORMATION *info = reinterpret_cast<FILE_NOTIFY_EXTENDED_INFORMATION *>(p);
				std::wstring fileName(info->FileName, info->FileNameLength / sizeof(wchar_t));
				fs::path path = basePath / fileName;

				switch (info->Action)
				{
				case FILE_ACTION_ADDED:
				{
					auto change = std::make_unique<NotifyEvent>();
					change->type = kFilesystem;
					change->flags = kCreated;
					change->path = path.string();

					queuedEvents.push_back(std::move(change));

					if (options.subtree && options.symlinks && fs::is_symlink(path) && fs::is_directory(path))
					{
						workers.push_back(std::make_unique<Worker>(path, options, eventsBuffer, eventsBufferMutex));
					}

					break;
				}
				case FILE_ACTION_REMOVED:
				{
					auto change = std::make_unique<NotifyEvent>();
					change->type = kFilesystem;
					change->flags = kDeleted;
					change->path = path.string();

					queuedEvents.push_back(std::move(change));

					if (options.subtree)
					{
						for (auto it = workers.begin(); it != workers.end(); it++)
						{
							auto &worker = *it;

							if (!worker->IsRunning() || worker->basePath == path)
							{
								it = workers.erase(it);
							}
						}
					}

					break;
				}
				case FILE_ACTION_MODIFIED:
				{
					if (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					{
						break;
					}

					auto change = std::make_unique<NotifyEvent>();
					change->type = kFilesystem;
					change->flags = kModified;
					change->path = path.string();

					queuedEvents.push_back(std::move(change));

					break;
				}
				case FILE_ACTION_RENAMED_OLD_NAME:
				{
					auto change = std::make_unique<NotifyEvent>();
					change->type = kFilesystem;
					change->flags = kRenamed;
					change->lastPath = path.string();

					queuedEvents.push_back(std::move(change));

					break;
				}
				case FILE_ACTION_RENAMED_NEW_NAME:
				{
					auto &change = queuedEvents.back();
					change->path = path.string();

					if (options.subtree)
					{
						for (auto it = workers.begin(); it != workers.end(); it++)
						{
							auto &worker = *it;

							if (!worker->IsRunning() || worker->basePath == change->lastPath)
							{
								it = workers.erase(it);
							}
						}

						if (options.symlinks && fs::is_symlink(path) && fs::is_directory(path))
						{
							workers.push_back(std::make_unique<Worker>(path, options, eventsBuffer, eventsBufferMutex));
						}
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

			std::lock_guard<std::mutex> lock(*eventsBufferMutex);

			for (auto it = queuedEvents.begin(); it != queuedEvents.end(); it++)
			{
				auto &change = *it;
				if (change->flags & options.notifyFilterFlags)
				{
					eventsBuffer->push(std::move(*it));
				}
			}

			break;
		}
		case WAIT_FAILED:
		{
			running = false;

			std::cerr << "Failed to wait: code " << std::hex << GetLastError() << std::endl;

			break;
		}
		default:
		{
			running = false;
			break;
		}
		}
#endif
	}

	{
		std::lock_guard<std::mutex> lock(*eventsBufferMutex);

		auto change = std::make_unique<NotifyEvent>();
		change->type = kStop;
		change->path = basePath.string();

		eventsBuffer->push(std::move(change));
	}
}

DirectoryWatcher::DirectoryWatcher()
#ifdef __linux__
	: m_threadCancelEventHandle(-1),
	  m_threadRunning(false),
	  m_thread()
#else
#endif
{
	eventsBuffer = std::make_unique<EventQueue>();
	eventsBufferMutex = std::make_unique<std::mutex>();
}

DirectoryWatcher::~DirectoryWatcher()
{
#ifdef __linux__
	if (fcntl(m_threadCancelEventHandle, F_GETFD) != -1)
	{
		close(m_threadCancelEventHandle);
	}
#else
#endif

	StopWatching();
}

bool DirectoryWatcher::Watch(const std::filesystem::path &absPath, const WatchOptions &options)
{
	if (!fs::is_directory(absPath))
	{
		return false;
	}

	workers.push_back(std::make_unique<Worker>(absPath, options, eventsBuffer.get(), eventsBufferMutex.get()));

	return true;
}

bool DirectoryWatcher::IsWatching(const std::filesystem::path &absPath) const
{
	for (auto it = workers.begin(); it != workers.end(); it++)
	{
		auto &worker = *it;
		if (worker->IsRunning() && worker->basePath == absPath)
		{
			return true;
		}
	}

	return false;
}

void DirectoryWatcher::StopWatching()
{
	workers.clear();
}

/*
bool DirectoryWatcher::Start()
{
	if (IsWatching())
	{
		return true;
	}

	if (!fs::is_directory(m_path))
	{
		return false;
	}

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

	m_thread = std::thread(&DirectoryWatcher::ThreadProc, this, std::move(config));
#else
	workers.push_back(std::make_unique<Worker>(this, m_path, watchOptions));
#endif

	watching = true;

	return true;
}

void DirectoryWatcher::Stop()
{
	if (!IsWatching())
	{
		return;
	}

	watching = false;

#ifdef __linux__
	uint64_t u = 1;
	write(m_threadCancelEventHandle, &u, sizeof(u));

	if (m_thread.joinable())
	{
		m_thread.join();
	}
#else
	workers.clear();
#endif

	ProcessEvents();
}
*/

/*
void DirectoryWatcher::ThreadProc(std::unique_ptr<WatchOptions> config)
{
	class ThreadData
	{
	public:
#ifdef __linux__
		int inotify_fd;
		int inotify_root_wd;
		std::map<int, fs::path> wd;
		uint32_t mask;
#endif

		ThreadData()
		{
#ifdef __linux__
			inotify_fd = -1;
			inotify_root_wd = -1;
			mask = 0;
#else
			//directory = INVALID_HANDLE_VALUE;
			//changeEvent = nullptr;
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
			std::lock_guard<std::mutex> lock(threadsMutex);
			threads.clear();

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
		HANDLE AddWatch(const std::unique_ptr<WatchOptions> &config, const fs::path &path, DWORD _dwNotifyFilter = 0)
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
									fs::path path(fileName);

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
									change->path = fs::path(fileName).string();

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
									change->path = fs::path(fileName).string();

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
									change->lastPath = fs::path(fileName).string();

									queuedEvents.push_back(std::move(change));
								}

								break;
							}
							case FILE_ACTION_RENAMED_NEW_NAME:
							{
								if (config->notifyFilters & FSW_NOTIFY_RENAMED)
								{
									auto &change = queuedEvents.back();
									change->path = fs::path(fileName).string();
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
*/

void DirectoryWatcher::ProcessEvents()
{
	std::lock_guard<std::mutex> lock(*eventsBufferMutex.get());

	while (!eventsBuffer->empty())
	{
		auto &front = eventsBuffer->front();
		OnProcessEvent(*front.get());
		eventsBuffer->pop();
	}
}

void DirectoryWatcher::OnProcessEvent(const NotifyEvent &event)
{
}