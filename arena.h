#include <stdalign.h>
#include <stddef.h>

struct Arena
{
  void *cur;
  void *beg;
  void *end;
};

#define ARENA_ALLOC(ar, type) arena_alloc(ar, sizeof(type), alignof(type))

// algin has to be a power of 2
void *arena_alloc(struct Arena *arena, size_t size, size_t align);

void dealloc(struct Arena *arena);
