#include <sourcemod>
#include <filewatcher>

public void OnPluginStart()
{
	char path[PLATFORM_MAX_PATH];
	BuildPath(Path_SM, path, sizeof(path), "cfg");

	FileSystemWatcher watcher = new FileSystemWatcher(path);
	watcher.IncludeSubdirectories = true;
	if (watcher.StartWatching())
	{
		PrintToServer("Now watching directory sourcemod/cfg for changes.");
	}
}