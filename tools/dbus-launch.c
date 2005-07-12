/* -*- mode: C; c-file-style: "gnu" -*- */
/* dbus-launch.c  dbus-launch utility
 *
 * Copyright (C) 2003 Red Hat, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <config.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/select.h>
#ifdef DBUS_BUILD_X11
#include <X11/Xlib.h>
#endif

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#undef	MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

static void
verbose (const char *format,
         ...)
{
  va_list args;
  static int verbose = TRUE;
  static int verbose_initted = FALSE;
  
  /* things are written a bit oddly here so that
   * in the non-verbose case we just have the one
   * conditional and return immediately.
   */
  if (!verbose)
    return;
  
  if (!verbose_initted)
    {
      verbose = getenv ("DBUS_VERBOSE") != NULL;
      verbose_initted = TRUE;
      if (!verbose)
        return;
    }

  fprintf (stderr, "%lu: ", (unsigned long) getpid ());
  
  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
}

static void
usage (int ecode)
{
  fprintf (stderr, "dbus-launch [--version] [--help] [--sh-syntax] [--csh-syntax] [--auto-syntax] [--exit-with-session]\n");
  exit (ecode);
}

static void
version (void)
{
  printf ("D-BUS Message Bus Launcher %s\n"
          "Copyright (C) 2003 Red Hat, Inc.\n"
          "This is free software; see the source for copying conditions.\n"
          "There is NO warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n",
          VERSION);
  exit (0);
}

static char *
xstrdup (const char *str)
{
  int len;
  char *copy;
  
  if (str == NULL)
    return NULL;
  
  len = strlen (str);

  copy = malloc (len + 1);
  if (copy == NULL)
    return NULL;

  memcpy (copy, str, len + 1);
  
  return copy;
}

typedef enum
{
  READ_STATUS_OK,    /**< Read succeeded */
  READ_STATUS_ERROR, /**< Some kind of error */
  READ_STATUS_EOF    /**< EOF returned */
} ReadStatus;

static ReadStatus
read_line (int        fd,
           char      *buf,
           size_t     maxlen)
{
  size_t bytes = 0;
  ReadStatus retval;

  memset (buf, '\0', maxlen);
  maxlen -= 1; /* ensure nul term */
  
  retval = READ_STATUS_OK;
  
  while (TRUE)
    {
      size_t chunk;    
      size_t to_read;
      
    again:
      to_read = maxlen - bytes;

      if (to_read == 0)
        break;
      
      chunk = read (fd,
                    buf + bytes,
                    to_read);
      if (chunk < 0 && errno == EINTR)
        goto again;
          
      if (chunk < 0)
        {
          retval = READ_STATUS_ERROR;
          break;
        }
      else if (chunk == 0)
        {
          retval = READ_STATUS_EOF;
          break; /* EOF */
        }
      else /* chunk > 0 */
	bytes += chunk;
    }

  if (retval == READ_STATUS_EOF &&
      bytes > 0)
    retval = READ_STATUS_OK;
  
  /* whack newline */
  if (retval != READ_STATUS_ERROR &&
      bytes > 0 &&
      buf[bytes-1] == '\n')
    buf[bytes-1] = '\0';
  
  return retval;
}

static ReadStatus
read_pid (int        fd,
          pid_t     *buf)
{
  size_t bytes = 0;
  ReadStatus retval;

  retval = READ_STATUS_OK;
  
  while (TRUE)
    {
      size_t chunk;    
      size_t to_read;
      
    again:
      to_read = sizeof (pid_t) - bytes;

      if (to_read == 0)
        break;
      
      chunk = read (fd,
                    ((char*)buf) + bytes,
                    to_read);
      if (chunk < 0 && errno == EINTR)
        goto again;
          
      if (chunk < 0)
        {
          retval = READ_STATUS_ERROR;
          break;
        }
      else if (chunk == 0)
        {
          retval = READ_STATUS_EOF;
          break; /* EOF */
        }
      else /* chunk > 0 */
	bytes += chunk;
    }

  return retval;
}

static void
do_write (int fd, const void *buf, size_t count)
{
  size_t bytes_written;
  int ret;
  
  bytes_written = 0;
  
 again:
  
  ret = write (fd, ((const char*)buf) + bytes_written, count - bytes_written);

  if (ret < 0)
    {
      if (errno == EINTR)
        goto again;
      else
        {
          fprintf (stderr, "Failed to write data to pipe! %s\n",
                   strerror (errno));
          exit (1); /* give up, we suck */
        }
    }
  else
    bytes_written += ret;
  
  if (bytes_written < count)
    goto again;
}

static void
write_pid (int   fd,
           pid_t pid)
{
  do_write (fd, &pid, sizeof (pid));
}

static int
do_waitpid (pid_t pid)
{
  int ret;
  
 again:
  ret = waitpid (pid, NULL, 0);

  if (ret < 0 &&
      errno == EINTR)
    goto again;

  return ret;
}

static pid_t bus_pid_to_kill = -1;

static void
kill_bus_and_exit (void)
{
  verbose ("Killing message bus and exiting babysitter\n");
  
  /* in case these point to any NFS mounts, get rid of them immediately */
  close (0);
  close (1);
  close (2);

  kill (bus_pid_to_kill, SIGTERM);
  sleep (3);
  kill (bus_pid_to_kill, SIGKILL);

  exit (0);
}

#ifdef DBUS_BUILD_X11
static int
x_io_error_handler (Display *xdisplay)
{
  verbose ("X IO error\n");
  kill_bus_and_exit ();
  return 0;
}
#endif

static int got_sighup = FALSE;

static void
signal_handler (int sig)
{
  switch (sig)
    {
    case SIGHUP:
      got_sighup = TRUE;
      break;
    }
}

static void
kill_bus_when_session_ends (void)
{
  int tty_fd;
  int x_fd;
  fd_set read_set;
  fd_set err_set;
  int ret;
  struct sigaction act;
  sigset_t empty_mask;
#ifdef DBUS_BUILD_X11
  Display *xdisplay;
#endif
  
  /* install SIGHUP handler */
  got_sighup = FALSE;
  sigemptyset (&empty_mask);
  act.sa_handler = signal_handler;
  act.sa_mask    = empty_mask;
  act.sa_flags   = 0;
  sigaction (SIGHUP,  &act, 0);
  
#ifdef DBUS_BUILD_X11
  xdisplay = XOpenDisplay (NULL);
  if (xdisplay != NULL)
    {
      verbose ("Successfully opened X display\n");
      x_fd = ConnectionNumber (xdisplay);
      XSetIOErrorHandler (x_io_error_handler);
    }
  else
    x_fd = -1;
#else
  verbose ("Compiled without X11 support\n");
  x_fd = -1;
#endif

  if (isatty (0))
    tty_fd = 0;
  else
    tty_fd = -1;

  if (tty_fd >= 0)
    verbose ("stdin isatty(), monitoring it\n");
  else
    verbose ("stdin was not a TTY, not monitoring it\n");  
  
  if (tty_fd < 0 && x_fd < 0)
    {
      fprintf (stderr, "No terminal on standard input and no X display; cannot attach message bus to session lifetime\n");
      exit (1);
    }
  
  while (TRUE)
    {
      FD_ZERO (&read_set);
      FD_ZERO (&err_set);

      if (tty_fd >= 0)
        {
          FD_SET (tty_fd, &read_set);
          FD_SET (tty_fd, &err_set);
        }

      if (x_fd >= 0)
        {
          FD_SET (x_fd, &read_set);
          FD_SET (x_fd, &err_set);
        }
      
      ret = select (MAX (tty_fd, x_fd) + 1,
                    &read_set, NULL, &err_set, NULL);

      if (got_sighup)
        {
          verbose ("Got SIGHUP, exiting\n");
          kill_bus_and_exit ();
        }
      
#ifdef DBUS_BUILD_X11
      /* Dump events on the floor, and let
       * IO error handler run if we lose
       * the X connection
       */
      if (x_fd >= 0)
        verbose ("X fd condition reading = %d error = %d\n",
                 FD_ISSET (x_fd, &read_set),
                 FD_ISSET (x_fd, &err_set));
      
      if (xdisplay != NULL)
        {      
          while (XPending (xdisplay))
            {
              XEvent ignored;
              XNextEvent (xdisplay, &ignored);
            }
        }
#endif

      if (tty_fd >= 0)
        {
          if (FD_ISSET (tty_fd, &read_set))
            {
              int bytes_read;
              char discard[512];

              verbose ("TTY ready for reading\n");
              
              bytes_read = read (tty_fd, discard, sizeof (discard));

              verbose ("Read %d bytes from TTY errno = %d\n",
                       bytes_read, errno);
              
              if (bytes_read == 0)
                kill_bus_and_exit (); /* EOF */
              else if (bytes_read < 0 && errno != EINTR)
                {
                  /* This shouldn't happen I don't think; to avoid
                   * spinning on the fd forever we exit.
                   */
                  fprintf (stderr, "dbus-launch: error reading from stdin: %s\n",
                           strerror (errno));
                  kill_bus_and_exit ();
                }
            }
          else if (FD_ISSET (tty_fd, &err_set))
            {
              verbose ("TTY has error condition\n");
              
              kill_bus_and_exit ();
            }
        }
    }
}

static void
babysit (int   exit_with_session,
         pid_t child_pid,
         int   read_bus_pid_fd,  /* read pid from here */
         int   write_bus_pid_fd) /* forward pid to here */
{
  int ret;
#define MAX_PID_LEN 64
  char buf[MAX_PID_LEN];
  long val;
  char *end;
  int dev_null_fd;
  const char *s;

  verbose ("babysitting, exit_with_session = %d, child_pid = %ld, read_bus_pid_fd = %d, write_bus_pid_fd = %d\n",
           exit_with_session, (long) child_pid, read_bus_pid_fd, write_bus_pid_fd);
  
  /* We chdir ("/") since we are persistent and daemon-like, and fork
   * again so dbus-launch can reap the parent.  However, we don't
   * setsid() or close fd 0 because the idea is to remain attached
   * to the tty and the X server in order to kill the message bus
   * when the session ends.
   */

  if (chdir ("/") < 0)
    {
      fprintf (stderr, "Could not change to root directory: %s\n",
               strerror (errno));
      exit (1);
    }

  /* Close stdout/stderr so we don't block an "eval" or otherwise
   * lock up. stdout is still chaining through to dbus-launch
   * and in turn to the parent shell.
   */
  dev_null_fd = open ("/dev/null", O_RDWR);
  if (dev_null_fd >= 0)
    {
      if (!exit_with_session)
        dup2 (dev_null_fd, 0);
      dup2 (dev_null_fd, 1);
      s = getenv ("DBUS_DEBUG_OUTPUT");
      if (s == NULL || *s == '\0')
        dup2 (dev_null_fd, 2);
    }
  else
    {
      fprintf (stderr, "Failed to open /dev/null: %s\n",
               strerror (errno));
      /* continue, why not */
    }
  
  ret = fork ();

  if (ret < 0)
    {
      fprintf (stderr, "fork() failed in babysitter: %s\n",
               strerror (errno));
      exit (1);
    }

  if (ret > 0)
    {
      /* Parent reaps pre-fork part of bus daemon, then exits and is
       * reaped so the babysitter isn't a zombie
       */

      verbose ("=== Babysitter's intermediate parent continues again\n");
      
      if (do_waitpid (child_pid) < 0)
        {
          /* shouldn't happen */
          fprintf (stderr, "Failed waitpid() waiting for bus daemon's parent\n");
          exit (1);
        }

      verbose ("Babysitter's intermediate parent exiting\n");
      
      exit (0);
    }

  /* Child continues */
  verbose ("=== Babysitter process created\n");

  verbose ("Reading PID from daemon\n");
  /* Now read data */
  switch (read_line (read_bus_pid_fd, buf, MAX_PID_LEN))
    {
    case READ_STATUS_OK:
      break;
    case READ_STATUS_EOF:
      fprintf (stderr, "EOF reading PID from bus daemon\n");
      exit (1);
      break;
    case READ_STATUS_ERROR:
      fprintf (stderr, "Error reading PID from bus daemon: %s\n",
               strerror (errno));
      exit (1);
      break;
    }

  end = NULL;
  val = strtol (buf, &end, 0);
  if (buf == end || end == NULL)
    {
      fprintf (stderr, "Failed to parse bus PID \"%s\": %s\n",
               buf, strerror (errno));
      exit (1);
    }

  bus_pid_to_kill = val;

  verbose ("Got PID %ld from daemon\n",
           (long) bus_pid_to_kill);
  
  /* Write data to launcher */
  write_pid (write_bus_pid_fd, bus_pid_to_kill);
  close (write_bus_pid_fd);
  
  if (exit_with_session)
    {
      /* Bus is now started and launcher has needed info;
       * we connect to X display and tty and wait to
       * kill bus if requested.
       */
      
      kill_bus_when_session_ends ();
    }

  verbose ("Babysitter exiting\n");
  
  exit (0);
}

#define READ_END  0
#define WRITE_END 1

int
main (int argc, char **argv)
{
  const char *prev_arg;
  const char *shname;
  const char *runprog = NULL;
  int remaining_args = 0;
  int exit_with_session;
  int c_shell_syntax = FALSE;
  int bourne_shell_syntax = FALSE;
  int auto_shell_syntax = FALSE;
  int i;  
  int ret;
  int bus_pid_to_launcher_pipe[2];
  int bus_pid_to_babysitter_pipe[2];
  int bus_address_to_launcher_pipe[2];
  char *config_file;
  
  exit_with_session = FALSE;
  config_file = NULL;
  
  prev_arg = NULL;
  i = 1;
  while (i < argc)
    {
      const char *arg = argv[i];
      
      if (strcmp (arg, "--help") == 0 ||
          strcmp (arg, "-h") == 0 ||
          strcmp (arg, "-?") == 0)
        usage (0);
      else if (strcmp (arg, "--auto-syntax") == 0)
        auto_shell_syntax = TRUE;
      else if (strcmp (arg, "-c") == 0 ||
	       strcmp (arg, "--csh-syntax") == 0)
        c_shell_syntax = TRUE;
      else if (strcmp (arg, "-s") == 0 ||
	       strcmp (arg, "--sh-syntax") == 0)
        bourne_shell_syntax = TRUE;
      else if (strcmp (arg, "--version") == 0)
        version ();
      else if (strcmp (arg, "--exit-with-session") == 0)
        exit_with_session = TRUE;
      else if (strstr (arg, "--config-file=") == arg)
        {
          const char *file;

          if (config_file != NULL)
            {
              fprintf (stderr, "--config-file given twice\n");
              exit (1);
            }
          
          file = strchr (arg, '=');
          ++file;

          config_file = xstrdup (file);
        }
      else if (prev_arg &&
               strcmp (prev_arg, "--config-file") == 0)
        {
          if (config_file != NULL)
            {
              fprintf (stderr, "--config-file given twice\n");
              exit (1);
            }

          config_file = xstrdup (arg);
        }
      else if (strcmp (arg, "--config-file") == 0)
        ; /* wait for next arg */
      else
	{
	  runprog = arg;
	  remaining_args = i+1;
	  break;
	}
      
      prev_arg = arg;
      
      ++i;
    }

  if (exit_with_session)
    verbose ("--exit-with-session enabled\n");

  if (auto_shell_syntax)
    {
      if ((shname = getenv ("SHELL")) != NULL)
       {
         if (!strncmp (shname + strlen (shname) - 3, "csh", 3))
           c_shell_syntax = TRUE;
         else
           bourne_shell_syntax = TRUE;
       }
      else
       bourne_shell_syntax = TRUE;
    }  

  if (pipe (bus_pid_to_launcher_pipe) < 0 ||
      pipe (bus_address_to_launcher_pipe) < 0)
    {
      fprintf (stderr,
               "Failed to create pipe: %s\n",
               strerror (errno));
      exit (1);
    }

  bus_pid_to_babysitter_pipe[READ_END] = -1;
  bus_pid_to_babysitter_pipe[WRITE_END] = -1;
  
  ret = fork ();
  if (ret < 0)
    {
      fprintf (stderr, "Failed to fork: %s\n",
               strerror (errno));
      exit (1);
    }

  if (ret == 0)
    {
      /* Child */
#define MAX_FD_LEN 64
      char write_pid_fd_as_string[MAX_FD_LEN];
      char write_address_fd_as_string[MAX_FD_LEN];

      verbose ("=== Babysitter's intermediate parent created\n");
      
      /* Fork once more to create babysitter */
      
      if (pipe (bus_pid_to_babysitter_pipe) < 0)
        {
          fprintf (stderr,
                   "Failed to create pipe: %s\n",
                   strerror (errno));
          exit (1);              
        }
      
      ret = fork ();
      if (ret < 0)
        {
          fprintf (stderr, "Failed to fork: %s\n",
                   strerror (errno));
          exit (1);
        }
      
      if (ret > 0)
        {
          /* In babysitter */
          verbose ("=== Babysitter's intermediate parent continues\n");
          
          close (bus_pid_to_launcher_pipe[READ_END]);
          close (bus_address_to_launcher_pipe[READ_END]);
          close (bus_address_to_launcher_pipe[WRITE_END]);
          close (bus_pid_to_babysitter_pipe[WRITE_END]);
          
          /* babysit() will fork *again*
           * and will also reap the pre-forked bus
           * daemon
           */
          babysit (exit_with_session, ret,
                   bus_pid_to_babysitter_pipe[READ_END],
                   bus_pid_to_launcher_pipe[WRITE_END]);
          exit (0);
        }

      verbose ("=== Bus exec process created\n");
      
      /* Now we are the bus process (well, almost;
       * dbus-daemon itself forks again)
       */
      close (bus_pid_to_launcher_pipe[READ_END]);
      close (bus_address_to_launcher_pipe[READ_END]);
      close (bus_pid_to_babysitter_pipe[READ_END]);
      close (bus_pid_to_launcher_pipe[WRITE_END]);

      sprintf (write_pid_fd_as_string,
               "%d", bus_pid_to_babysitter_pipe[WRITE_END]);

      sprintf (write_address_fd_as_string,
               "%d", bus_address_to_launcher_pipe[WRITE_END]);

      verbose ("Calling exec()\n");
      
      execlp ("dbus-daemon",
              "dbus-daemon",
              "--fork",
              "--print-pid", write_pid_fd_as_string,
              "--print-address", write_address_fd_as_string,
              config_file ? "--config-file" : "--session",
              config_file, /* has to be last in this varargs list */
              NULL);

      fprintf (stderr,
               "Failed to execute message bus daemon: %s\n",
               strerror (errno));
      exit (1);
    }
  else
    {
      /* Parent */
#define MAX_ADDR_LEN 512
      pid_t bus_pid;  
      char bus_address[MAX_ADDR_LEN];

      verbose ("=== Parent dbus-launch continues\n");
      
      close (bus_pid_to_launcher_pipe[WRITE_END]);
      close (bus_address_to_launcher_pipe[WRITE_END]);

      verbose ("Waiting for babysitter's intermediate parent\n");
      
      /* Immediately reap parent of babysitter
       * (which was created just for us to reap)
       */
      if (do_waitpid (ret) < 0)
        {
          fprintf (stderr, "Failed to waitpid() for babysitter intermediate process: %s\n",
                   strerror (errno));
          exit (1);
        }

      verbose ("Reading address from bus\n");
      
      /* Read the pipe data, print, and exit */
      switch (read_line (bus_address_to_launcher_pipe[READ_END],
                         bus_address, MAX_ADDR_LEN))
        {
        case READ_STATUS_OK:
          break;
        case READ_STATUS_EOF:
          fprintf (stderr, "EOF in dbus-launch reading address from bus daemon\n");
          exit (1);
          break;
        case READ_STATUS_ERROR:
          fprintf (stderr, "Error in dbus-launch reading address from bus daemon: %s\n",
                   strerror (errno));
          exit (1);
          break;
        }
        
      close (bus_address_to_launcher_pipe[READ_END]);

      verbose ("Reading PID from babysitter\n");
      
      switch (read_pid (bus_pid_to_launcher_pipe[READ_END], &bus_pid))
        {
        case READ_STATUS_OK:
          break;
        case READ_STATUS_EOF:
          fprintf (stderr, "EOF in dbus-launch reading address from bus daemon\n");
          exit (1);
          break;
        case READ_STATUS_ERROR:
          fprintf (stderr, "Error in dbus-launch reading address from bus daemon: %s\n",
                   strerror (errno));
          exit (1);
          break;
        }

      close (bus_pid_to_launcher_pipe[READ_END]);
      
      if (runprog)
	{
	  char *envvar;
	  char **args;

	  envvar = malloc (strlen ("DBUS_SESSION_BUS_ADDRESS=") + strlen (bus_address) + 1);
	  args = malloc (sizeof (char *) * ((argc-remaining_args)+2));

	  if (envvar == NULL || args == NULL)
	    goto oom;

	  args[0] = xstrdup (runprog);
	  if (!args[0])
	    goto oom;
	  for (i = 1; i <= (argc-remaining_args); i++)
	    {
	      size_t len = strlen (argv[remaining_args+i-1])+1;
	      args[i] = malloc (len);
	      if (!args[i])
		goto oom;
	      strncpy (args[i], argv[remaining_args+i-1], len);
	    }
	  args[i] = NULL;

	  strcpy (envvar, "DBUS_SESSION_BUS_ADDRESS=");
	  strcat (envvar, bus_address);
	  putenv (envvar);

	  execvp (runprog, args);
	  fprintf (stderr, "Couldn't exec %s: %s\n", runprog, strerror (errno));
	  exit (1);
	}
      else
	{
	  if (c_shell_syntax)
	    printf ("setenv DBUS_SESSION_BUS_ADDRESS '%s'\n", bus_address);	
	  else
	    {
	      printf ("DBUS_SESSION_BUS_ADDRESS='%s'\n", bus_address);
	      if (bourne_shell_syntax)
		printf ("export DBUS_SESSION_BUS_ADDRESS\n");
	    }
	  if (c_shell_syntax)
	    printf ("set DBUS_SESSION_BUS_PID=%ld\n", (long) bus_pid);
	  else
	    printf ("DBUS_SESSION_BUS_PID=%ld\n", (long) bus_pid);
	}
	  
      verbose ("dbus-launch exiting\n");

      fflush (stdout);
      fflush (stderr);
      close (1);
      close (2);
      
      exit (0);
    } 
  
  return 0;
 oom:
  fprintf (stderr, "Out of memory!");
  exit (1);
}
