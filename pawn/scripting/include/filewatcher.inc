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

typedef FileSystemWatcherOnChanged = function void(FileSystemWatcher fsw, const char[] path);
typedef FileSystemWatcherOnRenamed = function void(FileSystemWatcher fsw, const char[] oldPath, const char[] newPath);

methodmap FileSystemWatcher < Handle
{
	property bool IsWatching
	{
		public native get();
	}

	property bool IncludeSubdirectories
	{
		public native get();
		public native set(bool value);
	}

	property FileSystemWatcherNotifyFilterFlags NotifyFilter
	{
		public native get();
		public native set(FileSystemWatcherNotifyFilterFlags value);
	}

	property FileSystemWatcherOnChanged OnCreated
	{
		public native set(FileSystemWatcherOnChanged value);
	}

	property FileSystemWatcherOnChanged OnDeleted
	{
		public native set(FileSystemWatcherOnChanged value);
	}

	property FileSystemWatcherOnChanged OnModified
	{
		public native set(FileSystemWatcherOnChanged value);
	}

	property FileSystemWatcherOnRenamed OnRenamed
	{
		public native set(FileSystemWatcherOnRenamed value);
	}

	public native FileSystemWatcher(const char[] path = "", const char[] filter = "");

	public native bool StartWatching();
	public native bool StopWatching();
}

/**
 * Do not edit below this line!
 */
public Extension __ext_sdkhooks =
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