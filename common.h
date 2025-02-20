#ifndef __FUNLANG_COMMON_H_
#define __FUNLANG_COMMON_H_

#include <assert.h>
#include <stdbit.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
  char *source;
  uint32_t len;
} StrView;

typedef struct
{
  void *buffer;
  uint32_t len;
  uint32_t cap;
} DynamicArray;

void grow_array(DynamicArray *array, size_t elem_bytes);

uint32_t push_elem(register DynamicArray *restrict array,
                   register size_t elem_bytes, register void *elem);

void pop_elem(register DynamicArray *restrict array, register size_t elem_bytes,
              register void *elem);

#ifdef __FUNLANG_COMMON_H_IMPL

static uint32_t next_pow2(uint32_t n)
{
  uint32_t leading_zeros = stdc_leading_zeros_ui(n);
  uint32_t shamt = 32 - leading_zeros;

  return 1 << shamt;
}

// TODO: very questionable approach to polymorphism
void grow_array(register DynamicArray *restrict array,
                register size_t elem_size)
{
  uint32_t new_cap = next_pow2(array->len);

  void *new_buf = realloc(array->buffer, elem_size * new_cap);

  // TODO: actual error handling
  assert(new_buf && "failed to reallocate new_buf");

  array->buffer = new_buf;
  array->cap = new_cap;
}

uint32_t push_elem(register DynamicArray *restrict array,
                   register size_t elem_size, register void *elem)
{
  if (++array->len > array->cap)
    grow_array(array, elem_size);

  memcpy(array->buffer + elem_size * (array->len - 1), elem, elem_size);

  return array->len - 1;
}

void pop_elem(register DynamicArray *restrict array, register size_t elem_size,
              register void *elem)
{
  assert(array->len);

  memcpy(elem, array->buffer + elem_size * --array->len, elem_size);
}

#endif // __FUNLANG_COMMON_H_IMPL

#endif // __FUNLANG_COMMON_H_
