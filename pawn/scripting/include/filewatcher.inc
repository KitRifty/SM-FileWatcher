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

#if defined _filesystemwatcher_included
 #endinput
#endif
#define _filesystemwatcher_included

enum FileSystemWatcherNotifyFilterFlags
{
	FSW_NOTIFY_NONE = 0,
	FSW_NOTIFY_CREATED = (1 << 0),
	FSW_NOTIFY_DELETED = (1 << 1),
	FSW_NOTIFY_MODIFIED = (1 << 2),
	FSW_NOTIFY_RENAMED = (1 << 3)
};

typedef FileSystemWatcherOnStarted = function void(FileSystemWatcher fsw);
typedef FileSystemWatcherOnStopped = function void(FileSystemWatcher fsw);
typedef FileSystemWatcherOnChanged = function void(FileSystemWatcher fsw, const char[] path);
typedef FileSystemWatcherOnRenamed = function void(FileSystemWatcher fsw, const char[] oldPath, const char[] newPath);

methodmap FileSystemWatcher < Handle
{
	/**
	 * Indicates whether the watcher is enabled.
	 *
	 * Upon setting this to true, the watcher will begin monitoring its directory.
	 * However, events will not be sent to plugins until the first `OnGameFrame()` is
	 * called, so consider only watching when events can be sent such as on/after
	 * `OnMapStart()` or `OnConfigsExecuted()`. The `OnStarted` and `OnStopped` callbacks
	 * can be used to determine if the watcher is actually receiving events or not.
	 *
	 * It is not always guaranteed that the watcher will be receiving file change events
	 * especially if the watched directory is deleted, renamed, or does not exist.
	 * In this scenario, the watcher will retry watching the directory every `RetryInterval`
	 * milliseconds until it succeeds.
	 *
	 * Once you start watching a directory, you are discouraged from moving, renaming,
	 * or deleting the directory in any way. In Windows this is not allowed, and in
	 * Linux doing so will cause the watcher to stop receiving events and continually
	 * retry until it is successful.
	 */
	property bool IsWatching
	{
		public native get();
		public native set(bool value);
	}

	/**
	 * Indicates whether subdirectories within the watched directory should
	 * be monitored. By default this is false.
	 */
	property bool IncludeSubdirectories
	{
		public native get();
		public native set(bool value);
	}

	/**
	 * If IncludeSubdirectories is true, then this sets whether directory
	 * symbolic links within the watched directory should also be
	 * watched. By default this is true.
	 */
	property bool WatchDirectoryLinks
	{
		public native get();
		public native set(bool value);
	}

	/**
	 * The type of changes to watch for.
	 */
	property FileSystemWatcherNotifyFilterFlags NotifyFilter
	{
		public native get();
		public native set(FileSystemWatcherNotifyFilterFlags value);
	}

	/**
	 * Deprecated. Does nothing.
	 */
	property int RetryInterval
	{
		public native get();
		public native set(int value);
	}

	/**
	 * The size (in bytes) of the internal buffer. The size should be an interval
	 * of 4KB.
	 *
	 * Sometimes events can be missed which can happen with directories that are very active.
	 * You may consider increasing the buffer size if the default size (8KB) isn't enough,
	 * but do not exceed 64KB.
	 */
	property int InternalBufferSize
	{
		public native get();
		public native set(int value);
	}

	/**
	 * The callback for when the watcher begins receiving file system change events.
	 */
	property FileSystemWatcherOnStarted OnStarted
	{
		public native set(FileSystemWatcherOnStarted value);
	}

	/**
	 * The callback for when the watcher stops receiving file system change events.
	 *
	 * This can be triggered if the watcher is explicitly ordered to stop watching,
	 * or the watched directory is renamed/moved/deleted.
	 */
	property FileSystemWatcherOnStopped OnStopped
	{
		public native set(FileSystemWatcherOnStopped value);
	}

	/**
	 * The callback for when a file is created inside the watched directory or
	 * a file is moved into the watched directory from a directory that wasn't
	 * being watched.
	 */
	property FileSystemWatcherOnChanged OnCreated
	{
		public native set(FileSystemWatcherOnChanged value);
	}

	/**
	 * The callback for when a file is deleted from the watched directory or
	 * a file is moved out of the watched directory.
	 */
	property FileSystemWatcherOnChanged OnDeleted
	{
		public native set(FileSystemWatcherOnChanged value);
	}

	/**
	 * The callback for when a file is changed or modified.
	 */
	property FileSystemWatcherOnChanged OnModified
	{
		public native set(FileSystemWatcherOnChanged value);
	}

	/**
	 * The callback for when a file is renamed.
	 *
	 * There are slight differences on when this is called depending on the
	 * operating system, however in both Windows and Linux, changing the
	 * name of the file will result in a Rename action.
	 *
	 * For Windows: Moving a file from one directory to another is seen as two
	 * actions: Delete (from old directory) and Create (in new directory).
	 *
	 * For Linux: Moving a file from one *watched* directory to another watched
	 * directory is seen as a Rename action.
	 */
	property FileSystemWatcherOnRenamed OnRenamed
	{
		public native set(FileSystemWatcherOnRenamed value);
	}

	/**
	 * Creates a file watcher object. This listens to the file system for change
	 * notifications and raises events when a directory, or file in a directory,
	 * changes.
	 *
	 * @param path    Path to directory. This path is relative to the game directory.
	 * @error         Path points to a directory outside of the game directory.
	 */
	public native FileSystemWatcher(const char[] path = "");

	/**
	 * Retrieves the path of the watched directory, relative to the game directory.
	 *
	 * @param buffer        Buffer to store the path.
	 * @param bufferSize    Size of buffer.
	 * @return              Number of bytes written.
	 */
	public native int GetPath(char[] buffer, int bufferSize);
}

/**
 * Do not edit below this line!
 */
public Extension __ext_filewatcher =
{
	name = "FileWatcher",
	file = "filewatcher.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public void __ext_filewatcher_SetNTVOptional()
{
	MarkNativeAsOptional("FileSystemWatcher.IsWatching.get");
	MarkNativeAsOptional("FileSystemWatcher.IsWatching.set");
	MarkNativeAsOptional("FileSystemWatcher.IncludeSubdirectories.get");
	MarkNativeAsOptional("FileSystemWatcher.IncludeSubdirectories.set");
	MarkNativeAsOptional("FileSystemWatcher.WatchDirectoryLinks.get");
	MarkNativeAsOptional("FileSystemWatcher.WatchDirectoryLinks.set");
	MarkNativeAsOptional("FileSystemWatcher.NotifyFilter.get");
	MarkNativeAsOptional("FileSystemWatcher.NotifyFilter.set");
	MarkNativeAsOptional("FileSystemWatcher.RetryInterval.get");
	MarkNativeAsOptional("FileSystemWatcher.RetryInterval.set");
	MarkNativeAsOptional("FileSystemWatcher.InternalBufferSize.get");
	MarkNativeAsOptional("FileSystemWatcher.InternalBufferSize.set");
	MarkNativeAsOptional("FileSystemWatcher.OnStarted.set");
	MarkNativeAsOptional("FileSystemWatcher.OnStopped.set");
	MarkNativeAsOptional("FileSystemWatcher.OnCreated.set");
	MarkNativeAsOptional("FileSystemWatcher.OnDeleted.set");
	MarkNativeAsOptional("FileSystemWatcher.OnModified.set");
	MarkNativeAsOptional("FileSystemWatcher.OnRenamed.set");
	MarkNativeAsOptional("FileSystemWatcher.FileSystemWatcher");
	MarkNativeAsOptional("FileSystemWatcher.GetPath");
}
#endif
