/* This is simply a process that segfaults */
#include <signal.h>

int
main (int argc, char **argv)
{
  char *p = 0;
  
  raise (SIGSEGV);

  *p = 'a';
  
  return 0;
}
