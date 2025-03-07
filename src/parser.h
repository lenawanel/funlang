#ifndef _PARSER_H_
#define _PARSER_H_
#include "hashtable.h"
#include "lexer.h"

#define PNodeKindMacro(X1, X2, X3)                                             \
  X1(INVALID, INVALID)                                                         \
                                                                               \
  X1(FUN_INT, "fn")                                                            \
  X2(FUN_ARROW, "->", "%d", subtree_sz)                                        \
  X1(FUN_BLOCK, "{")                                                           \
  X2(FUN_END, "}", "%d", subtree_sz)                                           \
                                                                               \
  X1(EXP_ARGLIST_BEG, "(")                                                     \
  X1(EXP_ARGLIST_SEP, ",")                                                     \
  X2(EXP_ARGLIST_END, ")", "%d", subtree_sz)                                   \
                                                                               \
  X1(IMP_ARGLIST_BEG, "[")                                                     \
  X1(IMP_ARGLIST_SEP, ",")                                                     \
  X2(IMP_ARGLIST_END, "]", "%d", subtree_sz)                                   \
                                                                               \
  X3(BIND_NAME, "name", "%.*s", str.len, str.txt)                              \
  X3(BIND_TY_NAME, "Name", "%.*s", str.len, str.txt)                           \
  X2(BIND_TY_JUDGE, ":", "%d", subtree_sz)                                     \
  X2(BIND_TY_SUBTY, "<:", "%d", subtree_sz)                                    \
  X3(BIND_USE, "name", "%.*s", str.len, str.txt)                               \
  X3(BIND_TY_USE, "Name", "%.*s", str.len, str.txt)                            \
                                                                               \
  X2(LITERAL_INT, "1234", "%d", literal_int)                                   \
                                                                               \
  X2(PREFIX_MINUS, "-", "%d", subtree_sz)                                      \
                                                                               \
  X2(INFIX_MINUS, "-", "%d", subtree_sz)                                       \
  X2(INFIX_PLUS, "+", "%d", subtree_sz)                                        \
                                                                               \
  X1(STMT_RETURN, "return")                                                    \
  X1(STMT_LET_BIND, "let")                                                     \
  X1(STMT_ASSGN_EQ, "=")                                                       \
  X2(STMT_SEMI, ";", "%d", subtree_sz)                                         \
  X1(BUILTIN_TY, "u32, ...")

#define PNodeKindMacroDeclare(V, ...) V,

typedef enum
{
  PNodeKindMacro(PNodeKindMacroDeclare, PNodeKindMacroDeclare, PNodeKindMacroDeclare)
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
