#include "typer.h"
#include "parser.h"
#include <stdio.h>
#define __FUNLANG_COMMON_H_IMPL
#include "common.h"

int main(int argc, char **argv)
{
  assert(argc > 1);

  FILE *f = fopen(argv[1], "rb");
  (void)fseek(f, 0, SEEK_END);
  size_t fsize = (size_t)ftell(f);
  (void)fseek(f, 0, SEEK_SET); /* same as rewind(f); */

  char *string = malloc(fsize + 1);
  (void)fread(string, fsize, 1, f);
  (void)fclose(f);

  string[fsize] = 0;

  Lexer l = {.src = string, .cur = string, .end = string + fsize};
  LexRes lr = lex(l);

  free(string);

  printf("found %lu tokens\n", lr.tkeptr - lr.tokens);
  for (Token *tok = lr.tokens; tok < lr.tkeptr; ++tok)
  {
    printf("0x%x ", tok->tag & 0xff);
  }
  printf("\n");
  ParseRes parseres = parse(lr);
  destroy_lexres(lr);

  for (uintptr_t i = 0; i< parseres.size; ++i)
  {
    print_pnode(parseres.tree[i]);
    (void)putchar(0xa);
  }

  free(parseres.tree);
  free_hset(parseres.names);

  return 0;
}
