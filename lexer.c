#include "lexer.h"
#include "common.h"
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define MAX_INTERN_LEN 0xff
#define MIN(a,b) (((a)<(b))?(a):(b))

typedef struct
{
  char *intern;
  uint32_t intern_len, intern_cap;

  Token *tokens;
  uint32_t tokens_len, tokens_cap;

  uint64_t *lits;
  uint32_t lits_len, lits_cap;
} LexBuf;

static void intern_char(LexBuf *restrict lexbuf, char c)
{
  push_elem((DynamicArray *)&lexbuf->intern, sizeof(char), &c);
}

static uint16_t intern_mark(LexBuf lexbuf)
{
  return (uint16_t)lexbuf.intern_len;
}

static Intern intern_strview(LexBuf *restrict lexbuf, StrView s)
{
  uint32_t len = MIN(s.len, MAX_INTERN_LEN);
  lexbuf->intern_len += len;
  if (lexbuf->intern_len > lexbuf->intern_cap)
    grow_array((DynamicArray *)&lexbuf->intern, sizeof(char));

  Intern i = {.idx = (uint16_t)(lexbuf->intern_len - len),
              .len = (uint16_t)(s.len << 8)};

  memcpy(lexbuf->intern + lexbuf->intern_len - len, s.source,
         s.len * sizeof(char));

  return i;
}

static uint32_t push_lit(LexBuf *restrict lexbuf, uint64_t val)
{
  return push_elem((DynamicArray *)&lexbuf->lits, sizeof(uint64_t), &val);
}

static void push_token(LexBuf *restrict lexbuf, Token tok)
{
  push_elem((DynamicArray *)&lexbuf->tokens, sizeof(Token), &tok);
}

void destroy_lexres(LexRes lex_res)
{
  free(lex_res.intern);
  free(lex_res.tokens);
  free(lex_res.lits);
}

static void consume_ws(Lexer *restrict l)
{
  while (l->pos < l->end && isspace(l->source[l->pos]))
    l->pos++;
}

static void consume_comment(Lexer *restrict l)
{
  if (l->pos + 1 > l->end)
    return;
  if (l->source[l->pos] == '/')
  {
    if (l->source[l->pos + 1] == '/')
    { // single line comment
      while (l->pos < l->end && l->source[l->pos] != '\n')
        l->pos++; // consume until newline
      return;
    }

    else if (l->source[l->pos + 1] == '*')
    {
      while (++l->pos < l->end)
      {
        uint32_t pos = l->pos;
        if (l->source[pos] == '*' && l->source[++l->pos] == '/')
          break; // consume matching comment close
      }
      ++l->pos;
      return;
    }
  }
}

static char unescape(char **s)
{
  assert(*s);
  if (isdigit(**s))
    return (char)strtol(*s, s, 0);
  switch (**s)
  {
  case 'a':
    return 0x7; // bell
  case 'b':
    return 0x8; // backspace
  case 't':
    return 0x9; // tab
  case 'n':
    return 0xa; // new line
  case 'v':
    return 0xb; // vertical tab
  case 'f':
    return 0xc; // form feed
  case 'r':
    return 0xd; // carriage return
  case 'e':
    return 0x1b; // escape
  case '\\':
    return '\\';
  // actually kinda unreachable
  default:
    return 0;
  }
}

static TokTag hash_kw(const char *s, uint32_t len)
{
  // TODO: efficient perfect hash
  if (len != 2 && len != 6)
    ; // we don't have a keyword
  else if (!strncmp(s, "return", len))
    return KW_RET;
  else if (!strncmp(s, "as", len))
    return KW_AS;
  else if (!strncmp(s, "fn", len))
    return KW_FN;

  return VAL_ID;
}

#define MAX_SCOPE_DEPTH 10

typedef struct ScopeStacks
{
  uint32_t stacks[3][MAX_SCOPE_DEPTH];
  uint8_t cursors[3];
} ScopeStacks;

// lex(Lexer<'a>) -> LexRes<'b>
// TODO: actually take the effort to optimize this
[[nodiscard]] LexRes lex(Lexer l)
{
  LexBuf res_buf = {};
  ScopeStacks scopes = {0};

  while (l.pos < l.end)
  {
    consume_ws(&l);      // skip all white space
    consume_comment(&l); // skip comments
    consume_ws(&l);      // skip the white space after comments

    if (l.pos >= l.end)
      break;

    Token tok = {0};

    char char_at = l.source[l.pos];

    if (isdigit(char_at))
    { // TODO: convert this into a jump table
      // we are lexing a number
      char *end;
      char *start = l.source + l.pos;
      // TODO: strtoll does not have a max len, so this
      //       will evoke ub when there is a number right
      //       before eof and `l.source` isn't null terminated
      uint64_t num = (uint64_t)strtoll(start, &end, 0);
      uint32_t idx = push_lit(&res_buf, num);
      tok.pos = l.pos;
      tok.as_lit_idx = idx;
      tok.tag |= LIT_INT;

      l.pos += (uint32_t)(end - start);
    }
    else if (islower(char_at))
    {
      // we are lexing a value ident or a keyword
      uint32_t start = l.pos;
      while (l.pos < l.end &&
             (isalnum(l.source[l.pos]) || l.source[l.pos] == '_'))
        l.pos++;

      uint32_t len = l.pos - start;

      TokTag kind = hash_kw(l.source + start, len);
      if (kind == VAL_ID)
      { // we have a value ident
        StrView s = {.source = l.source + start, .len = len};
        Intern i = intern_strview(&res_buf, s);

        tok.as_val_ident = i;
      }

      tok.tag |= kind;
      tok.pos = start;
    }
    else if (isupper(char_at))
    {
      // we are lexing a Type ident
      uint32_t start = l.pos;
      while (l.pos < l.end &&
             (isalnum(l.source[l.pos]) || l.source[l.pos] == '_'))
        l.pos++;

      StrView s = {.source = l.source + start, .len = l.pos - start};
      Intern i = intern_strview(&res_buf, s);
      tok.pos = start;
      tok.as_val_ident = i;
      tok.tag |= TYPE_ID;
    }
    else if (char_at == '"')
    {
      uint32_t start = ++l.pos; // skip first quote
      uint16_t mark = intern_mark(res_buf);

      bool escaped = false;
      while (l.pos < l.end && !(l.source[l.pos] == '"' && !escaped))
      {
        if (escaped)
        {
          char *start = l.source + l.pos;
          char *end = start;
          char c = unescape(&end);
          l.pos += (uint32_t)(end - start);
          intern_char(&res_buf, c);
          escaped = false;
          continue;
        }
        escaped = l.source[l.pos] == '\\';
        l.pos++;
      }
      Intern i = {.idx = mark, .len = (uint16_t)((mark - l.pos) << 8)};
      l.pos++; // skip last quote
      tok.pos = start;
      tok.as_str_lit = i;
      tok.tag |= LIT_INT;
    }
    else if (char_at == '(' || char_at == '{' || char_at == '[')
    {

      uint8_t hash = (uint8_t)((char_at & 0xf) + (char_at >> 4));
      hash -= 10;
      hash >>= 2;

      uint8_t cursor = scopes.cursors[hash]++;
      if (cursor > MAX_SCOPE_DEPTH)
        goto next_token; // scope exceeded max depth,
                         // so this token is INVALID
      scopes.stacks[hash][cursor] = res_buf.tokens_len;
      tok.pos = l.pos++;
      tok.tag = (uint32_t)char_at;
    }
    else if (char_at == ')' || char_at == '}' || char_at == ']')
    {

      uint8_t hash = (uint8_t)((char_at & 0xf) + (char_at >> 4));
      hash -= 11;
      hash >>= 2;

      uint8_t cursor = --scopes.cursors[hash];

      if (cursor > MAX_SCOPE_DEPTH)
      {
        // scope isn't opened, or exceeds max depth
        // either way, this token is INVALID

        scopes.cursors[hash] =
            cursor == 0xff
                ? 0
                : cursor; // fix up in case of unmatched closing delim
        goto next_token;
      }
      uint32_t opening_delim = scopes.stacks[hash][cursor];
      // fix up the opening delimitier
      res_buf.tokens[opening_delim].matching_scp |= res_buf.tokens_len << 8;

      tok.pos = l.pos++;
      tok.tag = (uint32_t)char_at;
      tok.matching_scp |= opening_delim << 8;
    }
    else if (ispunct(char_at))
    {
      tok.pos = l.pos;
      if (char_at == '-')
      {
        if (l.source[++l.pos] == '>')
        {
          tok.tag = KW_ARROW;
        }
        else
        {
          tok.tag = HYPHON;
        }

        l.pos++;
      }
      else
      {
        tok.tag = (uint32_t)char_at;
        l.pos++;
      }
    }
    else
    {
      l.pos++;
    }

  next_token:
    push_token(&res_buf, tok);
  }

  LexRes res;
  res.intern = res_buf.intern;
  res.tokens = res_buf.tokens;
  res.tok_num = res_buf.tokens_len;
  res.lits = res_buf.lits;

  return res;
}

/*
static void _print_toks(LexRes token_buf) {
  for (uint32_t i = 0; i < token_buf.tok_num; ++i) {
    putchar(' ');
    Token tok = token_buf.tokens[i];
    switch (tok.tag & 0xff) {
      case INVALID: printf("INVALID"); break;
      case LIT_INT: printf("%lu", token_buf.lits[tok.as_lit_idx >> 8]); break;
      case LIT_STR:
        printf("\"%.*s\"", tok.as_str_lit.len >> 8,
               token_buf.intern + tok.as_str_lit.idx);
        break;
      case  VAL_ID:
        printf("val_id(%.*s)", tok.as_val_ident.len >> 8,
               token_buf.intern + tok.as_val_ident.idx);
        break;
      case  TYPE_ID:
        printf("typ_id(%.*s)", tok.as_typ_ident.len >> 8,
               token_buf.intern + tok.as_typ_ident.idx);
        break;
      case COLON:
      case SEMI:
      case HYPHON:
      case PIPE:
        putchar(tok.tag);
        break;
      case PAREN_O:
      case PAREN_C:
      case BRACE_O:
      case BRACE_C:
      case BRACK_O:
      case BRACK_C: {
        uint32_t scope = tok.matching_scp >> 8;
        printf("%c matching %d", tok.tag & 0xff, scope);
        break;
      }
      case KW_AS: printf("as"); break;
      case KW_FN: printf("fn"); break;
      case KW_RET: printf("return"); break;
      case KW_ARROW: printf("->"); break;
      default: printf("unkown token"); break;
    }
  }
  printf("\n");
}


int main(int argc, char** argv) {
  if (argc < 2) {
    printf("provide a file to lex\n");
    exit(1);
  }
  FILE *f = fopen(argv[1], "rb");
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);  // same as rewind(f);

  char *string = malloc(fsize + 1);
  fread(string, fsize, 1, f);
  fclose(f);

  string[fsize] = 0;
  Lexer l = {.source = string, .pos = 0, .end = fsize - 1};
  LexRes lexres = lex(l);
  free(string);

  printf("found %d tokens\n", lexres.tok_num);

  _print_toks(lexres);

  destroy_lexres(lexres);

  return 0;
}

*/
