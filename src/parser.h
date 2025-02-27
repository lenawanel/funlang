#include "common.h"
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

typedef struct {
  uint32_t rhs;
  uint32_t lhs;
  char op;
} BinExpr; 

typedef struct
{
  enum
  {
    LIT_INT,
    BIND_USE,
    APPLICATION,
    BINARY_EXPR,
  } kind;

  union
  {
    uint64_t as_lit_int;
    StrView as_bind_use;
    BinExpr as_bin_expr;
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
