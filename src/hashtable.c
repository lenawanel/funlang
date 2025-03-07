#include "hashtable.h"
#include <stdio.h>
#include <sys/mman.h>

static uint32_t fnv_32_str(StrView str)
{
  uint8_t *bp = (uint8_t *)str.txt; /* start of buffer */
  uint8_t *be = bp + str.len;       /* beyond end of buffer */

  // TODO: param?
  uint32_t hval = 0;
  while (bp < be)
  {
    /* multiply by the 32 bit FNV magic prime mod 2^32 */
    hval *= 0x01000193ul; // fnv 32 bit prime

    /* xor the bottom with the current octet */
    hval ^= (uint32_t)*bp++;
  }

  /* return our new hash value */
  return hval;
}

static void insert_unique_in_cap(SetEntry *restrict ens, StrView str,
                                 uint32_t cap_mask)
{

  uint32_t idx = fnv_32_str(str) & cap_mask;

  while (ens[idx].skey.txt)
    idx = (idx + 1) & cap_mask;

  ens[idx].skey = str;
}

static void grow(HSet *restrict hs)
{
  uint32_t new_cap = hs->encap ? hs->encap << 1 : 0x1000;
  SetEntry *nentrs =
      mmap(NULL, new_cap * sizeof(SetEntry), PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  assert((uintptr_t)nentrs != ~0ull && "failed to mmap HSet");

  SetEntry *ben = hs->entrs;
  if (!ben) goto end; // gurad against some weird ub
  SetEntry *een = ben + hs->encap;

  uint32_t mask = new_cap - 1;

  while (ben++ < een)
  {
    if (!ben->skey.txt) continue;

    insert_unique_in_cap(nentrs, ben->skey, mask);
  }

end:
  munmap(hs->entrs, hs->encap);
  hs->entrs = nentrs;
  hs->encap = new_cap;
}

StrView insert_str(HSet *restrict hs, StrView str)
{
  // TODO: idk if this is a good idea,
  //       I'm mostly using the golden ratio
  //       for the memes
  if ((float)hs->inuse * PHI >= (float)hs->encap) grow(hs);

  uint32_t cap_mask = hs->encap - 1;
  uint32_t idx      = fnv_32_str(str) & cap_mask;

  for (SetEntry ent = hs->entrs[idx]; ent.skey.txt; idx = (idx + 1) & cap_mask)
  {
    if (ent.skey.len == str.len &&
        !memcmp(ent.skey.txt, str.txt, MIN(ent.skey.len, str.len)))
      return ent.skey;
  }

  str.txt = co_append(&hs->intrn, str.txt, str.len);

  hs->entrs[idx].skey = str;

  hs->inuse++;

  return str;
}

bool remove_str(HSet *restrict hs, StrView str)
{

  uint32_t cap_mask = hs->encap - 1;
  uint32_t idx      = fnv_32_str(str) & cap_mask;

  SetEntry ent;
  bool there = false;

  while ((ent = hs->entrs[idx]).skey.txt)
  {
    if (!memcmp(ent.skey.txt, str.txt, MIN(ent.skey.len, str.len)))
    {
      there               = true;
      hs->entrs[idx].skey = (StrView){};
      break;
    }

    idx = (idx + 1) & cap_mask;
  }

  hs->inuse--;

  return there;
}

StrView insert(HSet *restrict hs, char *to_insert, uint32_t len)
{
  return insert_str(hs, (StrView){.len = len, .txt = to_insert});
}

void free_hset(HSet hs)
{
  munmap(hs.entrs, hs.encap);
  free(hs.intrn.buffer);
}
