#include <stdint.h>

typedef struct Lexer {
  char* source;
  uint32_t pos;
  uint32_t end;
} Lexer;

typedef struct Intern {
  // TODO: think of a distribution of bits
   uint16_t len;
   uint16_t idx;
 } Intern;

typedef  enum {
  INVALID  = 0x0,

  LIT_INT  = 0x2,
  LIT_STR  = 0x3,
  VAL_ID   = 0x8,
  TYPE_ID  = 0x9,

  HYPHON   = '-',
  PAREN_O  = '(',
  PAREN_C  = ')',
  COLON    = ':',
  SEMI     = ';',
  BRACK_O  = '[',
  BRACK_C  = ']',

  BRACE_O  = '{',
  PIPE     = '|',
  BRACE_C  = '}',
  
  KEYOWRD_ = 0x80, // a keyword is any kind special
                   // sequence of non whitespace characters
                   // in this source a token tag being a keyword
                   // is indicatated by the corrresponding tag
                   // representation having its highest bit set
  KW_FN    = 0x81,
  KW_AS    = 0x82,
  KW_RET   = 0x83,
  KW_ARROW = 0x84,
} TokTag;

typedef struct Token {
  uint32_t pos;
  union { // lower 8 bits determine tag of the enum
    TokTag tag;
    uint32_t as_lit_idx;
    uint32_t matching_scp;
    Intern   as_str_lit;
    Intern as_typ_ident;
    Intern as_val_ident;
  };
} __attribute__((aligned(8))) Token;

typedef struct {
  char*    intern;
  Token*   tokens;
  uint64_t*  lits;
  uint32_t tok_num;
} LexRes;

void destroy_lexres(LexRes lex_res);
[[nodiscard]] LexRes lex(Lexer l);
 
