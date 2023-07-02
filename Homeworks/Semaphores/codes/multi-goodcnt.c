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

  int nthreads = atoi(argv[1]);
	int niters = atoi(argv[2]);
	int i;

	pthread_t *tids = malloc(nthreads * sizeof(pthread_t));
	if (!tids) {
	  printf("malloc failed\n");
	  exit(-1);
  }

  for (i = 0; i < nthreads; i++)
	  Pthread_create(&tids[i], NULL, thread, &niters);

  for (i = 0; i < nthreads; i++)
	  Pthread_join(tids[i], NULL);
	
	free(tids);
	tids = NULL;

	/* Check result */
	if (cnt != (nthreads * niters))
		printf("BOOM! cnt=%d\n", cnt);
	else
		printf("OK cnt=%d\n", cnt);

	exit(0);
}
