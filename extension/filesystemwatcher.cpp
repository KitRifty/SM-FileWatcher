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
#include <cstring>

#ifdef __linux__
#else
#include <locale>
#include <codecvt>
#endif

#include "smsdk_ext.h"
#include "helpers.h"

namespace fs = std::filesystem;

SMDirectoryWatcher::SMDirectoryWatcher(const std::string& path) :
	DirectoryWatcher((fs::path(g_pSM->GetGamePath()) / path).lexically_normal()),
	m_Handle(0),
	m_owningContext(nullptr),
	m_onStarted(nullptr),
	m_onStopped(nullptr),
	m_onCreated(nullptr),
	m_onDeleted(nullptr),
	m_onModified(nullptr),
	m_onRenamed(nullptr)
{
	m_relPath = fs::path(path).lexically_normal();
}

size_t SMDirectoryWatcher::GetPath(char* buffer, size_t bufferSize)
{
	std::strncpy(buffer, m_relPath.string().c_str(), bufferSize - 1);
	buffer[bufferSize - 1] = '\0';
	return strlen(buffer) + 1;
}

void SMDirectoryWatcher::OnGameFrame(bool simulating)
{
	if (IsWatching())
	{
		ProcessEvents();
	}
}

void SMDirectoryWatcher::OnPluginUnloaded(SourceMod::IPlugin* plugin)
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

void SMDirectoryWatcher::OnProcessEvent(const NotifyEvent& event)
{
	switch (event.type)
	{
		case NotifyEvent::NotifyEventType::FILESYSTEM:
		{
			if (event.flags & FSW_NOTIFY_CREATED)
			{
				if (m_onCreated && m_onCreated->IsRunnable())
				{
					m_onCreated->PushCell(m_Handle);
					m_onCreated->PushString(event.path.c_str());
					m_onCreated->Execute(nullptr);
				}
			}

			if (event.flags & FSW_NOTIFY_DELETED)
			{
				if (m_onDeleted && m_onDeleted->IsRunnable())
				{
					m_onDeleted->PushCell(m_Handle);
					m_onDeleted->PushString(event.path.c_str());
					m_onDeleted->Execute(nullptr);
				}
			}

			if (event.flags & FSW_NOTIFY_MODIFIED)
			{
				if (m_onModified && m_onModified->IsRunnable())
				{
					m_onModified->PushCell(m_Handle);
					m_onModified->PushString(event.path.c_str());
					m_onModified->Execute(nullptr);
				}
			}

			if (event.flags & FSW_NOTIFY_RENAMED)
			{
				if (m_onRenamed && m_onRenamed->IsRunnable())
				{
					m_onRenamed->PushCell(m_Handle);
					m_onRenamed->PushString(event.lastPath.c_str());
					m_onRenamed->PushString(event.path.c_str());
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
}

SMDirectoryWatcherManager g_FileSystemWatchers;
SourceMod::HandleType_t SMDirectoryWatcherManager::m_HandleType(0);

SMDirectoryWatcherManager::SMDirectoryWatcherManager()
{
}

static void GameFrameHook(bool simulating)
{
	g_FileSystemWatchers.OnGameFrame(simulating);
}

bool SMDirectoryWatcherManager::SDK_OnLoad(char* error, int errorSize)
{
	m_HandleType = g_pHandleSys->CreateType("FileSystemWatcher", this, 0, nullptr, nullptr, myself->GetIdentity(), nullptr);
	if (!m_HandleType)
	{
		std::snprintf(error, errorSize, "Failed to create FileSystemWatcher handle type.");
		return false;
	}

	plsys->AddPluginsListener(this);

	smutils->AddGameFrameHook(&GameFrameHook);

	sharesys->AddNatives(myself, m_Natives);

	return true;
}

void SMDirectoryWatcherManager::SDK_OnUnload()
{
	smutils->RemoveGameFrameHook(&GameFrameHook);

	plsys->RemovePluginsListener(this);

	if (m_HandleType)
	{
		g_pHandleSys->RemoveType(m_HandleType, myself->GetIdentity());
	}
}

void SMDirectoryWatcherManager::OnGameFrame(bool simulating)
{
	for (auto it = m_watchers.begin(); it != m_watchers.end(); it++)
	{
		(*it)->OnGameFrame(simulating);
	}
}

SourceMod::Handle_t SMDirectoryWatcherManager::CreateWatcher(SourcePawn::IPluginContext* context, const std::string &path)
{
	SMDirectoryWatcher* watcher = new SMDirectoryWatcher(path);
	watcher->m_owningContext = context;
	watcher->m_Handle = handlesys->CreateHandle(m_HandleType, watcher, context->GetIdentity(), myself->GetIdentity(), nullptr);
	m_watchers.push_back(watcher);
	return watcher->m_Handle;
}

SMDirectoryWatcher* SMDirectoryWatcherManager::GetWatcher(SourceMod::Handle_t handle)
{
	SMDirectoryWatcher* watcher = nullptr;
	HandleSecurity sec(nullptr, myself->GetIdentity());

	SourceMod::HandleError err = g_pHandleSys->ReadHandle(handle, m_HandleType, &sec, (void**)(&watcher));
	return (err == HandleError_None) ? watcher : nullptr;
}

void SMDirectoryWatcherManager::OnHandleDestroy(SourceMod::HandleType_t type, void *object)
{
	if (type == m_HandleType)
	{
		SMDirectoryWatcher* watcher = (SMDirectoryWatcher*)object;

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

void SMDirectoryWatcherManager::OnPluginUnloaded(SourceMod::IPlugin* plugin)
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
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	return watcher->m_includeSubdirectories;
}

cell_t Native_FileSystemWatcher_IncludeSubdirectoriesSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
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
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	return (cell_t)watcher->m_notifyFilter;
}

cell_t Native_FileSystemWatcher_NotifyFilterSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	watcher->m_notifyFilter = (DirectoryWatcher::NotifyFilters)params[2];
	return 0;
}

cell_t Native_FileSystemWatcher_RetryIntervalGet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	return watcher->m_retryInterval;
}

cell_t Native_FileSystemWatcher_RetryIntervalSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	int retryInterval = params[2];
	if (retryInterval < 0)
	{
		context->ReportError("RetryInterval cannot be negative");
		return 0;
	}

	watcher->m_retryInterval = retryInterval;
	return 0;
}

cell_t Native_FileSystemWatcher_InternalBufferSizeGet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	return (cell_t)watcher->m_bufferSize;
}

cell_t Native_FileSystemWatcher_InternalBufferSizeSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
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
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
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
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
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
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
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
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
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
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
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
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
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
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	return watcher->IsWatching();
}

cell_t Native_FileSystemWatcher_IsWatchingSet(SourcePawn::IPluginContext *context, const cell_t *params)
{
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
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
	SMDirectoryWatcher* watcher = g_FileSystemWatchers.GetWatcher(params[1]);
	if (!watcher)
	{
		context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
		return 0;
	}

	char* buffer = nullptr;
	context->LocalToString(params[2], &buffer);

	int bufferSize = params[3];

	return watcher->GetPath(buffer, bufferSize);
}

sp_nativeinfo_s SMDirectoryWatcherManager::m_Natives[] =
{
	{"FileSystemWatcher.FileSystemWatcher",	Native_FileSystemWatcher},
	{"FileSystemWatcher.IsWatching.get", Native_FileSystemWatcher_IsWatchingGet},
	{"FileSystemWatcher.IsWatching.set", Native_FileSystemWatcher_IsWatchingSet},
	{"FileSystemWatcher.IncludeSubdirectories.get", Native_FileSystemWatcher_IncludeSubdirectoriesGet},
	{"FileSystemWatcher.IncludeSubdirectories.set", Native_FileSystemWatcher_IncludeSubdirectoriesSet},
	{"FileSystemWatcher.NotifyFilter.get", Native_FileSystemWatcher_NotifyFilterGet},
	{"FileSystemWatcher.NotifyFilter.set", Native_FileSystemWatcher_NotifyFilterSet},
	{"FileSystemWatcher.RetryInterval.get", Native_FileSystemWatcher_RetryIntervalGet},
	{"FileSystemWatcher.RetryInterval.set", Native_FileSystemWatcher_RetryIntervalSet},
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