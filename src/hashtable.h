#ifndef _HASHTABLE_H
#define _HASHTABLE_H

#include "common.h"

typedef struct
{
  StrView skey;
} SetEntry;

// TODO: remove()
typedef struct
{
  SetEntry *entrs;
  uint32_t inuse, encap;
  DynamicArray intrn;
} HSet;

StrView insert_str(HSet *restrict hs, StrView str);
StrView insert(HSet *restrict hs, char *text, uint32_t len);
bool remove_str(HSet *restrict hs, StrView str);
void free_hset(HSet hs);

#endif // _HASHTABLE_H
