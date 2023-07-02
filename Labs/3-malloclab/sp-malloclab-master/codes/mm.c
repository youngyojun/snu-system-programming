/*
 * mm.c - The outstanding malloc package.
 *
 * 2020-14378
 * stu70@sp01.snucse.org
 *
 * Gyojun Youn
 * youngyojun [at] snu.ac.kr
 *
 * College of Engineering
 * Dept. of Computer Science and Engineering
 *
 *
 * Block structure specification
 * - Freed blocks
 *   - Structure
 *       byte:      4     4         s-8          4     4
 *               +-----+-----+----------------+-----+-----+
 *               |  h  |  p  |  (free space)  |  f  |  n  |
 *               +-----+-----+----------------+-----+-----+
 *               ^     ^     ^                ^     ^     ^
 *       addr:  bp-8  bp-4   bp            bp+s-8 bp+s-4 bp+s
 *   - Metadata
 *     - s  := allocatable bytes (aligned in double-word)
 *     - bp := block pointer address
 *     - h  := header metadata (== s)
 *     - f  := footer metadata (== s ^ FOOT_HSH)
 *     - p  := previous freed block address
 *     - n  := next freed block address
 *
 * - Allocated blocks
 *   - Structure
 *       byte:      4       4                s
 *               +-----+----------+---------------------+
 *               |  h  | (unused) |  (allocated space)  |
 *               +-----+----------+---------------------+
 *               ^     ^          ^                     ^
 *       addr:  bp-8  bp-4        bp                   bp+s
 *   - Metadata
 *     - s  := allocatable bytes (aligned in double-word)
 *     - bp := block pointer address
 *     - h  := header metadata (== s | 0x1)
 *
 *
 * Segregated free list specification
 * - 14 size classes;
 *      (0,16], (16,24], (24,32], (32,48], (48,64],
 *      (64,128], (128,256], ..., (8192,16384], (16384,+inf)
 * - LIFO bidirectional linked lists
 * - Best fit with truncated 64 blocks per class
 *
 *
 * Tricks
 * - Increase request size by a little
 *      to prepare further re-allocation
 * - Use magic hash value FOOT_HSH for footer of freed blocks
 *      to prevent memory hack
 * - Merge and use adjacent freed blocks in the case of re-allocation
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "mm.h"
#include "memlib.h"

typedef uint32_t uint;


/*
 * Basic constants.
 */
#define WSIZE     (4)   /* single-word in bytes     */
#define DSIZE     (8)   /* double-word in bytes     */
#define QSIZE     (16)  /* quadruple-word in bytes  */
#define ALIGNMENT (8)   /* double-word alignment    */


/*
 * Important constants.
 */
#define NUM_CLASS (14)            /* number of size classes               */
#define MAX_SIZE  (0x3f3f3f30u)   /* maximum bytes allowed to allocate    */
#define FOOT_HSH  (0xcc0708f8u)   /* xor hash value for footer blocksize  */


/*
 * Macros.
 */

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size)             (((size) + (ALIGNMENT-1)) & ~0x7)

#define GET(p)                  (*(uint*)(p))             /* get uint value of a word at p */
#define PUT(p,v)                ((*(uint*)(p)) = (v))     /* write uint word value at p */

#define FLAG_ALLOC(p)           ((*(uint*)(p)) |=  0x1)   /* flag allocated at p */
#define FLAG_FREED(p)           ((*(uint*)(p)) &= ~0x1)   /* flag freed at p */

#define GET_SIZE(v)             ((v) & ~0x7)              /* get size in bytes */
#define IS_ALLOC(v)             ((v) &  0x1)              /* is allocated block */

/* get root pointer of the corresponding class with block size s */
#define GET_ROOT(s)             (((void*)(listptr)) + (get_class(s) * WSIZE))

/* the header address where the block size is stored */
#define HEAD_SIZE_PTR(bp)       (((void*)(bp)) - DSIZE)

/* read block size of a valid block pointer bp */
#define READ_SIZE(bp)           (GET_SIZE(GET(HEAD_SIZE_PTR(bp))))

/* the footer address where the block size is stored */
#define FOOT_SIZE_PTR(bp)       (((void*)(bp)) + READ_SIZE(bp) - DSIZE)

/* the addresses where the prev/next bp address is stored */
#define PREV_PTR(bp)            (((void*)(bp)) - WSIZE)
#define NEXT_PTR(bp)            (((void*)(bp)) + READ_SIZE(bp) - WSIZE)

/* read prev of a valid freed block pointer bp */
#define READ_PREV(bp)           ((void*)(GET(PREV_PTR(bp))))

/* read next of a valid freed block pointer bp */
#define READ_NEXT(bp)           ((void*)(GET(NEXT_PTR(bp))))

/* set size/prev/next of a valid freed block pointer bp */
#define SET_HEAD_SIZE(bp,v)     (PUT(HEAD_SIZE_PTR(bp), (v)))
#define SET_FOOT_SIZE(bp,v)     (PUT(FOOT_SIZE_PTR(bp), (v)))
#define SET_PREV(bp,prev_bp)    (PUT(PREV_PTR(bp)     , (uint)(prev_bp)))
#define SET_NEXT(bp,next_bp)    (PUT(NEXT_PTR(bp)     , (uint)(next_bp)))


/*
 * Dynamic storage allocator functions.
 */
#ifdef __MM_YGJ_DEBUG__
static int dbg_init(void);
static void* dbg_malloc(size_t size);
static void dbg_free(void *ptr);
static void* dbg_realloc(void *ptr, size_t size);
#else   /* __MM_YGJ_DEBUG__ */
int mm_init(void);
void* mm_malloc(size_t size);
void mm_free(void *ptr);
void* mm_realloc(void *ptr, size_t size);
#endif  /* __MM_YGJ_DEBUG__ */


/*
 * Low-level structural helpers.
 */
static void remove_footer(void *bp);
static void* valid_foot_bp(void *ptr);
static void* get_lower_fb(void *bp);
static void* get_upper_fb(void *bp);
static void* merge_fb(void *lo_bp, void *up_bp);


/*
 * Linked list helpers.
 */
static void ll_pop(void *bp);
static void ll_push(void *rt, void *bp);


/*
 * Basic memory block builders.
 */
static void write_freed_info(void *bp, uint bsize);
static void write_alloc_info(void *bp, uint bsize);


/*
 * Dynamic allocation core functions.
 */
static uint get_class(uint bsize);
static uint enrich_size(uint bsize);
static void* split_block(void *bp, uint sp_size);
static void* extend_heap(uint bsize);
static void* find_fit(uint bsize);
static void* coal(void *bp);
static void report_fb(void *bp);
static void* extend_block(void *bp);


/*
 * Memory checker functions.
 */
int mm_check(void);
#ifdef __MM_YGJ_DEBUG__
int mm_init(void);
void* mm_malloc(size_t size);
void mm_free(void *ptr);
void* mm_realloc(void *ptr, size_t size);
#endif  /* __MM_YGJ_DEBUG__ */


/*
 * Important global variables.
 */

/* start and end pointer for heap ; [start, end) */
void *heap_start  = NULL;
void *heap_end    = NULL;

/* pointer of segregated free list */
void *listptr     = NULL;



/***************************************
 * Dynamic storage allocator functions *
 ***************************************/

/* 
 * mm_init - Initialize the malloc package.
 */
#ifdef __MM_YGJ_DEBUG__
static int dbg_init(void)
#else   /* __MM_YGJ_DEBUG__ */
int mm_init(void)
#endif  /* __MM_YGJ_DEBUG__ */
{
  /* get NUM_CLASS words for segregated free list headers */
  if ((void*)(-1) == (listptr = mem_sbrk(NUM_CLASS * WSIZE))) {
    /* mem_sbrk error */
    return -1;
  }

  /* initialize header pointers as nullptr */
  for (int i = 0; i < NUM_CLASS; i++) {
    PUT(listptr + (i * WSIZE), 0);
  }

  /* set start/end heap pointer */
  heap_start = heap_end = listptr + (NUM_CLASS * WSIZE);

  /* successfully initialized */
  return 0;
}


/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *    Always allocate a block whose size is a multiple of the alignment.
 */
#ifdef __MM_YGJ_DEBUG__
static void* dbg_malloc(size_t size)
#else   /* __MM_YGJ_DEBUG__ */
void* mm_malloc(size_t size)
#endif  /* __MM_YGJ_DEBUG__ */
{
  /* trivial case */
  if (!size) return NULL;

  /* too large size */
  if (MAX_SIZE < size) {
    /* out of memory error */
    errno = ENOMEM;
    return NULL;
  }

  /* get enriched size for further re-allocations */
  size = enrich_size(size);

  /* find the best fit for given block size */
  void *bp = find_fit(size);

  /* if found, then return */
  if (bp) {
    /* shrink if possible */
    if (split_block(bp, size)) return bp;

    /* cannot shrink */

    /* flag allocated in header */
    FLAG_ALLOC(HEAD_SIZE_PTR(bp));

    return bp;
  }

  /* no appropriate freed blocks are found */

  /* get a new freed block by extending the heap */
  bp = extend_heap(size);

  /* extending heap failed */
  if (NULL == bp) {
    /* out of memory error */
    errno = ENOMEM;
    return NULL;
  }

  return bp;
}


/*
 * mm_free - Freeing a block does nothing.
 */
#ifdef __MM_YGJ_DEBUG__
static void dbg_free(void *ptr)
#else   /* __MM_YGJ_DEBUG__ */
void mm_free(void *ptr)
#endif  /* __MM_YGJ_DEBUG__ */
{
  /* trivial case */
  if (NULL == ptr) return;

  /* valid (legally allocated) ptr is given */

  /* write freed info at the block */
  write_freed_info(ptr, READ_SIZE(ptr));

  /* coalesce about the block */
  ptr = coal(ptr);

  /* report new freed block */
  report_fb(ptr);
}


/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free.
 */
#ifdef __MM_YGJ_DEBUG__
static void* dbg_realloc(void *ptr, size_t size)
#else   /* __MM_YGJ_DEBUG__ */
void* mm_realloc(void *ptr, size_t size)
#endif  /* __MM_YGJ_DEBUG__ */
{
  /* equivalent to mm_malloc(size) */
  if (NULL == ptr) {
    return mm_malloc(size);
  }

  /* equivalent to mm_free(size) */
  if (!size) {
    mm_free(ptr);
    return NULL;
  }

  /* valid (legally allocated) ptr is given */

  /* original block size */
  uint bsize = READ_SIZE(ptr);

  /* enriched required size */
  uint rich_size = enrich_size(size);

  /* shrink if possible */
  if (split_block(ptr, rich_size)) return ptr;

  /*
   * the block has enough space
   * but cannot shrink for a new freed block
   */
  if (size <= bsize) {
    /* nothing to do */
    return ptr;
  }

  /* the block space is too small */

  /* try to extend the block up/down-wards */
  ptr = extend_block(ptr);

  /* re-compute block size */
  bsize = READ_SIZE(ptr);

  /*
   * if the block space becomes large enough,
   * then try to re-allocate again
   */
  if (size <= bsize) {
    return mm_realloc(ptr, size);
  }

  /* allocate new block */
  void *bp = mm_malloc(size);

  /* malloc; out of memory error */
  if (NULL == bp) return NULL;

  /* copy data; bsize < size */
  memcpy(bp, ptr, bsize);

  /* free the old block */
  mm_free(ptr);

  return bp;
}



/********************************
 * Low-level structural helpers *
 ********************************/

/*
 * remove_footer - Remove block size metadata
 *    of the freed block pointer bp.
 *    It must be done just before allocation at bp.
 */
static void remove_footer(void *bp)
{
  SET_FOOT_SIZE(bp, 0);
}


/*
 * valid_foot_bp - If ptr points a footer of the freed block,
 *    then return its block pointer.
 *    Otherwise, return nullptr.
 *    ptr must be aligned.
 */
static void* valid_foot_bp(void *ptr)
{
  /* check the validity of ptr */
  if ((ptr <= heap_start)
    || (heap_end - DSIZE <= ptr)) return NULL;

  /* decrypt block size information */
  uint bsize = GET(ptr) ^ FOOT_HSH;

  /* check freed 3-bit */
  if (bsize & 0x7) return NULL;
  
  /* check the validity of bsize */
  if (!bsize || ptr - heap_start < bsize) return NULL;

  /* check the validity of the header */
  if (GET(ptr - bsize) != bsize) return NULL;

  /*
   * everything is good
   * return the corresponding block pointer
   */
  return ptr - bsize + DSIZE;
}


/*
 * get_lower_fb - Get block pointer of the lower freed block if exists.
 *    If not, then return nullptr.
 *    bp must be a valid block pointer.
 */
static void* get_lower_fb(void *bp)
{
  /* check the validity of the footer of lower freed block */
  if (bp < heap_start + QSIZE) return NULL;

  return valid_foot_bp(bp - QSIZE);
}


/*
 * get_upper_fb - Get block pointer of the upper freed block if exists.
 *    If not, then return nullptr.
 *    bp must be a valid block pointer.
 */
static void* get_upper_fb(void *bp)
{
  /* get the very first address of the upper block */
  void *ptr = bp + READ_SIZE(bp);

  /* check the validity of ptr */
  if (heap_end <= ptr + DSIZE) return NULL;

  /* check freed 3-bit */
  if (GET(ptr) & 0x7) return NULL;

  /*
   * everything is good
   * return the corresponding block pointer
   */
  return ptr + DSIZE;
}


/*
 * merge_fb - Merge two adjacent free blocks,
 *    and return the merged freed-normalized block.
 *    Both lo_bp and up_bp must be valid freed blocks,
 *    and adjacent with lo_bp < up_bp,
 *    but do not have to be normalized.
 *    lo_bp and up_bp will lose their prev/next.
 */
static void* merge_fb(void *lo_bp, void *up_bp)
{
  /*
   * get merged block size;
   * READ_SIZE(lo_bp) for allocated space in lo_bp
   * READ_SIZE(up_bp) for allocated space in up_bp
   * 2 words for header of up_bp
   */
  uint bsize = READ_SIZE(lo_bp) + READ_SIZE(up_bp) + DSIZE;

  /* set useless pointers as nullptr */
  SET_PREV(lo_bp, NULL);
  SET_NEXT(up_bp, NULL);

  /* write new block size */
  SET_FOOT_SIZE(up_bp, bsize ^ FOOT_HSH);
  SET_HEAD_SIZE(lo_bp, bsize);

  return lo_bp;
}



/***********************
 * Linked list helpers *
 ***********************/

/*
 * ll_pop - Linked list element pop.
 *    bp must be a valid freed block pointer.
 */
static void ll_pop(void *bp)
{
  /* get prev/next block pointers */
  void *prev_bp = READ_PREV(bp);  /* prev_bp must be not nullptr */
  void *next_bp = READ_NEXT(bp);

  if (next_bp) SET_PREV(next_bp, prev_bp);

  if (prev_bp < heap_start) {
    /* prev_bp points one of the roots */
    PUT(prev_bp, (uint)next_bp);
  } else {
    /* prev_bp points a valid freed block */
    SET_NEXT(prev_bp, next_bp);
  }

  /* remove prev/next information */
  SET_PREV(bp, NULL);
  SET_NEXT(bp, NULL);
}


/*
 * ll_push - Linked list element push.
 *    rt must be a valid linked list root.
 *    bp must be a valid freed block pointer.
 */
static void ll_push(void *rt, void *bp)
{
  void *next_bp = (void*)GET(rt);

  SET_NEXT(bp, (uint)next_bp);
  SET_PREV(bp, rt);
  
  if (next_bp) SET_PREV(next_bp, bp);

  PUT(rt, (uint)bp);
}



/*******************************
 * Basic memory block builders *
 *******************************/

/*
 * write_freed_info - Write freed metadata for any pointer bp.
 *    bp will be a valid freed-normalized block pointer.
 *    bsize must be a valid block size.
 */
static void write_freed_info(void *bp, uint bsize)
{
  /* clear prev/next info */
  SET_PREV(bp, NULL);
  PUT(bp + bsize - WSIZE, (uint)NULL);  /* do not use SET_NEXT */

  /* write appropriate info at header and footer */
  PUT(HEAD_SIZE_PTR(bp), bsize);
  PUT(FOOT_SIZE_PTR(bp), bsize ^ FOOT_HSH);
}


/*
 * write_alloc_info - Write allocation medatdata
 *    for any pointer bp.
 *    bp will be a valid allocated block pointer.
 *    bsize must be a valid block size.
 */
static void write_alloc_info(void *bp, uint bsize)
{
  SET_HEAD_SIZE(bp, bsize);
  FLAG_ALLOC(HEAD_SIZE_PTR(bp));

  SET_PREV(bp, NULL);
}



/*************************************
 * Dynamic allocation core functions *
 *************************************/

/*
 * get_class - Return the index of the corresponding class
 *    with the block size bsize.
 *    If bsize is zero, then return 0.
 */
static uint get_class(uint bsize)
{
       if (bsize > 16384) return 13;
  else if (bsize > 8192 ) return 12;
  else if (bsize > 4096 ) return 11;
  else if (bsize > 2048 ) return 10;
  else if (bsize > 1024 ) return 9;
  else if (bsize > 512  ) return 8;
  else if (bsize > 256  ) return 7;
  else if (bsize > 128  ) return 6;
  else if (bsize > 64   ) return 5;
  else if (bsize > 48   ) return 4;
  else if (bsize > 32   ) return 3;
  else if (bsize > 24   ) return 2;
  else if (bsize > 16   ) return 1;
  else                    return 0;
}


/*
 * enrich_size - Return more relaxable block size.
 *    It is guaranteed that the return value is aligned and at least bsize.
 */
static uint enrich_size(uint bsize)
{
  if (7048 <= bsize && bsize <= 8192) return 8192;
  if (3524 <= bsize && bsize <= 4096) return 4096;
  if (1760 <= bsize && bsize <= 2048) return 2048;
  if (880  <= bsize && bsize <= 1024) return 1024;
  if (440  <= bsize && bsize <= 512 ) return 512;
  if (224  <= bsize && bsize <= 256 ) return 256;
  return ALIGN(bsize);
}


/*
 * split_block - Split arbitrary block into two blocks;
 *    the lower one allocates the same space as the given,
 *    the upper one is a valid freed block.
 *    If succeeded, then return the upper freed block.
 *    If failed, then return nullptr.
 */
static void* split_block(void *bp, uint sp_size)
{
  /* get size of the original block */
  uint bsize = READ_SIZE(bp);

  /* the block has enough space to split */
  if (sp_size + QSIZE <= bsize) {
    /* shrink the block */
    SET_HEAD_SIZE(bp, sp_size);
    FLAG_ALLOC(HEAD_SIZE_PTR(bp));

    /* free unused space */
    void *free_bp = bp + sp_size + DSIZE;
    write_freed_info(free_bp, bsize - sp_size - DSIZE);

    free_bp = coal(free_bp);  /* == free_bp */
    report_fb(free_bp);

    return free_bp;
  }

  /* cannot split and shrink */
  return NULL;
}


/*
 * extend_heap - Extend heap and return a new freed-normalized block.
 *    It is guaranteed that the size of the block is aligned and at least bsize.
 *    Return nullptr if out of memory error occurs.
 */
static void* extend_heap(uint bsize)
{
  /* bsize must be a multiple of 8 */
  bsize = ALIGN(bsize);

  /* trivial case */
  if (!bsize) return NULL;

  /*
   * increase heap area by (size + 8) bytes;
   * 2 words for header,
   * size bytes for allocation
   */
  uint incr = bsize + DSIZE;
  void *prev_brk = mem_sbrk(incr);

  /* mem_sbrk error */
  if ((void*)(-1) == prev_brk) {
    return NULL;
  }

  /* update heap_end */
  heap_end = prev_brk + incr;

  /* block pointer for the new area */
  void *bp = prev_brk + DSIZE;
  write_alloc_info(bp, bsize);

  return bp;
}


/*
 * find_fit - Find appropriate freed block
 *    whose block size is at least bsize.
 *    The returned block will be removed from the linked list,
 *    and ready to be allocated.
 *    If not found, then return nullptr.
 */
static void* find_fit(uint bsize)
{
  /* root of the linked list */
  void *rt = GET_ROOT(bsize);

  /* best block pointer to return */
  void *ret_bp = NULL;

  /* block size of ret_bp; find the minimum */
  uint ret_bsize = (uint)(-1);
  
  /* traversal each linked list */
  while (rt < heap_start) {
    /* freed block pointer in the linked list rt */
    void *bp = (void*)GET(rt);

    /* maximum number of moves */
    uint left_cnt = 64;

    while (bp && left_cnt) {
      uint nw_bsize = READ_SIZE(bp);

      if (bsize <= nw_bsize && nw_bsize < ret_bsize) {
        ret_bp = bp;
        ret_bsize = nw_bsize;
      }

      bp = READ_NEXT(bp);
      left_cnt -= 1;
    }

    /* move to the next linked list */
    rt += WSIZE;
  }

  /* if not found, then return nullptr */
  if (NULL == ret_bp) return NULL;

  /* remove the found block pointer from the linked list */
  ll_pop(ret_bp);

  /*
   * remove footer metadata;
   * it must be done in order to prevent memory hack
   */
  remove_footer(ret_bp);

  return ret_bp;
}


/*
 * coal - Coalesce adjacent free blocks,
 *    and return the block pointer of (merged) freed block.
 *    bp must be a freed-normalized valid block pointer.
 */
static void* coal(void *bp)
{
  /* get adjacent free blocks */
  void *lo_bp = get_lower_fb(bp);
  void *up_bp = get_upper_fb(bp);

  if (up_bp) {
    ll_pop(up_bp);
    merge_fb(bp, up_bp);      /* == bp    */
  }

  if (lo_bp) {
    ll_pop(lo_bp);
    bp = merge_fb(lo_bp, bp); /* == lo_bp */
  }

  return bp;
}


/*
 * report_fb - Report a new freed block bp.
 *    bp must be a valid freed-normalized block pointer,
 *    and not have been reported yet.
 */
static void report_fb(void *bp)
{
  /* get block size */
  uint bsize = READ_SIZE(bp);

  /* get root of the corresponding class */
  void *rt = GET_ROOT(bsize);

  /* push bp into rt linked list */
  ll_push(rt, bp);
}


/*
 * extend_block - Extend already allocated block
 *    using adjacent free blocks.
 *    bp must be a valid allocated block pointer.
 */
static void* extend_block(void *bp)
{
  /* get adjacent free blocks */
  void *lo_bp = get_lower_fb(bp);
  void *up_bp = get_upper_fb(bp);

  /* nothing to do */
  if (NULL == lo_bp && NULL == up_bp) return bp;

  /*
   * remove footers to prevent memory hack,
   * and pop them from the linked list
   */
  if (lo_bp) {
    ll_pop(lo_bp);
    remove_footer(lo_bp);
  }
  if (up_bp) {
    ll_pop(up_bp);
    remove_footer(up_bp);
  }

  if (NULL == lo_bp) {
    /* just extend bp upward */
    uint new_size = READ_SIZE(bp) + DSIZE + READ_SIZE(up_bp);
    SET_HEAD_SIZE(bp, new_size);
    FLAG_ALLOC(HEAD_SIZE_PTR(bp));
    return bp;
  }

  uint new_size = READ_SIZE(bp)
                  + DSIZE + READ_SIZE(lo_bp)
                  + (up_bp ? DSIZE + READ_SIZE(up_bp) : 0);

  SET_HEAD_SIZE(lo_bp, new_size);
  FLAG_ALLOC(HEAD_SIZE_PTR(lo_bp));

  memmove(lo_bp, bp, READ_SIZE(bp));

  return lo_bp;
}



/****************************
 * Memory checker functions *
 ****************************/

/*
 * mm_check - Check every validity and assumptions.
 *    If succeeded, then return 0.
 *    If failed, then assertion failed error occurs.
 */
int mm_check(void)
{
#ifdef __MM_YGJ_DEBUG__

  fprintf(stderr, "[mm_check] called\n");
  fprintf(stderr, "[mm_check] listptr=%p, heap_start=%p, head_end=%p\n", listptr, heap_start, heap_end);

  /* check the validity of global variables */
  assert(mem_heap_lo() == listptr);
  assert(listptr + (NUM_CLASS * WSIZE) == heap_start);
  assert(heap_start <= heap_end);
  assert(mem_heap_hi() + 1 == heap_end);

  uint cnt_alloc  = 0;  /* number of allocated blocks       */
  uint cnt_freed  = 0;  /* number of freed blocks           */
  uint flag_con   = 0;  /* bitmask of non-zero linked lists */

  /*
   * check all blocks in heap area
   */

  fprintf(stderr, "[mm_check] start loop blocks\n");

  void *cur_ptr = heap_start; /* current block start pointer */
  uint is_prev_freed = 0;     /* is the previous block freed */

  while (cur_ptr < heap_end) {
    /* get block pointer */
    void *bp = cur_ptr + DSIZE;

    /* check bp is aligned */
    assert(ALIGN((uint)bp) == (uint)bp);

    /* check bp is in heap area */
    assert(bp < heap_end);

    /* get header metadata */
    uint head_data = GET(HEAD_SIZE_PTR(bp));

    /* get block size */
    uint bsize = GET_SIZE(head_data);

    /* check the validity of bsize */
    assert(bsize);  /* non-zero */
    assert(bsize <= heap_end - bp);   /* end of the block (= bp + bsize) <= heap_end */
    assert(ALIGN(bsize) == bsize);    /* aligned */

    if (IS_ALLOC(head_data)) {
      /* allocated block */

      /* check the validity of head_data */
      assert((bsize | 0x1) == head_data);

      /* it is a valid allocated block */
      cnt_alloc++;
      is_prev_freed = 0;

      fprintf(stderr, "[mm_check] %uth allocated block bp=%p, bsize=%u\n", cnt_alloc, bp, bsize);
    } else {
      /* freed block */

      /* check the validity of head_data */
      assert(head_data == bsize);

      /* get footer metadata */
      uint foot_data = GET(FOOT_SIZE_PTR(bp));

      /* decrypt the footer */
      foot_data ^= FOOT_HSH;

      /* check the validity of foot_data */
      assert(foot_data == bsize);

      /* get prev freed block pointers */
      void *prev_bp = READ_PREV(bp);

      /* check the validity of prev_bp */
      assert(prev_bp);  /* not nullptr */
      assert(listptr <= prev_bp); /* check the range [listptr, heap_end) */
      assert(prev_bp < heap_end);
      assert(prev_bp != bp);      /* not self-loop */
      if (prev_bp < heap_start) {
        /* prev_bp points a root of linked list */

        assert(0 == (((uint)(prev_bp - listptr)) & 0x3)); /* aligned in word */
        assert(GET(prev_bp) == (uint)bp);   /* root points bp */

        uint ll_id = (uint)(prev_bp - listptr) >> 2; /* id of the linked list */

        /* check the validity of ll_id */
        assert(0 <= ll_id);   /* check the range [0, NUM_CLASS) */
        assert(ll_id < NUM_CLASS);

        assert(0 == ((flag_con >> ll_id) & 0x1)); /* not in flag_con yet */
        flag_con |= 0x1u << ll_id;  /* set in */
      } else {
        /* prev_bp points a valid freed block pointer */

        /* check the validity of prev_bp as a freed block pointer */
        assert(ALIGN((uint)prev_bp) == (uint)prev_bp);  /* aligned */
        assert(DSIZE <= prev_bp - heap_start);  /* heap_start <= prev_bp - DSIZE */
        assert(0 == (GET(HEAD_SIZE_PTR(prev_bp)) & 0x7));   /* freed */
        assert(heap_start < NEXT_PTR(prev_bp)); /* in heap area */
        assert(NEXT_PTR(prev_bp) <= heap_end - WSIZE);
        assert(READ_NEXT(prev_bp) == bp);    /* prev->next is idempotent */
      }

      /* get next freed block pointers */
      void *next_bp = READ_NEXT(bp);

      /* check the validity of next_bp */
      if (next_bp) {  /* might be nullptr */
        /* next_bp points a valid freed block pointer */

        assert(heap_start < next_bp); /* check the range (heap_start, heap_end) */
        assert(next_bp < heap_end);
        assert(next_bp != bp);        /* not self-loop */

        /* check the validity of next_bp as a freed block pointer */
        assert(ALIGN((uint)next_bp) == (uint)next_bp);      /* aligned */
        assert(DSIZE <= next_bp - heap_start);  /* heap_start <= next_bp - DSIZE */
        assert(0 == (GET(HEAD_SIZE_PTR(next_bp)) & 0x7));   /* freed */
        assert(heap_start < NEXT_PTR(next_bp)); /* in heap area */
        assert(NEXT_PTR(next_bp) <= heap_end - WSIZE);
        assert(heap_start <= PREV_PTR(next_bp));
        assert(READ_PREV(next_bp) == bp);    /* next->prev is idempotent */
      }

      assert(prev_bp != next_bp); /* prev and next cannot be the same */

      assert(0 == is_prev_freed); /* no adjacent freed blocks */

      /* it is a valid freed block */
      cnt_freed++;
      is_prev_freed = 1;

      fprintf(stderr, "[mm_check] %uth freed block bp=%p, bsize=%u ; prev=%p, next=%p\n", cnt_freed, bp, bsize, prev_bp, next_bp);
    }

    cur_ptr = bp + bsize;
  }

  fprintf(stderr, "[mm_check] end loop blocks\n");
  fprintf(stderr, "[mm_check] total; cnt_alloc=%u, cnt_freed=%u\n", cnt_alloc, cnt_freed);

  assert(cur_ptr == heap_end);  /* loop ends well */

  /*
   * traversal along the linked lists
   */

  fprintf(stderr, "[mm_check] start loop linked lists\n");

  void *root_ptr = listptr; /* root of the current linked list */
  uint class_id = 0;        /* current size class index */
  uint cnt_llfb = 0;        /* number of freed blocks in linked lists */

  while (root_ptr < heap_start) {
    uint cnt_cur = 0; /* number of blocks in the current linked list */
    void *bp = (void*)GET(root_ptr); /* freed block pointer */

    fprintf(stderr, "[mm_check] current root %p of id=%u\n", root_ptr, class_id);

    /* check that it meets with flag_con */
    assert((NULL != bp) == ((flag_con >> class_id) & 0x1));

    while (bp) {
      /* check the validity of bp */
      assert(heap_start + DSIZE <= bp);   /* in heap area */
      assert(bp <= heap_end - DSIZE);

      /* it is guarenteed that bp is a valid freed block */

      fprintf(stderr, "[mm_check] freed block bp=%p, bsize=%u\n", bp, READ_SIZE(bp));

      assert(GET_ROOT(READ_SIZE(bp)) == root_ptr);  /* in the correct linked list */

      cnt_cur++;
      bp = READ_NEXT(bp);
    }

    fprintf(stderr, "[mm_check] linked list %p of id=%u has %u freed blocks\n", root_ptr, class_id, cnt_cur);

    cnt_llfb += cnt_cur;

    root_ptr += WSIZE;
    class_id++;
  }

  fprintf(stderr, "[mm_check] end loop linked lists\n");

  assert(root_ptr == heap_start); /* loop ends well */

  assert(cnt_freed == cnt_llfb);  /* every freed block is in the linked lists */

  fprintf(stderr, "[mm_check] done\n");

#endif  /* __MM_YGJ_DEBUG__ */

  return 0;
}


#ifdef __MM_YGJ_DEBUG__

/* wrapper call id */
uint _dbg_call_id = 0;


/*
 * mm_init - Wrapper function for mm_init
 */
int mm_init(void)
{
  uint call_id = _dbg_call_id++;

  fprintf(stderr, "[%4u] mm_init() called\n", call_id);

  int retval = dbg_init();

  fprintf(stderr, "[%4u] mm_init() == %d returned\n", call_id, retval);

  mm_check();

  return retval;
}


/*
 * mm_malloc - Wrapper function for mm_malloc
 */
void* mm_malloc(size_t size)
{
  uint call_id = _dbg_call_id++;

  fprintf(stderr, "[%4u] mm_malloc(size=%u) called\n", call_id, size);

  void *retval = dbg_malloc(size);

  fprintf(stderr, "[%4u] mm_malloc(size=%u) == %p returned\n", call_id, size, retval);

  mm_check();

  return retval;
}


/*
 * mm_free - Wrapper function for mm_free
 */
void mm_free(void *ptr)
{
  uint call_id = _dbg_call_id++;

  fprintf(stderr, "[%4u] mm_free(ptr=%p) called\n", call_id, ptr);

  dbg_free(ptr);

  fprintf(stderr, "[%4u] mm_free(ptr=%p) returned\n", call_id, ptr);

  mm_check();
}


/*
 * mm_realloc - Wrapper function for mm_realloc
 */
void* mm_realloc(void *ptr, size_t size)
{
  uint call_id = _dbg_call_id++;

  fprintf(stderr, "[%4u] mm_realloc(ptr=%p, size=%u) called\n", call_id, ptr, size);

  void *retval = dbg_realloc(ptr, size);

  fprintf(stderr, "[%4u] mm_realloc(ptr=%p, size=%u) == %p returned\n", call_id, ptr, size, retval);

  mm_check();

  return retval;
}

#endif  /* __MM_YGJ_DEBUG__ */
