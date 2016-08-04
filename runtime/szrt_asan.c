//===- subzero/runtime/szrt_asan.c - AddressSanitizer Runtime -----*- C -*-===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Provides the AddressSanitizer runtime.
///
/// Exposes functions for initializing the shadow memory region and managing it
/// on loads, stores, and allocations.
///
//===----------------------------------------------------------------------===//

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define RZ_SIZE (32)
#define SHADOW_SCALE_LOG2 (3)
#define SHADOW_SCALE ((size_t)1 << SHADOW_SCALE_LOG2)
#define DEBUG (0)

// Assuming 48 bit address space on 64 bit systems
#define SHADOW_LENGTH_64 (1u << (48 - SHADOW_SCALE_LOG2))
#define SHADOW_LENGTH_32 (1u << (32 - SHADOW_SCALE_LOG2))
#define WORD_SIZE (sizeof(uint32_t))
#define IS_32_BIT (sizeof(void *) == WORD_SIZE)

#define SHADOW_OFFSET(p) ((uintptr_t)(p) % SHADOW_SCALE)
#define IS_SHADOW_ALIGNED(p) (SHADOW_OFFSET(p) == 0)

#define MEM2SHADOW(p) (((uintptr_t)(p) >> SHADOW_SCALE_LOG2) + shadow_offset)
#define SHADOW2MEM(p)                                                          \
  ((uintptr_t)((char *)(p)-shadow_offset) << SHADOW_SCALE_LOG2)

#define STACK_POISON_VAL ((char)-1)
#define HEAP_POISON_VAL ((char)-2)
#define GLOBAL_POISON_VAL ((char)-3)
#define MEMTYPE_INDEX(x) (-1 - (x))
static const char *memtype_names[] = {"stack", "heap", "global"};

#define ACCESS_LOAD (0)
#define ACCESS_STORE (1)
static const char *access_names[] = {"load from", "store to"};

#if DEBUG
#define DUMP(args...)                                                          \
  do {                                                                         \
    printf(args);                                                              \
  } while (false);
#else // !DEBUG
#define DUMP(args...)
#endif // DEBUG

static char *shadow_offset = NULL;

static bool __asan_check(char *, int);
static void __asan_error(char *, int, int);
static void __asan_get_redzones(char *, char **, char **);

void __asan_init(int, void **, int *);
void __asan_check_load(char *, int);
void __asan_check_store(char *, int);
void *__asan_malloc(size_t);
void *__asan_calloc(size_t, size_t);
void *__asan_realloc(char *, size_t);
void __asan_free(char *);
void __asan_poison(char *, int, char);
void __asan_unpoison(char *, int);

static void __asan_error(char *ptr, int size, int access) {
  char *shadow_addr = MEM2SHADOW(ptr);
  char shadow_val = *shadow_addr;
  if (shadow_val > 0)
    shadow_val = *(shadow_addr + 1);
  assert(access == ACCESS_LOAD || access == ACCESS_STORE);
  const char *access_name = access_names[access];
  assert(shadow_val == STACK_POISON_VAL || shadow_val == HEAP_POISON_VAL ||
         shadow_val == GLOBAL_POISON_VAL);
  const char *memtype = memtype_names[MEMTYPE_INDEX(shadow_val)];
  fprintf(stderr, "Illegal %d byte %s %s object at %p\n", size, access_name,
          memtype, ptr);
  abort();
}

// check only the first byte of each word unless strict
static bool __asan_check(char *ptr, int size) {
  assert(size == 1 || size == 2 || size == 4 || size == 8);
  char *shadow_addr = (char *)MEM2SHADOW(ptr);
  char shadow_val = *shadow_addr;
  DUMP("check %d bytes at %p: %p + %d (%d)\n", size, ptr, shadow_addr,
       (uintptr_t)ptr % SHADOW_SCALE, shadow_val);
  if (size == SHADOW_SCALE) {
    return shadow_val == 0;
  }
  return shadow_val == 0 || (char)SHADOW_OFFSET(ptr) + size <= shadow_val;
}

static void __asan_get_redzones(char *ptr, char **left, char **right) {
  char *rz_left = ptr - RZ_SIZE;
  char *rz_right = *(char **)rz_left;
  if (left != NULL)
    *left = rz_left;
  if (right != NULL)
    *right = rz_right;
}

void __asan_check_load(char *ptr, int size) {
  // aligned single word accesses may be widened single byte accesses, but for
  // all else use strict check
  int check_size =
      (size == WORD_SIZE && (uintptr_t)ptr % WORD_SIZE == 0) ? 1 : size;
  if (!__asan_check(ptr, check_size))
    __asan_error(ptr, size, ACCESS_LOAD);
}

void __asan_check_store(char *ptr, int size) {
  // stores may never be partially out of bounds so use strict check
  if (!__asan_check(ptr, size))
    __asan_error(ptr, size, ACCESS_STORE);
}

void __asan_init(int n_rzs, void **rzs, int *rz_sizes) {
  // ensure the redzones are large enough to hold metadata
  assert(RZ_SIZE >= sizeof(void *) && RZ_SIZE >= sizeof(size_t));
  assert(shadow_offset == NULL);
  size_t length = (IS_32_BIT) ? SHADOW_LENGTH_32 : SHADOW_LENGTH_64;
  int prot = PROT_READ | PROT_WRITE;
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  int fd = -1;
  off_t offset = 0;
  shadow_offset = mmap((void *)length, length, prot, flags, fd, offset);
  if (shadow_offset == NULL)
    fprintf(stderr, "unable to allocate shadow memory\n");
  else
    DUMP("set up shadow memory at %p\n", shadow_offset);
  if (mprotect(MEM2SHADOW(shadow_offset), length >> SHADOW_SCALE_LOG2,
               PROT_NONE))
    fprintf(stderr, "could not protect bad region\n");
  else
    DUMP("protected bad region\n");

  // poison global redzones
  DUMP("poisioning %d global redzones\n", n_rzs);
  for (int i = 0; i < n_rzs; i++) {
    DUMP("(%d) poisoning redzone of size %d at %p\n", i, rz_sizes[i], rzs[i]);
    __asan_poison(rzs[i], rz_sizes[i], GLOBAL_POISON_VAL);
  }
}

void *__asan_malloc(size_t size) {
  DUMP("malloc() called with size %d\n", size);
  size_t padding =
      (IS_SHADOW_ALIGNED(size)) ? 0 : SHADOW_SCALE - SHADOW_OFFSET(size);
  size_t rz_left_size = RZ_SIZE;
  size_t rz_right_size = RZ_SIZE + padding;
  void *rz_left;
  int err = posix_memalign(&rz_left, SHADOW_SCALE,
                           rz_left_size + size + rz_right_size);
  if (err != 0) {
    assert(err == ENOMEM);
    return NULL;
  }
  void *ret = rz_left + rz_left_size;
  void *rz_right = ret + size;
  __asan_poison(rz_left, rz_left_size, HEAP_POISON_VAL);
  __asan_poison(rz_right, rz_right_size, HEAP_POISON_VAL);
  // record size and location data so we can find it again
  *(void **)rz_left = rz_right;
  *(size_t *)rz_right = rz_right_size;
  assert((uintptr_t)ret % 8 == 0);
  return ret;
}

void *__asan_calloc(size_t nmemb, size_t size) {
  size_t alloc_size = nmemb * size;
  void *ret = __asan_malloc(alloc_size);
  memset(ret, 0, alloc_size);
  return ret;
}

void *__asan_realloc(char *ptr, size_t size) {
  if (ptr == NULL)
    return __asan_malloc(size);
  if (size == 0) {
    __asan_free(ptr);
    return NULL;
  }
  char *rz_right;
  __asan_get_redzones(ptr, NULL, &rz_right);
  size_t old_size = rz_right - ptr;
  if (size == old_size)
    return ptr;
  char *new_alloc = __asan_malloc(size);
  if (new_alloc == NULL)
    return NULL;
  size_t copyable = (size < old_size) ? size : old_size;
  memcpy(new_alloc, ptr, copyable);
  __asan_free(ptr);
  return new_alloc;
}

void __asan_free(char *ptr) {
  DUMP("free() called on %p\n", ptr);
  char *rz_left, *rz_right;
  __asan_get_redzones(ptr, &rz_left, &rz_right);
  size_t rz_right_size = *(size_t *)rz_right;
  __asan_unpoison(rz_left, RZ_SIZE);
  __asan_unpoison(rz_right, rz_right_size);
  free(rz_left);
}

void __asan_poison(char *ptr, int size, char poison_val) {
  char *end = ptr + size;
  assert(IS_SHADOW_ALIGNED(end));
  // redzones should be no greater than RZ_SIZE + RZ_SIZE-1 for alignment
  assert(size < 2 * RZ_SIZE);
  DUMP("poison %d bytes at %p: %p - %p\n", size, ptr, MEM2SHADOW(ptr),
       MEM2SHADOW(end));
  size_t offset = SHADOW_OFFSET(ptr);
  *(char *)MEM2SHADOW(ptr) = (offset == 0) ? poison_val : offset;
  ptr += SHADOW_OFFSET(size);
  assert(IS_SHADOW_ALIGNED(ptr));
  int len = (end - ptr) >> SHADOW_SCALE_LOG2;
  memset(MEM2SHADOW(ptr), poison_val, len);
}

void __asan_unpoison(char *ptr, int size) {
  char *end = ptr + size;
  assert(IS_SHADOW_ALIGNED(end));
  assert(size < 2 * RZ_SIZE);
  DUMP("unpoison %d bytes at %p: %p - %p\n", size, ptr, MEM2SHADOW(ptr),
       MEM2SHADOW(end));
  *(char *)MEM2SHADOW(ptr) = 0;
  ptr += SHADOW_OFFSET(size);
  assert(IS_SHADOW_ALIGNED(ptr));
  memset(MEM2SHADOW(ptr), 0, (end - ptr) >> SHADOW_SCALE_LOG2);
}
