#ifndef _TYPES_H
#define _TYPES_H
#include "lexer.h"

typedef enum
{
  U8  = TOK_KW_U8,  U16 = TOK_KW_U16,
  U32 = TOK_KW_U32, U64 = TOK_KW_U64,
  S8  = TOK_KW_S8,  S16 = TOK_KW_S16,
  S32 = TOK_KW_S32, S64 = TOK_KW_S64,
} InbuiltType;




#endif // _TYPES_H
