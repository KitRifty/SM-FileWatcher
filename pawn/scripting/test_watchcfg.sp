#include <sourcemod>
#include <filewatcher>

public void OnPluginStart()
{
	FileSystemWatcher fsw = new FileSystemWatcher("cfg");
	fsw.IncludeSubdirectories = true;
	fsw.NotifyFilter = FSW_NOTIFY_CREATED | FSW_NOTIFY_DELETED | FSW_NOTIFY_MODIFIED | FSW_NOTIFY_RENAMED;
	fsw.OnCreated = OnCreated;
	fsw.OnDeleted = OnDeleted;
	fsw.OnModified = OnModified;
	fsw.OnRenamed = OnRenamed;

	if (fsw.StartWatching())
	{
		PrintToServer("Now watching your cfg/ folder for changes.");
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