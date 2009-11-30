/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*- */
/* dbus-launch.c  dbus-launch utility
 *
 * Copyright (C) 2007 Ralf Habacker <ralf.habacker@freenet.de>
 *
 * Licensed under the Academic Free License version 2.1
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <mbstring.h>
#include <assert.h>

#if defined __MINGW32__ || (defined _MSC_VER && _MSC_VER <= 1310)
/* save string functions version
*/ 
#define errno_t int

errno_t strcat_s(char *dest, size_t size, char *src) 
{
  assert(strlen(dest) + strlen(src) +1 <= size);
  strcat(dest,src);
  return 0;
}

errno_t strcpy_s(char *dest, size_t size, char *src)
{
  assert(strlen(src) +1 <= size);
  strcpy(dest,src);  
  return 0;
}
#endif

/* TODO: Use Unicode APIs */

/* TODO (tl): This Windows version of dbus-launch is curretly rather
 * pointless as it doesn't take the same command-line options as the
 * UNIX dbus-launch does. A main point of the dbus-launch command is
 * to pass it for instance a --config-file option to make the started
 * dbus-daemon use that config file.
 * 
 * This version also doesn't print out any information, which is a
 * main point of the UNIX one. It should at least support the
 * --sh-syntax option, and maybe also a --cmd-syntax to print out the
 * variable settings in cmd.exe syntax?
 * 
 * NOTE (rh): The main task of dbus-launch is (from the man page) to start 
 * a session bus and this is archieved by the current implemention. 
 * 
 * Additional on windows the session bus starting in not integrated 
 * into the logon process, so there is no need for any --syntax option. 
 * In fact (at least for kde on windows) the session bus is autostarted 
 * with the first application requesting a session bus. 
 *
 */

#define AUTO_ACTIVATE_CONSOLE_WHEN_VERBOSE_MODE 1

int main(int argc,char **argv)
{
  char dbusDaemonPath[MAX_PATH*2+1];
  char command[MAX_PATH*2+1];
  char *p;
  char *daemon_name;
  int result;
  int showConsole = 0;
  char *s = getenv("DBUS_VERBOSE");
  int verbose = s && *s != '\0' ? 1 : 0;
  PROCESS_INFORMATION pi;
  STARTUPINFO si;
  HANDLE h; 
  
#ifdef AUTO_ACTIVATE_CONSOLE_WHEN_VERBOSE_MODE
  if (verbose)
      showConsole = 1; 
#endif
  GetModuleFileName(NULL,dbusDaemonPath,sizeof(dbusDaemonPath));

#ifdef _DEBUG
      daemon_name = "dbus-daemond.exe";
#else
      daemon_name = "dbus-daemon.exe";
#endif
  if ((p = _mbsrchr (dbusDaemonPath, '\\'))) 
    {
      *(p+1)= '\0';
      strcat_s(dbusDaemonPath,sizeof(dbusDaemonPath),daemon_name);
    }
  else 
    {
      if (verbose)
          fprintf(stderr,"error: could not extract path from current applications module filename\n");
      return 1;
    } 
  
  strcpy_s(command,sizeof(command),dbusDaemonPath);
  strcat_s(command,sizeof(command)," --session");
  if (verbose)
      fprintf(stderr,"%s\n",command);
  
  memset(&si, 0, sizeof(si));
  memset(&pi, 0, sizeof(pi));
  si.cb = sizeof(si);

  result = CreateProcess(NULL, 
                  command,
                  0,
                  0,
                  TRUE,
                  (showConsole ? CREATE_NEW_CONSOLE : 0) | NORMAL_PRIORITY_CLASS,
                  0,
                  0,
                  &si,
                  &pi);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  if (result == 0) 
    {
      if (verbose)
          fprintf(stderr, "Could not start dbus-daemon error=%d",GetLastError());
      return 4;
    }
   
  return 0;
}
