<div align="center">
<h1>FileWatcher</h1>

![](https://github.com/KitRifty/SM-FileWatcher/actions/workflows/ci.yml/badge.svg?branch=main)
![PR's Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat)
[![](https://img.shields.io/github/license/KitRifty/SM-FileWatcher)](https://opensource.org/licenses/)

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
- [Windows vs. Linux](#windows-vs-linux)
	- [Renaming, moving, or deleting a watched directory](#renaming-moving-or-deleting-a-watched-directory)
	- [Moving files between subdirectories](#moving-files-between-subdirectories)
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

# Windows vs. Linux

To detect changes, [`ReadDirectoryChangesW`](https://docs.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw) is used for Windows, [`inotify`](https://linux.die.net/man/7/inotify) for Linux. Both send out change events similarly for the most part aside for a few differences.

## Renaming, moving, or deleting a watched directory

For Windows, this is not allowed.

For Linux, it is allowed, but the directory will no longer be watched.

In any case, you are discouraged from changing the watched directory itself in any way.

## Moving files between subdirectories

For Windows, moving directories/files between subdirectories under the same watched file tree (`IncludeSubdirectories = true`) is seen as two actions in this order:
- Delete from old directory
- Create in new directory

For Linux, this is seen as a single Rename action.

This is intentional behavior, but if you wish to handle these events consistently across platforms, consider handling Rename events as two separate Delete and Create events as both the old and new paths are given in the callback.

> **Note**
> This behavior difference does not apply when moving files ***in and out*** of a watched tree. As far as the watcher is concerned, they will be seen as Create and Delete actions respectively on both platforms.

# License

[GNU General Public License 3.0](https://choosealicense.com/licenses/gpl-3.0/)