#ifndef _LEXER_H
#define _LEXER_H

#include <stdint.h>

typedef struct Lexer
{
  char *src;
  char *cur;
  char *end;
} Lexer;

typedef struct Intern
{
  // TODO: think of a distribution of bits
  uint16_t len;
  uint16_t idx;
} Intern;

typedef enum
{
  TOK_INVALID  = 0x0,

  TOK_LIT_INT  = 0x2,
  TOK_LIT_STR  = 0x3,
  TOK_VAL_ID   = 0x8,
  TOK_TYPE_ID  = 0x9,

  TOK_PAREN_O  = '(',
  TOK_PAREN_C  = ')',
  TOK_PLUS     = '+',
  TOK_HYPHON   = '-',
  TOK_COLON    = ':',
  TOK_SEMI     = ';',
  TOK_BRACK_O  = '[',
  TOK_BRACK_C  = ']',

  TOK_BRACE_O  = '{',
  TOK_PIPE     = '|',
  TOK_BRACE_C  = '}',

  TOK_KEYOWRD_ = 0x80, // a keyword is any kind special
                       // sequence of non whitespace characters
                       // in this source a token tag being a keyword
                       // is indicatated by the corrresponding tag
                       // representation having its highest bit set

  TOK_KW_FN    = 0x81,
  TOK_KW_U8    = 0x82,
  TOK_KW_S8    = 0x83,

  TOK_KW_ARROW = 0x86,
  TOK_KW_SUBTY = 0x87,
  TOK_KW_SHIFR = 0x88,
  TOK_KW_SHIFL = 0x89,

  // three letter kws
  TOK_KW_ASS   = 0x90,
  TOK_KW_ASU   = 0x91,
  TOK_KW_LET   = 0x92,
  TOK_KW_U16   = 0x93,
  TOK_KW_U32   = 0x94,
  TOK_KW_U64   = 0x95,
  TOK_KW_S16   = 0x96,
  TOK_KW_S32   = 0x97,
  TOK_KW_S64   = 0x98,
  // 4 letter kws
  TOK_KW_HOLE  = 0xa0,
  // 5 letter kws
  TOK_KW_RETRN = 0xb0,
} TokTag;

typedef struct Token
{
  uint32_t pos;
  union
  { // lower 8 bits determine tag of the enum
    TokTag tag;
    uint32_t as_lit_idx;
    int32_t matching_scp;
    Intern as_intern;
  };
} __attribute__((aligned(8))) Token;

typedef struct
{
  char *intern;
  Token *tokens;
  uint64_t *lits;
  Token *tkeptr;
} LexRes;

void destroy_lexres(LexRes lex_res);
[[nodiscard]] LexRes lex(Lexer l);

#endif // _LEXER_H
