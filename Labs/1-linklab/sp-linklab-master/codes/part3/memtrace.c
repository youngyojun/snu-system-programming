//------------------------------------------------------------------------------
//
// memtrace
//
// trace calls to the dynamic memory manager
//
#define _GNU_SOURCE

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memlog.h>
#include <memlist.h>

//
// function pointers to stdlib's memory management functions
//
static void* (*mallocp) (size_t size)               = NULL;
static void* (*callocp) (size_t nmemb, size_t size) = NULL;
static void* (*reallocp)(void *ptr, size_t size)    = NULL;
static void  (*freep)   (void *ptr)                 = NULL;

//
// statistics & other global variables
//
static unsigned long n_malloc  = 0;
static unsigned long n_calloc  = 0;
static unsigned long n_realloc = 0;
static unsigned long n_allocb  = 0;
static unsigned long n_freeb   = 0;
static item *list = NULL;

//
// init - this function is called once when the shared library is loaded
//
__attribute__((constructor))
void init(void)
{
  char *error;

  dlerror();

  LOG_START();

  // initialize a new list to keep track of all memory (de-)allocations
  // (not needed for part 1)
  list = new_list();

  if (!mallocp)
  {
    mallocp = dlsym(RTLD_NEXT, "malloc");
    if ((error = dlerror()) || !mallocp)
    {
      fprintf(stderr, "Error getting symbol 'malloc': %s\n", error);
      exit(EXIT_FAILURE);
    }
  }

  if (!callocp)
  {
    callocp = dlsym(RTLD_NEXT, "calloc");
    if ((error = dlerror()) || !callocp)
    {
      fprintf(stderr, "Error getting symbol 'calloc': %s\n", error);
      exit(EXIT_FAILURE);
    }
  }

  if (!reallocp)
  {
    reallocp = dlsym(RTLD_NEXT, "realloc");
    if ((error = dlerror()) || !reallocp)
    {
      fprintf(stderr, "Error getting symbol 'realloc': %s\n", error);
      exit(EXIT_FAILURE);
    }
  }

  if (!freep)
  {
    freep = dlsym(RTLD_NEXT, "free");
    if ((error = dlerror()) || !freep)
    {
      fprintf(stderr, "Error getting symbol 'free': %s\n", error);
      exit(EXIT_FAILURE);
    }
  }
}

//
// fini - this function is called once when the shared library is unloaded
//
__attribute__((destructor))
void fini(void)
{
  unsigned long alloc_tot = n_malloc + n_calloc + n_realloc;
  unsigned long alloc_avg = n_allocb ? alloc_tot / n_allocb : 0;
  int nonfreed_flag = 0;

  LOG_STATISTICS(alloc_tot, alloc_avg, n_freeb);

  for (item *i = list; i; i = i->next)
  {
    if (0 < i->cnt)
    {
      if (!nonfreed_flag)
      {
        nonfreed_flag = 1;
        LOG_NONFREED_START();
      }
      LOG_BLOCK(i->ptr, i->size, i->cnt);
    }
  }

  LOG_STOP();

  // free list (not needed for part 1)
  free_list(list);
  list = NULL;
}

void* malloc(size_t size) {
  void *ptr = mallocp(size);

  alloc(list, ptr, size);
  LOG_MALLOC(size, ptr);

  n_malloc += size;
  n_allocb++;

  return ptr;
}

void* calloc(size_t nmemb, size_t size) {
  void *ptr = callocp(nmemb, size);

  alloc(list, ptr, size);
  LOG_CALLOC(nmemb, size, ptr);

  n_calloc += size;
  n_allocb++;

  return ptr;
}

void* realloc(void* ptr, size_t size) {
  void *nptr = reallocp(ptr, size);

  LOG_REALLOC(ptr, size, nptr);

  n_freeb += find(list, ptr)->size;
  n_realloc += size;
  n_allocb++;

  dealloc(list, ptr);
  alloc(list, nptr, size);

  return nptr;
}

void free(void* ptr) {
  item* i = find(list, ptr);

  LOG_FREE(ptr);

  if (!i)
  {
    LOG_ILL_FREE();
    return;
  }

  if (!i->cnt)
  {
    LOG_DOUBLE_FREE();
    return;
  }

  dealloc(list, ptr);
  n_freeb += i->size;

  freep(ptr);
}
