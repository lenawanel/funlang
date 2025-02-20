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
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

typedef struct
{
  DynamicArray intern;

  DynamicArray tokens;

  DynamicArray lits;
} LexBuf;

static void intern_char(LexBuf *restrict lexbuf, char c)
{
  push_elem((DynamicArray *)&lexbuf->intern, sizeof(char), &c);
}

static uint16_t intern_mark(LexBuf lexbuf)
{
  return (uint16_t)lexbuf.intern.len;
}

static Intern intern_strview(LexBuf *restrict lexbuf, StrView s)
{
  uint32_t len = MIN(s.len, MAX_INTERN_LEN);
  lexbuf->intern.len += len;
  if (lexbuf->intern.len > lexbuf->intern.cap)
    grow_array((DynamicArray *)&lexbuf->intern, sizeof(char));

  Intern i = {.idx = (uint16_t)(lexbuf->intern.len - len),
              .len = (uint16_t)(s.len << 8)};

  memcpy(lexbuf->intern.buffer + lexbuf->intern.len - len, s.source,
         s.len * sizeof(char));

  return i;
}

static uint32_t push_lit(LexBuf *restrict lexbuf, uint64_t val)
{
  return push_elem(&lexbuf->lits, sizeof(uint64_t), &val);
}

static void push_token(LexBuf *restrict lexbuf, Token tok)
{
  push_elem(&lexbuf->tokens, sizeof(Token), &tok);
}

void destroy_lexres(LexRes lex_res)
{
  free(lex_res.intern);
  free(lex_res.tokens);
  free(lex_res.lits);
}

static bool consume_ws(Lexer *restrict l)
{
  bool skipped = false;

  while (l->pos < l->end && isspace(l->source[l->pos]))
  {
    skipped = true;
    l->pos++;
  }

  return skipped;
}

static bool consume_comment(Lexer *restrict l)
{
  if (l->pos + 1 > l->end)
    return false;

  if (l->source[l->pos++] == '/')
  {
    if (l->source[l->pos++] == '/')
    { // single line comment
      while (l->pos < l->end && l->source[l->pos] != '\n')
        l->pos++; // consume until newline
      return true;
    }

    else if (l->source[l->pos++] == '*')
    {
      while (++l->pos < l->end)
      {
        uint32_t pos = l->pos;
        if (l->source[pos] == '*' && l->source[++l->pos] == '/')
          break; // consume matching comment close
      }
      ++l->pos;
      return true;
    }
  }
  return false;
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
    bool skippeable = true;
    // skip all whitespace and comments
    while (skippeable)
    {
      skippeable = consume_ws(&l);
      skippeable |= consume_comment(&l);
    }

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
      scopes.stacks[hash][cursor] = res_buf.tokens.len;
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
      ((Token *)res_buf.tokens.buffer)[opening_delim].matching_scp |=
          res_buf.tokens.len << 8;

      tok.pos = l.pos++;
      tok.tag = (uint32_t)char_at;
      tok.matching_scp |= opening_delim << 8;
    }
    else if (ispunct(char_at))
    {
      tok.pos = l.pos++;
      if (char_at == '-')
      {
        if (l.source[l.pos] == '>')
        {
          tok.tag = KW_ARROW;
        }
        else
        {
          tok.tag = HYPHON;
        }
      }
      else
      {
        tok.tag = (uint32_t)char_at;
      }
      l.pos++;
    }
    else
    {
      l.pos++;
    }

  next_token:
    push_token(&res_buf, tok);
  }

  LexRes res;
  res.intern = res_buf.intern.buffer;
  res.tokens = res_buf.tokens.buffer;
  res.tok_num = res_buf.tokens.len;
  res.lits = res_buf.lits.buffer;

  return res;
}
