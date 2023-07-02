#include <stdio.h>
#include <stdlib.h>
#include "csapp.h"

volatile int cnt = 0; /* global */
sem_t mutex;          /* semaphore that protects cnt */

void *thread(void *vargp)
{
  int i, niters = *((int *)vargp);

  for (i = 0; i < niters; i++) {
    P(&mutex);
    cnt++;
    V(&mutex);
  }

  return NULL;
}

int main(int argc, char **argv)
{
  Sem_init(&mutex, 0, 1);

  int niters = atoi(argv[1]);
  pthread_t tid1, tid2;

  Pthread_create(&tid1, NULL, thread, &niters);
  Pthread_create(&tid2, NULL, thread, &niters);

  Pthread_join(tid1, NULL);
  Pthread_join(tid2, NULL);

  /* Check result */
  if (cnt != (2 * niters))
    printf("BOOM! cnt=%d\n", cnt);
  else
    printf("OK cnt=%d\n", cnt);
  exit(0);
}
