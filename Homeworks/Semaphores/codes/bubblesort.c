#include <stdio.h>
#include <stdlib.h>

/* linear congruent */
int f()
{
  static const int a = 1103515245, c = 12345;
  static int x = 20010610;

  return x = a*x + c;
}

int main(int argc, char **argv)
{
  int n = atoi(argv[1]);
  int i, j, t;

  int *arr = malloc(n * sizeof(int));

  if (!arr) {
    printf("malloc error\n");
    exit(-1);
  }

  for (i = 0; i < n; i++)
    arr[i] = f();

  for (i = 0; i < n; i++)
    for (j = i+1; j < n; j++)
      if (arr[j-1] > arr[j]) {
        t = arr[j-1];
        arr[j-1] = arr[j];
        arr[j] = t;
      }

  free(arr);
  arr = NULL;

  exit(0);
}
