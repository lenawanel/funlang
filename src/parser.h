#include "common.h"
#include "lexer.h"
#include "hashtable.h"

typedef struct
{
  StrView name;
  uint32_t ia_beg, ia_len;
  uint32_t ea_beg, ea_len;
  uint32_t type;
  uint32_t bl_beg, bl_len;
} FunctionDef;

typedef struct
{
  StrView name;
  uint32_t type;
} ExplicitArg;

typedef struct
{
  StrView tname; /* TODO */
  uint32_t rhs;
} ImplicitArg;

// a parsed type, this isn't name resolved etc.
typedef struct
{
  enum
  {
    NAMED,
    INBUILT,
  } kind;

  union
  {
    StrView as_named;
    enum
    {
      U8  = TOK_KW_U8,  U16 = TOK_KW_U16,
      U32 = TOK_KW_U32, U64 = TOK_KW_U64,
      S8  = TOK_KW_S8,  S16 = TOK_KW_S16,
      S32 = TOK_KW_S32, S64 = TOK_KW_S64,
    } as_inbuilt;
  };
} ParsedType;

typedef struct
{
  enum
  {
    RETURN,
    LET_BIND,
  } kind;

  union
  {
    uint32_t as_return;
    struct
    {
      StrView name;
      uint32_t type;
      uint32_t expr;
    } as_let_bind;
  };
} Statement;

typedef struct
{
  uint32_t rhs;
  uint32_t lhs;
  char op;
} BinExpr;

typedef struct
{
  uint32_t operand;
  char op;
} UnaExpr;

typedef struct
{
  enum
  {
    LIT_INT,
    BIND_USE,
    APPLICATION,
    BINARY_EXPR,
    UNARY_EXPR,
  } kind;

  union
  {
    uint64_t as_lit_int;
    StrView as_bind_use;
    BinExpr as_bin_expr;
    UnaExpr as_una_expr;
  };
} Expression;

typedef struct
{
  FunctionDef *funcs;

  ImplicitArg *iargs;
  ExplicitArg *eargs;

  ParsedType *types;

  Statement *stmts;
  Expression *exprs;

  uint32_t funcs_len, iargs_len, eargs_len, types_len, stmts_len, exprs_len;

  HSet strings;
} ParseRes;
