/* This is a process that just sleeps infinitely. */

#include <unistd.h>

int
main (int argc, char **argv)
{
  while (1)
    sleep (10000000);
  
  return 1;
}
