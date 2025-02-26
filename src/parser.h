#include "common.h"

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
    INBUILT, // TODO: does special casing make sense here?
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

typedef struct
{
  enum
  {
    LIT_INT,
    BIND_USE,
    APPLICATION,
  } kind;

  union
  {
    uint64_t as_lit_int;
    StrView as_bind_use;
  };
} Expression;

