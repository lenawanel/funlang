#define __FUNLANG_COMMON_H_IMPL
#include "parser.h"
#include "common.h"
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

  printf("0x%02x == 0x%02x\n", tok.tag & 0xff, tag);
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

bool is_at(LexRes *lr, TokTag tag)
{
  Token tok = *lr->tokens;
  return (tok.tag & 0xff) == tag;
}

static void parse_type(ParseBuf *buf, LexRes *lr, HSet *hs)
{
  // TODO: more complex types
  Token tok = *lr->tokens++;

  ParsedType type = {};
  switch (tok.tag & 0xff)
  {
  case TOK_TYPE_ID:
    type.kind = NAMED;
    type.as_named = insert_intern(hs, lr->intern, tok.as_typ_ident);
    break;

  default:
    type.kind = INBUILT;
    type.as_inbuilt = tok.tag & 0xff;
  }

  push_elem(&buf->types, sizeof(type), &type);
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

  while (lr->tokens < end &&
         !opt_munch_token(lr, ')')) // TODO: fix lexer matching scope
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

struct
{
  uint8_t l, r;
} infix_binding_power(char op)
{
  uint8_t l[255] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 1};

  uint8_t r[255] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 2};

  uint8_t op_idx = (uint8_t)op;

  return (typeof(infix_binding_power(0))){.l = l[op_idx], .r = r[op_idx]};
}

uint8_t prefix_binding_power(char op)
{
  uint8_t p[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5};

  return p[(uint8_t)op];
}

[[nodiscard]] static uint32_t parse_expr(ParseBuf *buf, LexRes *lr, HSet *hs,
                                         TokTag delim, uint8_t min_bp)
{
  uint32_t lhs;
  // TODO: other kinds of expressions
  Expression expr = {};
  Token first_tok = *lr->tokens++;
  switch (first_tok.tag & 0xff)
  {
  case TOK_LIT_INT:
    expr.kind = LIT_INT;
    expr.as_lit_int = lr->lits[first_tok.as_lit_idx >> 8];

    lhs = push_elem(&buf->exprs, sizeof(expr), &expr);
    break;

  case TOK_VAL_ID:
    expr.kind = BIND_USE;
    expr.as_bind_use = insert_intern(hs, lr->intern, first_tok.as_val_ident);

    lhs = push_elem(&buf->exprs, sizeof(expr), &expr);
    break;

  case TOK_PAREN_O:
    lhs = parse_expr(buf, lr, hs, delim, 0);
    expect_token(lr, ')');
    break;

  default: // TODO: actually validat that this is a valid prefix op
    char op = (char)(first_tok.tag & 0xff);
    expr.kind = UNARY_EXPR;
    expr.as_una_expr.op = op;
    uint8_t r_bp = prefix_binding_power(op);

    assert(r_bp && "encountered unexpected token"
                   " while parsing an expression");

    expr.as_una_expr.operand = parse_expr(buf, lr, hs, delim, r_bp);

    lhs = push_elem(&buf->exprs, sizeof(expr), &expr);
  }

  while (lr->tokens < lr->tkeptr && !is_at(lr, delim))
  {
    char op = (char)(lr->tokens->tag & 0xff);
    expr.kind = BINARY_EXPR;
    expr.as_bin_expr.op = op;
    expr.as_bin_expr.lhs = lhs;

    typeof(infix_binding_power(op)) bp = infix_binding_power(op);
    if (bp.l < min_bp)
      break;

    lr->tokens++;
    expr.as_bin_expr.rhs = parse_expr(buf, lr, hs, delim, bp.r);
    lhs = push_elem(&buf->exprs, sizeof(expr), &expr);
  }

  return lhs;
}

static void parse_stmt(ParseBuf *buf, LexRes *lr, HSet *hs)
{
  if (lr->tokens + 1 > lr->tkeptr)
    err(1, "encountered unexpected "
           "eof while parsing statement");

  Token *start = lr->tokens++;
  Statement s = {};

  switch (start->tag & 0xff)
  {
  case TOK_KW_RETRN:
    s.kind = RETURN;
    s.as_return = parse_expr(buf, lr, hs, ';', 0);
    break;

  case TOK_KW_LET:
    s.kind = LET_BIND;
    Token name = expect_token(lr, TOK_VAL_ID);
    s.as_let_bind.name = insert_intern(hs, lr->intern, name.as_val_ident);

    if (opt_munch_token(lr, ':'))
    {
      s.as_let_bind.type = buf->exprs.len;
      parse_type(buf, lr, hs);
    }
    expect_token(lr, '=');
    s.as_let_bind.expr = parse_expr(buf, lr, hs, ';', 0);
    break;

  default:
    err(1,
        "encountered unexpected token"
        " while parsing satement: 0x%x",
        (start->tag & 0xff));
  }

  push_elem(&buf->stmts, sizeof(s), &s);

  expect_token(lr, ';');
}

static uint32_t parse_block(ParseBuf *buf, LexRes *lr, HSet *hs)
{
  uint32_t len = 0;
  Token intro = expect_token(lr, '{');
  ptrdiff_t matching = intro.matching_scp >> 8;
  Token *end = lr->tokens + matching;

  while (lr->tokens < end &&
         !opt_munch_token(lr, '}')) // TODO: optional munch shouldn't be needed
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

[[nodiscard("has to be freed")]] ParseRes parse(LexRes lr)
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

  ParseRes res = {
      .funcs = buf.funcs.buffer,
      .funcs_len = buf.funcs.len,

      .iargs = buf.iargs.buffer,
      .iargs_len = buf.eargs.len,

      .eargs = buf.eargs.buffer,
      .eargs_len = buf.eargs.len,

      .types = buf.types.buffer,
      .types_len = buf.types.len,

      .stmts = buf.stmts.buffer,
      .stmts_len = buf.stmts.len,

      .exprs = buf.exprs.buffer,
      .exprs_len = buf.exprs.len,

      .strings = hs,
  };

  return res;
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

  free(string);

  printf("found %lu tokens\n", lr.tkeptr - lr.tokens);
  for (Token *tok = lr.tokens; tok < lr.tkeptr; ++tok)
  {
    printf("0x%x ", tok->tag & 0xff);
  }
  printf("\n");
  ParseRes parseres = parse(lr);
  destroy_lexres(lr);

  free(parseres.funcs);
  free(parseres.eargs);
  free(parseres.iargs);
  free(parseres.types);
  free(parseres.stmts);
  free(parseres.exprs);

  free_hset(parseres.strings);

  return 0;
}
