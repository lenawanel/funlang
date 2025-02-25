#include "arena.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void grow(struct Arena *arena, size_t size)
{
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
  // FIXED memory bc I don't want to deal with indices
  size_t align_mask = (size_t)sysconf(_SC_PAGE_SIZE) - 1;
  flags |= arena->beg ? 0 : MAP_FIXED;
  size_t arena_size = (size_t)(arena->end - arena->beg);

  size_t to_alloc = (size + arena_size + align_mask) & ~align_mask;

  // TODO: mremap? idk if allocating over previos allocations
  //               is valid
  arena->beg = mmap(arena->cur, to_alloc, PROT_READ | PROT_WRITE, flags, -1, 0);
  arena->cur = arena->cur ? arena->cur : arena->beg;
}

// algin has to be a power of 2
void *arena_alloc(struct Arena *arena, size_t size, size_t align)
{
  align--;
  size_t aligned_sz = (size + align) & ~align;
  size_t remaining_space = (size_t)(arena->end - arena->cur);

  if (remaining_space < size)
    grow(arena, aligned_sz);

  // FIX: this may be buggy
  size_t correction = (size_t)arena->cur & align;
  correction = -correction & align;
  void *res = arena->cur + correction;

  arena->cur += aligned_sz;
  return res;
}

void dealloc(struct Arena *arena)
{
  munmap(arena->beg, (size_t)(arena->beg - arena->end));
  memset(arena, 0, sizeof(struct Arena));
}
