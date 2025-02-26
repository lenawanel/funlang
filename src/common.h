#ifndef __FUNLANG_COMMON_H_
#define __FUNLANG_COMMON_H_

#include <assert.h>
#include <stdbit.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PHI 1.618033988749894848204586834365638118f
#define MIN(a, b) ((a < b) ? a : b)
#define MAX(a, b) ((a > b) ? a : b)

typedef struct
{
  const char *txt;
  uint32_t len;
} StrView;

typedef struct
{
  void *buffer;
  uint32_t len, cap;
} DynamicArray;

void grow_array(DynamicArray *restrict array, size_t elem_bytes);

uint32_t push_elem(DynamicArray *restrict array, size_t elem_bytes,
                   const void *elem);

void pop_elem(DynamicArray *restrict array, size_t elem_bytes, void *elem);

void insert_elem(DynamicArray *restrict array, size_t elem_bytes,
                 const void *elem, uint32_t idx);

void append_buf(DynamicArray *restrict array, size_t elem_bytes,
                const void *buf, size_t len);

#ifdef __FUNLANG_COMMON_H_IMPL

static uint32_t next_pow2(uint32_t n)
{
  uint32_t leading_zeros = stdc_leading_zeros_ui(n);
  uint32_t shamt = 32 - leading_zeros;

  return 1 << shamt;
}

// TODO: very questionable approach to polymorphism
void grow_array(DynamicArray *restrict array, size_t elem_bytes)
{
  uint32_t new_cap = next_pow2(array->len);

  void *new_buf = realloc(array->buffer, elem_bytes * new_cap);

  // TODO: actual error handling
  assert(new_buf && "failed to reallocate new_buf");

  array->buffer = new_buf;
  array->cap = new_cap;
}

uint32_t push_elem(DynamicArray *restrict array, size_t elem_bytes,
                   const void *elem)
{
  if (++array->len > array->cap)
    grow_array(array, elem_bytes);

  memcpy(array->buffer + elem_bytes * (array->len - 1), elem, elem_bytes);

  return array->len - 1;
}

void pop_elem(DynamicArray *restrict array, size_t elem_bytes, void *elem)
{
  assert(array->len);

  memcpy(elem, array->buffer + elem_bytes * --array->len, elem_bytes);
}

void insert_elem(DynamicArray *restrict array, size_t elem_bytes,
                 const void *elem, uint32_t idx)
{
  assert(idx <= array->len && "OOB insert into dyn array");

  if (++array->len > array->cap)
    grow_array(array, elem_bytes);

  void *insert_start = array->buffer + elem_bytes * idx;
  size_t region_size = (array->len - idx) * elem_bytes;

  memmove(insert_start + elem_bytes, insert_start, region_size);

  memcpy(insert_start, elem, elem_bytes);
}

void append_buf(DynamicArray *array, size_t elem_bytes, const void *buf,
                size_t len)
{
  void *start = array->buffer + array->len * elem_bytes;
  array->len += (uint32_t)len;
  if (array->len > array->cap)
    grow_array(array, elem_bytes);

  memcpy(start, buf, len * elem_bytes);
}
#endif // __FUNLANG_COMMON_H_IMPL
#endif // __FUNLANG_COMMON_H_
