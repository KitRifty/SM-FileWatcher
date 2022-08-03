#include <sourcemod>
#include <filewatcher>

public void OnPluginStart()
{
	char path[PLATFORM_MAX_PATH];
	BuildPath(Path_SM, path, sizeof(path), "cfg");

	FileSystemWatcher watcher = new FileSystemWatcher(path);
	watcher.IncludeSubdirectories = true;
	watcher.NotifyFilter = FSW_NOTIFY_FILE_NAME | FSW_NOTIFY_DIR_NAME | FSW_NOTIFY_LASTWRITE;
	watcher.OnChanged = OnChanged;
	if (watcher.StartWatching())
	{
		PrintToServer("Now watching directory sourcemod/cfg for changes.");
	}
	else
	{
		PrintToServer("FAILED to watch directory sourcemod/cfg for changes.")
	}
}

void OnChanged(const char[] fileName)
{
	PrintToServer("%s was changed!", fileName);
}