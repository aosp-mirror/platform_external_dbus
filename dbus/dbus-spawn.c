/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-spawn.c Wrapper around fork/exec
 * 
 * Copyright (C) 2002, 2003  Red Hat, Inc.
 * Copyright (C) 2003 CodeFactory AB
 *
 * Licensed under the Academic Free License version 1.2
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include "dbus-spawn.h"
#include "dbus-sysdeps.h"
#include "dbus-internals.h"

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

/**
 * @addtogroup DBusInternalsUtils
 * @{
 */

/* Avoids a danger in threaded situations (calling close()
 * on a file descriptor twice, and another thread has
 * re-opened it since the first close)
 */
static int
close_and_invalidate (int *fd)
{
  int ret;

  if (*fd < 0)
    return -1;
  else
    {
      ret = close (*fd);
      *fd = -1;
    }

  return ret;
}

static dbus_bool_t
make_pipe (int        p[2],
           DBusError *error)
{
  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  if (pipe (p) < 0)
    {
      dbus_set_error (error,
		      DBUS_ERROR_SPAWN_FAILED,
		      "Failed to create pipe for communicating with child process (%s)",
		      _dbus_errno_to_string (errno));
      return FALSE;
    }
  else
    {
      _dbus_fd_set_close_on_exec (p[0]);
      _dbus_fd_set_close_on_exec (p[1]);      
      return TRUE;
    }
}

enum
{
  CHILD_CHDIR_FAILED,
  CHILD_EXEC_FAILED,
  CHILD_DUP2_FAILED,
  CHILD_FORK_FAILED
};

static void
write_err_and_exit (int fd, int msg)
{
  int en = errno;
  
  write (fd, &msg, sizeof(msg));
  write (fd, &en, sizeof(en));
  
  _exit (1);
}

static dbus_bool_t
read_ints (int        fd,
	   int       *buf,
	   int        n_ints_in_buf,
	   int       *n_ints_read,
	   DBusError *error)
{
  size_t bytes = 0;    

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  while (TRUE)
    {
      size_t chunk;    

      if (bytes >= sizeof(int)*2)
        break; /* give up, who knows what happened, should not be
                * possible.
                */
          
    again:
      chunk = read (fd,
                    ((char*)buf) + bytes,
                    sizeof(int) * n_ints_in_buf - bytes);
      if (chunk < 0 && errno == EINTR)
        goto again;
          
      if (chunk < 0)
        {
          /* Some weird shit happened, bail out */
              
          dbus_set_error (error,
			  DBUS_ERROR_SPAWN_FAILED,
			  "Failed to read from child pipe (%s)",
			  _dbus_errno_to_string (errno));

          return FALSE;
        }
      else if (chunk == 0)
        break; /* EOF */
      else /* chunk > 0 */
	bytes += chunk;
    }

  *n_ints_read = (int)(bytes / sizeof(int));

  return TRUE;
}

static void
do_exec (int                       child_err_report_fd,
	 char                    **argv,
	 DBusSpawnChildSetupFunc   child_setup,
	 void                     *user_data)
{
#ifdef DBUS_BUILD_TESTS
  int i, max_open;
#endif

  if (child_setup)
    (* child_setup) (user_data);

#ifdef DBUS_BUILD_TESTS
  max_open = sysconf (_SC_OPEN_MAX);
  
  for (i = 3; i < max_open; i++)
    {
      int retval;

      retval = fcntl (i, F_GETFD);

      if (retval != -1 && !(retval & FD_CLOEXEC))
	_dbus_warn ("Fd %d did not have the close-on-exec flag set!\n", i);
    }
#endif
  
  execv (argv[0], argv);

  /* Exec failed */
  write_err_and_exit (child_err_report_fd,
                      CHILD_EXEC_FAILED);
  
}

/**
 * Spawns a new process. The executable name and argv[0]
 * are the same, both are provided in argv[0]. The child_setup
 * function is passed the given user_data and is run in the child
 * just before calling exec().
 *
 * @todo this code should be reviewed/double-checked as it's fairly
 * complex and no one has reviewed it yet.
 *
 * @param argv the executable and arguments
 * @param child_setup function to call in child pre-exec()
 * @param user_data user data for setup function
 * @param error error object to be filled in if function fails
 * @returns #TRUE on success, #FALSE if error is filled in
 */
dbus_bool_t
_dbus_spawn_async (char                    **argv,
		   DBusSpawnChildSetupFunc   child_setup,
		   void                     *user_data,
		   DBusError                *error)
{
  int pid = -1, grandchild_pid;
  int child_err_report_pipe[2] = { -1, -1 };
  int status;

  _DBUS_ASSERT_ERROR_IS_CLEAR (error);
  
  if (!make_pipe (child_err_report_pipe, error))
    return FALSE;

  pid = fork ();
  
  if (pid < 0)
    {
      dbus_set_error (error,
		      DBUS_ERROR_SPAWN_FORK_FAILED,
		      "Failed to fork (%s)",
		      _dbus_errno_to_string (errno));
      return FALSE;
    }
  else if (pid == 0)
    {
      /* Immediate child. */
      
      /* Be sure we crash if the parent exits
       * and we write to the err_report_pipe
       */
      signal (SIGPIPE, SIG_DFL);

      /* Close the parent's end of the pipes;
       * not needed in the close_descriptors case,
       * though
       */
      close_and_invalidate (&child_err_report_pipe[0]);

      /* We need to fork an intermediate child that launches the
       * final child. The purpose of the intermediate child
       * is to exit, so we can waitpid() it immediately.
       * Then the grandchild will not become a zombie.
       */
      grandchild_pid = fork ();
      
      if (grandchild_pid < 0)
	{
	  write_err_and_exit (child_err_report_pipe[1],
			      CHILD_FORK_FAILED);              
	}
      else if (grandchild_pid == 0)
	{
	  do_exec (child_err_report_pipe[1],
		   argv,
		   child_setup, user_data);
	}
      else
	{
	  _exit (0);
	}
    }
  else
    {
      /* Parent */

      int buf[2];
      int n_ints = 0;    
      
      /* Close the uncared-about ends of the pipes */
      close_and_invalidate (&child_err_report_pipe[1]);

    wait_again:
      if (waitpid (pid, &status, 0) < 0)
	{
	  if (errno == EINTR)
	    goto wait_again;
	  else if (errno == ECHILD)
	    ; /* do nothing, child already reaped */
	  else
	    _dbus_warn ("waitpid() should not fail in "
			"'_dbus_spawn_async'");
	}

      if (!read_ints (child_err_report_pipe[0],
                      buf, 2, &n_ints,
                      error))
	  goto cleanup_and_fail;
      
      if (n_ints >= 2)
        {
          /* Error from the child. */
          switch (buf[0])
            {
	    default:
              dbus_set_error (error,
			      DBUS_ERROR_SPAWN_FAILED,
			      "Unknown error executing child process \"%s\"",
			      argv[0]);
              break;
	    }

	  goto cleanup_and_fail;
	}


      /* Success against all odds! return the information */
      close_and_invalidate (&child_err_report_pipe[0]);

      return TRUE;
    }

 cleanup_and_fail:

  /* There was an error from the Child, reap the child to avoid it being
     a zombie.
  */
  if (pid > 0)
    {
    wait_failed:
      if (waitpid (pid, NULL, 0) < 0)
	{
          if (errno == EINTR)
            goto wait_failed;
          else if (errno == ECHILD)
            ; /* do nothing, child already reaped */
          else
            _dbus_warn ("waitpid() should not fail in "
			"'_dbus_spawn_async'");
	}
    }
  
  close_and_invalidate (&child_err_report_pipe[0]);
  close_and_invalidate (&child_err_report_pipe[1]);

  return FALSE;
}


/** @} */
