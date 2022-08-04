#include <sourcemod>
#include <filewatcher>

public void OnPluginStart()
{
	char path[PLATFORM_MAX_PATH];
	BuildPath(Path_SM, path, sizeof(path), "cfg");

	FileSystemWatcher fsw = new FileSystemWatcher(path);
	fsw.IncludeSubdirectories = true;
	fsw.NotifyFilter = FSW_NOTIFY_CREATED | FSW_NOTIFY_DELETED | FSW_NOTIFY_MODIFIED | FSW_NOTIFY_RENAMED;
	fsw.OnCreated = OnCreated;
	fsw.OnDeleted = OnDeleted;
	fsw.OnModified = OnModified;
	fsw.OnRenamed = OnRenamed;

	if (fsw.StartWatching())
	{
		PrintToServer("Now watching directory sourcemod/cfg for changes.");
	}
	else
	{
		PrintToServer("FAILED to watch directory sourcemod/cfg for changes.")
	}
}

void OnCreated(FileSystemWatcher fsw, const char[] path)
{
	PrintToServer("+ %s", path);
}

void OnDeleted(FileSystemWatcher fsw, const char[] path)
{
	PrintToServer("- %s", path);
}

void OnModified(FileSystemWatcher fsw, const char[] path)
{
	PrintToServer("%s was modified", path);
}

void OnRenamed(FileSystemWatcher fsw, const char[] oldPath, const char[] newPath)
{
	PrintToServer("%s -> %s", oldPath, newPath);
}