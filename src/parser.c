#define __FUNLANG_COMMON_H_IMPL
#include "parser.h"
#include "common.h"
#include "hashtable.h"
#include "lexer.h"
#include <err.h>
#include <stddef.h>
#include <stdio.h>

typedef struct
{
  DynamicArray funcs;

  DynamicArray iargs;
  DynamicArray eargs;

  DynamicArray types;

  DynamicArray stmts;
  DynamicArray exprs;
} ParseBuf;

static StrView insert_intern(HSet *hs, char *intrn, Intern i)
{
  return insert(hs, i.idx + intrn, i.len >> 8);
}

Token expect_token(LexRes *lr, TokTag tag)
{
  Token tok = *lr->tokens++;

  // TODO: proper error handling
  assert((tok.tag & 0xff) == tag);
  return tok;
}

bool opt_munch_token(LexRes *lr, TokTag tag)
{
  Token tok = *lr->tokens;
  bool matched = (tok.tag & 0xff) == tag;

  if (matched)
    lr->tokens++;

  return matched;
}

static void parse_type(ParseBuf *buf, LexRes *lr, HSet *hs)
{
  // TODO: more complex types
  Token tok = expect_token(lr, TOK_TYPE_ID);
  ParsedType type = {};
  type.kind = NAMED;
  type.as_named = insert_intern(hs, lr->intern, tok.as_typ_ident);
  push_elem(&buf->types, sizeof(ParsedType), &type);
}

static uint32_t parse_implicit_args(ParseBuf *, LexRes *, HSet *)
{
  err(1, "TODO: "
         "parse implicit arguments");
}

static uint32_t parse_explicit_args(ParseBuf *buf, LexRes *lr, HSet *hs)
{
  uint32_t len = 0;
  Token intro = expect_token(lr, '(');
  ptrdiff_t matching = intro.matching_scp >> 8;
  Token *end = lr->tokens + matching;

  while (++lr->tokens < end)
  {
    ExplicitArg earg = {};

    Token name = expect_token(lr, TOK_VAL_ID);
    earg.name = insert_intern(hs, lr->intern, name.as_val_ident);

    expect_token(lr, ':');

    earg.type = buf->types.len;
    parse_type(buf, lr, hs);

    opt_munch_token(lr, ',');

    push_elem(&buf->eargs, sizeof(earg), &earg);
    len++;
  }

  return len;
}

static void parse_expr(ParseBuf *buf, LexRes *lr, HSet *hs, TokTag tag)
{
  (void)tag;
  (void)hs;
  (void)buf;
  // TODO: other kinds of expressions
  Expression expr = {};
  expr.kind = LIT_INT;
  Token lit = expect_token(lr, TOK_LIT_INT);
  expr.as_lit_int = lr->lits[lit.as_lit_idx >> 8];
  push_elem(&buf->exprs, sizeof(expr), &expr);
}

static void parse_stmt(ParseBuf *buf, LexRes *lr, HSet *hs)
{
  if (lr->tokens + 1 > lr->tkeptr)
    err(1, "encountered unexpected "
           "eof while parsing statement");

  Token *start = lr->tokens++;
  switch (start->tag & 0xff)
  {
  case TOK_KW_RETRN:
    parse_expr(buf, lr, hs, ';');
    break;

    // TODO: case TOK_KW_LET:

  default:
    err(1,
        "encountered unexpected token"
        " while parsing satement: 0x%x",
        (start->tag & 0xff));
  }

  expect_token(lr, ';');
}

static uint32_t parse_block(ParseBuf *buf, LexRes *lr, HSet *hs)
{
  uint32_t len = 0;
  Token intro = expect_token(lr, '{');
  ptrdiff_t matching = intro.matching_scp >> 8;
  Token *end = lr->tokens + matching;

  while (lr->tokens < end && !opt_munch_token(lr, '}'))
  {
    parse_stmt(buf, lr, hs);

    len++;
  }

  return len;
}

static void parse_fn(ParseBuf *buf, LexRes *lr, HSet *hs)
{
  FunctionDef fn = {};

  Token name_tok = expect_token(lr, TOK_VAL_ID);
  fn.name = insert_intern(hs, lr->intern, name_tok.as_val_ident);

  if (lr->tokens + 1 > lr->tkeptr)
    err(1, "encountered unexpected eof while parsing function");

  Token first_arglist_start = *lr->tokens;
  if ((first_arglist_start.tag & 0xff) == '[')
  {
    fn.ea_beg = buf->eargs.len;
    fn.ea_len = parse_implicit_args(buf, lr, hs);
  }

  fn.ia_beg = buf->iargs.len;
  fn.ia_len = parse_explicit_args(buf, lr, hs);

  if (lr->tokens + 1 > lr->tkeptr)
    err(1, "encountered unexpected eof while parsing function");

  Token arr_or_bl = *(lr->tokens++);

  if ((arr_or_bl.tag & 0xff) == TOK_KW_ARROW)
  {
    fn.type = buf->types.len;
    parse_type(buf, lr, hs);
  }

  fn.bl_beg = buf->stmts.len;
  fn.bl_len = parse_block(buf, lr, hs);

  push_elem(&buf->funcs, sizeof(fn), &fn);
}

[[nodiscard("has to be freed")]] ParseBuf parse(LexRes lr)
{
  ParseBuf buf = {};
  // TODO: hacky
  push_elem(&buf.funcs, sizeof(ExplicitArg), &(FunctionDef){});
  push_elem(&buf.iargs, sizeof(ExplicitArg), &(ImplicitArg){});
  push_elem(&buf.eargs, sizeof(ExplicitArg), &(ExplicitArg){});
  push_elem(&buf.types, sizeof(ExplicitArg), &(ParsedType){});
  push_elem(&buf.stmts, sizeof(ExplicitArg), &(Statement){});
  push_elem(&buf.exprs, sizeof(ExplicitArg), &(Expression){});

  HSet hs = {};

  while (lr.tokens < lr.tkeptr)
  {
    switch ((lr.tokens++)->tag & 0xff)
    {
    case TOK_KW_FN:
      parse_fn(&buf, &lr, &hs);
      break;

    default:
      err(1, "unhandled token in top position: 0x%x", lr.tokens->tag & 0xff);
    }
  }

  return buf;
}

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
  printf("found %lu tokens\n", lr.tkeptr - lr.tokens);
  for (Token *tok = lr.tokens; tok < lr.tkeptr; ++tok)
  {
    printf("0x%x ", tok->tag & 0xff);
  }
  printf("\n");
  ParseBuf parseres = parse(lr);
  destroy_lexres(lr);

  free(parseres.funcs.buffer);
  free(parseres.eargs.buffer);
  free(parseres.iargs.buffer);
  free(parseres.types.buffer);
  free(parseres.stmts.buffer);
  free(parseres.exprs.buffer);

  free(string);
  return 0;
}
