<div align="center">
<h1>FileWatcher</h1>

[![contributions-welcome](https://user-images.githubusercontent.com/6830124/160218475-2bf2fef4-38cc-42d2-8f04-b509973ad819.svg)](#%EF%B8%8Fcontributions)

</div>

A SourceMod extension that allows plugins to watch files/directory trees for changes. Unlike iterating through entire subtrees of files to check for changes, **this listens to change events from the file system.**

This is inspired from [.NET's FileSystemWatcher](https://docs.microsoft.com/en-us/dotnet/api/system.io.filesystemwatcher?view=net-6.0) class which uses the same underlying API.

<h1>Table of Contents</h1>

- [Requirements](#requirements)
- [API Reference](#api-reference)
- [Usage](#usage)
	- [Watching a directory](#watching-a-directory)
	- [Watching a single file](#watching-a-single-file)
	- [Stop watching a directory](#stop-watching-a-directory)
- [License](#license)

# Requirements

- SourceMod 1.11 or later

# API Reference

See the [include file](./pawn/scripting/include/filewatcher.inc) for more information. The include file also contains some information on some behavior differences between Windows and Linux.

# Usage

Assume the following code is preceded by this snippet:

```sourcepawn
#include <filewatcher>

FileSystemWatcher g_fsw;
```

## Watching a directory

This example shows how to watch the game's `cfg` directory for changes.

```sourcepawn
public void OnPluginStart()
{
	g_fsw = new FileSystemWatcher("cfg");
	g_fsw.IncludeSubdirectories = true;
	g_fsw.NotifyFilter = FSW_NOTIFY_CREATED |
		FSW_NOTIFY_DELETED |
		FSW_NOTIFY_MODIFIED |
		FSW_NOTIFY_RENAMED;
	g_fsw.OnStarted = OnStarted;
	g_fsw.OnStopped = OnStopped;
}

public void OnMapStart()
{
	g_fsw.IsWatching = true;
}

static void OnStarted(FileSystemWatcher fsw)
{
	char path[PLATFORM_MAX_PATH];
	fsw.GetPath(path, sizeof(path));

	PrintToServer("Now watching the %s folder for changes.", path);
}

static void OnStopped(FileSystemWatcher fsw)
{
	char path[PLATFORM_MAX_PATH];
	fsw.GetPath(path, sizeof(path));

	PrintToServer("Stopped watching the %s folder for changes.", path);
}
```

## Watching a single file

This example shows how to watch the server's `server.cfg` file for changes and automatically execute it.

```sourcepawn
public void OnPluginStart()
{
	g_fsw = new FileSystemWatcher("cfg");
	g_fsw.NotifyFilter = FSW_NOTIFY_CREATED |
		FSW_NOTIFY_MODIFIED |
		FSW_NOTIFY_RENAMED;
	g_fsw.OnCreated = OnCreated;
	g_fsw.OnModified = OnModified;
	g_fsw.OnRenamed = OnRenamed;
}

public void OnConfigsExecuted()
{
	g_fsw.IsWatching = true;
}

static void OnCreated(FileSystemWatcher fsw, const char[] path)
{
	if (strcmp(path, "server.cfg", false) == 0)
	{
		ServerCommand("exec server.cfg");
	}
}

static void OnModified(FileSystemWatcher fsw, const char[] path)
{
	if (strcmp(path, "server.cfg", false) == 0)
	{
		ServerCommand("exec server.cfg");
	}
}

static void OnRenamed(FileSystemWatcher fsw, const char[] oldPath, const char[] newPath)
{
	if (strcmp(newPath, "server.cfg", false) == 0)
	{
		ServerCommand("exec server.cfg");
	}
}

```

## Stop watching a directory

You may either set `IsWatching` to false or delete the `FileSystemWatcher` itself. Unloading the plugin will automatically perform the latter.

```sourcepawn
// no more watching
g_fsw.IsWatching = false;

// good ol' fashioned delete
delete g_fsw;
```

# License

[GNU General Public License 3.0](https://choosealicense.com/licenses/gpl-3.0/)