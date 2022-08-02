
#include "filesystemwatcher.h"
#include "smsdk_ext.h"
#include "am-string.h"

FileSystemWatcher::FileSystemWatcher()
{
}

FileSystemWatcher::~FileSystemWatcher()
{
#ifdef KE_WINDOWS

#endif
}

bool FileSystemWatcher::AddPath(const char* path, bool watchSubTree)
{
	m_paths.push_back(std::make_unique<PathInfo>(path, watchSubTree));
	return true;
}

FileSystemWatcherManager g_FileSystemWatchers;

FileSystemWatcherManager::FileSystemWatcherManager() : 
	m_HandleType(0)
{
}

bool FileSystemWatcherManager::SDK_OnLoad(char* error, int errorSize)
{
	SourceMod::HandleError err;
	m_HandleType = g_pHandleSys->CreateType("FileSystemWatcher", this, 0, nullptr, nullptr, myself->GetIdentity(), &err);
	if (err != SourceMod::HandleError_None)
	{
		ke::SafeStrcpy(error, errorSize, "Failed to create FileSystemWatcher handle type.");
		return false;
	}

	return true;
}

void FileSystemWatcherManager::SDK_OnUnload()
{
	if (m_HandleType)
	{
		g_pHandleSys->RemoveType(m_HandleType, myself->GetIdentity());
	}
}

SourceMod::Handle_t FileSystemWatcherManager::CreateWatcher(SourceMod::IPlugin* plugin)
{
	FileSystemWatcher* watcher = new FileSystemWatcher();

	return handlesys->CreateHandle(m_HandleType, watcher, plugin->GetIdentity(), myself->GetIdentity(), nullptr);
}

void FileSystemWatcherManager::OnHandleDestroy(SourceMod::HandleType_t type, void *object)
{
	if (type == m_HandleType)
	{
		delete object;
	}
}