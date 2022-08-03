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
	FSW_NOTIFY_FILE_NAME = (1 << 0),        // The name of the file.
	FSW_NOTIFY_DIR_NAME = (1 << 1),         // The name of the directory.
	FSW_NOTIFY_ATTRIBUTES = (1 << 2),       // The attributes of the file or folder.
	FSW_NOTIFY_SIZE = (1 << 3),             // The size of the file or folder.
	FSW_NOTIFY_LASTWRITE = (1 << 4),        // The date the file or folder last had anything written on it.
	FSW_NOTIFY_LASTACCESS = (1 << 5),       // The date the file or folder was last opened.
	FSW_NOTIFY_CREATIONTIME = (1 << 6),     // The time the file or folder was created.
	FSW_NOTIFY_SECURITY = (1 << 8)          // The security settings of the file or folder.
}

typedef FileSystemWatcherOnChanged = function void(const char[] path);

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

	property FileSystemWatcherOnChanged OnChanged
	{
		public native set(FileSystemWatcherOnChanged value);
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