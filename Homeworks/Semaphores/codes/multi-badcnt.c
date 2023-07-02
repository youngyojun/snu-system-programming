#include <stdio.h>
#include <stdlib.h>
#include "csapp.h"

volatile int cnt = 0; /* global */

void *thread(void *vargp)
{
	int i, niters = *((int *)vargp);

	for (i = 0; i < niters; i++)
		cnt++;
	
	return NULL;
}

int main(int argc, char **argv)
{
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
