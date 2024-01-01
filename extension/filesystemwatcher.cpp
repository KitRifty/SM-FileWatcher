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
#include "smsdk_ext.h"

namespace fs = std::filesystem;

SMDirectoryWatcher::SMDirectoryWatcher(const fs::path &path)
    : DirectoryWatcher(),
      gamePath(fs::path(path).lexically_normal()),
      watching(false),
      options{false, true, kNone, 8192},
      handle(0),
      owningContext(nullptr),
      onStarted(nullptr),
      onStopped(nullptr),
      onCreated(nullptr),
      onDeleted(nullptr),
      onModified(nullptr),

      onRenamed(nullptr)
{
}

bool SMDirectoryWatcher::Start()
{
    if (IsWatching())
    {
        return true;
    }

    fs::path absPath(g_pSM->GetGamePath());
    absPath = absPath.lexically_normal() / gamePath;

    if (!Watch(absPath, options))
    {
        return false;
    }

    watching = true;

    if (onStarted && onStarted->IsRunnable())
    {
        onStarted->PushCell(handle);
        onStarted->Execute(nullptr);
    }

    return true;
}

void SMDirectoryWatcher::Stop()
{
    if (!IsWatching())
    {
        return;
    }

    watching = false;
    StopWatching();

    if (onStopped && onStopped->IsRunnable())
    {
        onStopped->PushCell(handle);
        onStopped->Execute(nullptr);
    }
}

void SMDirectoryWatcher::OnGameFrame(bool simulating)
{
    if (IsWatching())
    {
        ProcessEvents();
    }
}

void SMDirectoryWatcher::OnPluginUnloaded(SourceMod::IPlugin *plugin)
{
    IPluginContext *context = plugin->GetBaseContext();

    if (owningContext == context)
    {
        owningContext = nullptr;
        Stop();
    }

    if (onStarted && onStarted->GetParentContext() == context)
    {
        onStarted = nullptr;
    }

    if (onStopped && onStopped->GetParentContext() == context)
    {
        onStopped = nullptr;
    }

    if (onCreated && onCreated->GetParentContext() == context)
    {
        onCreated = nullptr;
    }

    if (onDeleted && onDeleted->GetParentContext() == context)
    {
        onDeleted = nullptr;
    }

    if (onModified && onModified->GetParentContext() == context)
    {
        onModified = nullptr;
    }

    if (onRenamed && onRenamed->GetParentContext() == context)
    {
        onRenamed = nullptr;
    }
}

void SMDirectoryWatcher::OnProcessEvent(const NotifyEvent &event)
{
    switch (event.type)
    {
    case kFilesystem:
    {
        if (event.flags & kCreated)
        {
            if (onCreated && onCreated->IsRunnable())
            {
                auto relPath = fs::path(event.path).lexically_relative(g_pSM->GetGamePath() / gamePath).string();

                onCreated->PushCell(handle);
                onCreated->PushString(relPath.c_str());
                onCreated->Execute(nullptr);
            }
        }

        if (event.flags & kDeleted)
        {
            if (onDeleted && onDeleted->IsRunnable())
            {
                auto relPath = fs::path(event.path).lexically_relative(g_pSM->GetGamePath() / gamePath).string();

                onDeleted->PushCell(handle);
                onDeleted->PushString(relPath.c_str());
                onDeleted->Execute(nullptr);
            }
        }

        if (event.flags & kModified)
        {
            if (onModified && onModified->IsRunnable())
            {
                auto relPath = fs::path(event.path).lexically_relative(g_pSM->GetGamePath() / gamePath).string();

                onModified->PushCell(handle);
                onModified->PushString(relPath.c_str());
                onModified->Execute(nullptr);
            }
        }

        if (event.flags & kRenamed)
        {
            if (onRenamed && onRenamed->IsRunnable())
            {
                auto relPath = fs::path(event.path).lexically_relative(g_pSM->GetGamePath() / gamePath).string();
                auto relLastPath = fs::path(event.lastPath).lexically_relative(g_pSM->GetGamePath() / gamePath).string();

                onRenamed->PushCell(handle);
                onRenamed->PushString(relLastPath.c_str());
                onRenamed->PushString(relPath.c_str());
                onRenamed->Execute(nullptr);
            }
        }

        break;
    }
    }
}

SMDirectoryWatcherManager g_FileSystemWatchers;
SourceMod::HandleType_t SMDirectoryWatcherManager::m_HandleType(0);

SMDirectoryWatcherManager::SMDirectoryWatcherManager() {}

static void GameFrameHook(bool simulating)
{
    g_FileSystemWatchers.OnGameFrame(simulating);
}

bool SMDirectoryWatcherManager::SDK_OnLoad(char *error, int errorSize)
{
    m_HandleType =
        g_pHandleSys->CreateType("FileSystemWatcher", this, 0, nullptr, nullptr,
                                 myself->GetIdentity(), nullptr);
    if (!m_HandleType)
    {
        std::snprintf(error, errorSize,
                      "Failed to create FileSystemWatcher handle type.");
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

SourceMod::Handle_t SMDirectoryWatcherManager::CreateWatcher(
    SourcePawn::IPluginContext *context,
    const fs::path &path)
{
    SMDirectoryWatcher *watcher = new SMDirectoryWatcher(path);
    watcher->owningContext = context;
    watcher->handle =
        handlesys->CreateHandle(m_HandleType, watcher, context->GetIdentity(),
                                myself->GetIdentity(), nullptr);
    m_watchers.push_back(watcher);
    return watcher->handle;
}

SMDirectoryWatcher *SMDirectoryWatcherManager::GetWatcher(
    SourceMod::Handle_t handle)
{
    SMDirectoryWatcher *watcher = nullptr;
    HandleSecurity sec(nullptr, myself->GetIdentity());

    SourceMod::HandleError err =
        g_pHandleSys->ReadHandle(handle, m_HandleType, &sec, (void **)(&watcher));
    return (err == HandleError_None) ? watcher : nullptr;
}

void SMDirectoryWatcherManager::OnHandleDestroy(SourceMod::HandleType_t type,
                                                void *object)
{
    if (type == m_HandleType)
    {
        SMDirectoryWatcher *watcher = (SMDirectoryWatcher *)object;

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

void SMDirectoryWatcherManager::OnPluginUnloaded(SourceMod::IPlugin *plugin)
{
    for (auto it = m_watchers.begin(); it != m_watchers.end(); it++)
    {
        (*it)->OnPluginUnloaded(plugin);
    }
}

cell_t smn_FileSystemWatcher(SourcePawn::IPluginContext *context,
                             const cell_t *params)
{
    char *_path = nullptr;
    context->LocalToString(params[1], &_path);

    fs::path path = fs::path(_path).lexically_normal();
    if (!path.empty() && *path.begin() == "..")
    {
        context->ReportError("Path \"%s\" is invalid: path must be within the game directory", path.string().c_str());
        return 0;
    }

    return g_FileSystemWatchers.CreateWatcher(context, path);
}

cell_t smn_IncludeSubdirGet(SourcePawn::IPluginContext *context,
                            const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    return watcher->options.subtree;
}

cell_t smn_IncludeSubdirSet(SourcePawn::IPluginContext *context,
                            const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    watcher->options.subtree = params[2] != 0;
    return 0;
}

cell_t smn_WatchSymLinksGet(SourcePawn::IPluginContext *context,
                            const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    return watcher->options.symlinks;
}

cell_t smn_WatchSymLinksSet(SourcePawn::IPluginContext *context,
                            const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    watcher->options.symlinks = params[2] != 0;
    return 0;
}

cell_t smn_NotifyFilterGet(SourcePawn::IPluginContext *context,
                           const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    return (cell_t)watcher->options.notifyFilterFlags;
}

cell_t smn_NotifyFilterSet(SourcePawn::IPluginContext *context,
                           const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    watcher->options.notifyFilterFlags =
        (DirectoryWatcher::NotifyFilterFlags)params[2];
    return 0;
}

cell_t smn_RetryIntervalGet(SourcePawn::IPluginContext *context,
                            const cell_t *params)
{
    return 0;
}

cell_t smn_RetryIntervalSet(SourcePawn::IPluginContext *context,
                            const cell_t *params)
{
    return 0;
}

cell_t smn_InternalBufferSizeGet(SourcePawn::IPluginContext *context,
                                 const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    return (cell_t)watcher->options.bufferSize;
}

cell_t smn_InternalBufferSizeSet(SourcePawn::IPluginContext *context,
                                 const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    watcher->options.bufferSize = params[2];
    return 0;
}

cell_t smn_OnStartedSet(SourcePawn::IPluginContext *context,
                        const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    SourcePawn::IPluginFunction *cb = context->GetFunctionById(params[2]);
    if (!cb && params[2] != -1)
    {
        context->ReportError("Invalid function id %x", params[2]);
        return 0;
    }

    watcher->onStarted = cb;
    return 0;
}

cell_t smn_OnStoppedSet(SourcePawn::IPluginContext *context,
                        const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    SourcePawn::IPluginFunction *cb = context->GetFunctionById(params[2]);
    if (!cb && params[2] != -1)
    {
        context->ReportError("Invalid function id %x", params[2]);
        return 0;
    }

    watcher->onStopped = cb;
    return 0;
}

cell_t smn_OnCreatedSet(SourcePawn::IPluginContext *context,
                        const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    SourcePawn::IPluginFunction *cb = context->GetFunctionById(params[2]);
    if (!cb && params[2] != -1)
    {
        context->ReportError("Invalid function id %x", params[2]);
        return 0;
    }

    watcher->onCreated = cb;
    return 0;
}

cell_t smn_OnDeletedSet(SourcePawn::IPluginContext *context,
                        const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    SourcePawn::IPluginFunction *cb = context->GetFunctionById(params[2]);
    if (!cb && params[2] != -1)
    {
        context->ReportError("Invalid function id %x", params[2]);
        return 0;
    }

    watcher->onDeleted = cb;
    return 0;
}

cell_t smn_OnModifiedSet(SourcePawn::IPluginContext *context,
                         const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    SourcePawn::IPluginFunction *cb = context->GetFunctionById(params[2]);
    if (!cb && params[2] != -1)
    {
        context->ReportError("Invalid function id %x", params[2]);
        return 0;
    }

    watcher->onModified = cb;
    return 0;
}

cell_t smn_OnRenamedSet(SourcePawn::IPluginContext *context,
                        const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    SourcePawn::IPluginFunction *cb = context->GetFunctionById(params[2]);
    if (!cb && params[2] != -1)
    {
        context->ReportError("Invalid function id %x", params[2]);
        return 0;
    }

    watcher->onRenamed = cb;
    return 0;
}

cell_t smn_IsWatchingGet(SourcePawn::IPluginContext *context,
                         const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    return watcher->IsWatching();
}

cell_t smn_IsWatchingSet(SourcePawn::IPluginContext *context,
                         const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
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

cell_t smn_GetPath(SourcePawn::IPluginContext *context, const cell_t *params)
{
    SMDirectoryWatcher *watcher = g_FileSystemWatchers.GetWatcher(params[1]);
    if (!watcher)
    {
        context->ReportError("Invalid FileSystemWatcher handle %x", params[1]);
        return 0;
    }

    size_t writtenBytes;
    context->StringToLocalUTF8(params[2], params[3], watcher->gamePath.string().c_str(), &writtenBytes);
    return writtenBytes;
}

sp_nativeinfo_s SMDirectoryWatcherManager::m_Natives[] = {
    {"FileSystemWatcher.FileSystemWatcher", smn_FileSystemWatcher},
    {"FileSystemWatcher.IsWatching.get", smn_IsWatchingGet},
    {"FileSystemWatcher.IsWatching.set", smn_IsWatchingSet},
    {"FileSystemWatcher.IncludeSubdirectories.get", smn_IncludeSubdirGet},
    {"FileSystemWatcher.IncludeSubdirectories.set", smn_IncludeSubdirSet},
    {"FileSystemWatcher.WatchDirectoryLinks.get", smn_WatchSymLinksGet},
    {"FileSystemWatcher.WatchDirectoryLinks.set", smn_WatchSymLinksSet},
    {"FileSystemWatcher.NotifyFilter.get", smn_NotifyFilterGet},
    {"FileSystemWatcher.NotifyFilter.set", smn_NotifyFilterSet},
    {"FileSystemWatcher.RetryInterval.get", smn_RetryIntervalGet},
    {"FileSystemWatcher.RetryInterval.set", smn_RetryIntervalSet},
    {"FileSystemWatcher.InternalBufferSize.get", smn_InternalBufferSizeGet},
    {"FileSystemWatcher.InternalBufferSize.set", smn_InternalBufferSizeSet},
    {"FileSystemWatcher.OnStarted.set", smn_OnStartedSet},
    {"FileSystemWatcher.OnStopped.set", smn_OnStoppedSet},
    {"FileSystemWatcher.OnCreated.set", smn_OnCreatedSet},
    {"FileSystemWatcher.OnDeleted.set", smn_OnDeletedSet},
    {"FileSystemWatcher.OnModified.set", smn_OnModifiedSet},
    {"FileSystemWatcher.OnRenamed.set", smn_OnRenamedSet},
    {"FileSystemWatcher.GetPath", smn_GetPath},
    {NULL, NULL},
};