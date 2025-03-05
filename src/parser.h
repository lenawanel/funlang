#ifndef _PARSER_H_
#define _PARSER_H_
#include "hashtable.h"
#include "lexer.h"

typedef enum
{
  INVALID,

  FUN_INT,   // fn
  FUN_ARROW, // ->
  FUN_BLOCK, // {
  FUN_END,   // }

  EXP_ARGLIST_BEG, // (
  EXP_ARGLIST_END, // )
  EXP_ARGLIST_SEP, // ,

  BIND_NAME,
  BIND_TY_JUDGE, // :
  BIND_TY_SUBTY, // <:
  BIND_USE,
  BIND_TY_USE,

  LITERAL_INT,

  PREFIX_MINUS, // -

  // TODO: maybe have one tag for infix and include the kind info
  //       in the associated union
  INFIX_MINUS, // -
  INFIX_PLUS,  // +

  STMT_RETURN,   // return
  STMT_LET_BIND, // let
  STMT_ASSGN_EQ, // =
  STMT_SEMI,     // ;
  BUILTIN_TY,
} PNodeKind;

typedef struct
{
  PNodeKind kind;
  union
  {
    uint32_t subtree_sz;
    uint64_t literal_int;
    StrView str;
  };
  uint32_t pos;
} PNode;

typedef struct
{
  PNode *tree;
  uintptr_t size;

  HSet names;
} ParseRes;

[[nodiscard]] ParseRes parse(LexRes lr);

void print_pnode(PNode n);

#endif // _PARSER_H_
