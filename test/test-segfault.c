/* This is simply a process that segfaults */
#include <signal.h>

#include <sys/time.h>
#include <sys/resource.h>

int
main (int argc, char **argv)
{
  char *p;  

  struct rlimit r = { 0, };
  
  getrlimit (RLIMIT_CORE, &r);
  r.rlim_cur = 0;
  setrlimit (RLIMIT_CORE, &r);
  
  raise (SIGSEGV);

  p = 0;
  *p = 'a';
  
  return 0;
}
