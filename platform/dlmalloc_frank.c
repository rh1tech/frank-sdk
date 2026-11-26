/*
 * frank-sdk — Doug Lea's malloc (public domain), built as the two mspaces of
 * heap.c: no global malloc, no mmap, no sbrk, no locks (heap.c holds a
 * spinlock around every call).
 */
#define ONLY_MSPACES 1
#define HAVE_MMAP 0
#define HAVE_MORECORE 0
#define NO_MALLOC_STATS 1
#define USE_LOCKS 0
#define ABORT_ON_ASSERT_FAILURE 0
#define MALLOC_FAILURE_ACTION
#define LACKS_SYS_MMAN_H 1
#define LACKS_UNISTD_H 1
#define LACKS_SYS_PARAM_H 1
#include "dlmalloc.c"
