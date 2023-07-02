/* 
 * myreturn.c - A handy program for testing your tiny shell 
 * 
 * usage: myreturn <n> <r>
 * Sleeps for <n> seconds in 1-second chunks and return <r>.
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char **argv) 
{
  int i, secs;

  if (argc != 3) {
    fprintf(stderr, "Usage: %s <n> <r>\n", argv[0]);
    exit(0);
  }
  secs = atoi(argv[1]);
  for (i=0; i < secs; i++)
    sleep(1);
  exit(atoi(argv[2]));
}

