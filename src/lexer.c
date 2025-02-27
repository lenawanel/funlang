#include "lexer.h"
#include "common.h"
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_INTERN_LEN 0xff

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

  memcpy(lexbuf->intern.buffer + lexbuf->intern.len - len, s.txt,
         len * sizeof(char));

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

  while (l->cur < l->end && isspace(*l->cur))
  {
    skipped = true;
    l->cur++;
  }

  return skipped;
}

static bool consume_char(Lexer *restrict l, char c)
{
  if (l->cur + 1 > l->end)
    return false;

  if (*l->cur == c)
  {
    l->cur++;
    return true;
  }
  else
  {
    return false;
  }
}

static bool consume_comment(Lexer *restrict l)
{
  if (l->cur + 1 > l->end)
    return false;

  if (consume_char(l, '/'))
  {
    if (consume_char(l, '/'))
    { // single line comment

      while (l->cur < l->end && *l->cur != '\n')
        l->cur++; // consume until newline

      l->cur++; // consume remaining newline
      return true;
    }
    else if (consume_char(l, '*'))
    {
      while (++l->cur + 1 < l->end)
        if (!memcmp("*/", l->cur, 2))
          break; // consume matching comment close

      l->cur += 2;
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
  if (len != 2 && len != 6 && len != 3)
    ; // we don't have a keyword
  else if (!strncmp(s, "return", len))
    return TOK_KW_RETRN;
  else if (!strncmp(s, "as", len))
    return TOK_KW_AS;
  else if (!strncmp(s, "let", len))
    return TOK_KW_LET;
  else if (!strncmp(s, "fn", len))
    return TOK_KW_FN;

  return TOK_VAL_ID;
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

  while (l.cur < l.end)
  {

    if (l.cur >= l.end)
      break;

    Token tok = {0};

    char char_at = *l.cur;
    printf("lexer @ %c\n", char_at);

    // TODO: clean up this stuff, it's ugly
    if (isspace(char_at))
    {
      consume_ws(&l);
      continue;
    }
    else if (char_at == '/')
    {
      if (consume_comment(&l))
        continue;
      goto punct;
    }
    else if (isdigit(char_at))
    { // TODO: convert this into a jump table
      // we are lexing a number
      char *start = l.cur;
      // TODO: strtoll does not have a max len, so this
      //       will evoke ub when there is a number right
      //       before eof and `l.source` isn't null terminated
      uint64_t num = (uint64_t)strtoll(start, &l.cur, 0);
      uint32_t idx = push_lit(&res_buf, num);
      tok.pos = (uint32_t)(l.src - l.cur);
      tok.as_lit_idx = idx;
      tok.tag |= TOK_LIT_INT;
    }
    else if (islower(char_at))
    {
      // we are lexing a value ident or a keyword
      char *start = l.cur;
      while (l.cur < l.end && (isalnum(*l.cur) || *l.cur == '_'))
        l.cur++;

      uint32_t len = (uint32_t)(l.cur - start);

      TokTag kind = hash_kw(start, len);
      if (kind == TOK_VAL_ID)
      { // we have a value ident
        StrView s = {.txt = start, .len = len};
        Intern i = intern_strview(&res_buf, s);

        tok.as_val_ident = i;
      }

      tok.tag |= kind;
      tok.pos = (uint32_t)(l.src - start);
    }
    else if (isupper(char_at))
    {
      // we are lexing a Type ident
      char *start = l.cur;
      while (l.cur < l.end && (isalnum(*l.cur) || *l.cur == '_'))
        l.cur++;

      StrView s = {.txt = start, .len = (uint32_t)(l.cur - start)};
      Intern i = intern_strview(&res_buf, s);
      tok.pos = (uint32_t)(l.src - start);
      tok.as_val_ident = i;
      tok.tag |= TOK_TYPE_ID;
    }
    else if (char_at == '"')
    {
      char *start = ++l.cur; // skip first quote
      uint16_t mark = intern_mark(res_buf);

      bool escaped = false;
      while (l.cur < l.end && !(*l.cur == '"' && !escaped))
      {
        if (escaped)
        {
          char c = unescape(&l.cur);
          intern_char(&res_buf, c);
          escaped = false;
          continue;
        }
        intern_char(&res_buf, *l.cur);
        escaped = *l.cur == '\\';
        l.cur++;
      }
      Intern i = {.idx = mark,
                  .len = (uint16_t)((uint16_t)(l.cur - start) << 8u)};
      l.cur++; // skip closing quote
      tok.pos = (uint32_t)(l.src - start);
      tok.as_str_lit = i;
      tok.tag |= TOK_LIT_INT;
    }
    else if (strchr("({[", char_at))
    {

      uint8_t hash = (uint8_t)((char_at & 0xf) + (char_at >> 4));
      hash -= 10;
      hash >>= 2;

      uint8_t cursor = scopes.cursors[hash]++;
      if (cursor >= MAX_SCOPE_DEPTH)
        goto next_token; // scope exceeded max depth,
                         // so this token is INVALID
      scopes.stacks[hash][cursor] = res_buf.tokens.len;
      tok.pos = (uint32_t)(l.src - l.cur++);
      tok.tag = (uint32_t)char_at;
    }
    else if (strchr(")}]", char_at))
    {

      uint8_t hash = (uint8_t)((char_at & 0xf) + (char_at >> 4));
      hash -= 11;
      hash >>= 2;

      uint8_t cursor = --scopes.cursors[hash];

      if (cursor >= MAX_SCOPE_DEPTH)
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
          (int32_t)((res_buf.tokens.len - opening_delim) << 8);

      tok.pos = (uint32_t)(l.src - l.cur++);
      tok.tag = (uint32_t)char_at;
      tok.matching_scp |= (int32_t)((opening_delim - res_buf.tokens.len) << 8);
    }
    else if (ispunct(char_at))
    {
    punct:
      tok.pos = (uint32_t)(l.src - l.cur++);
      if (char_at == '-')
      {
        if (*l.cur == '>')
        {
          tok.tag = TOK_KW_ARROW;
          l.cur++;
        }
        else
        {
          tok.tag = TOK_HYPHON;
        }
      }
      else if (char_at == '<')
      {
        if (*l.cur == ':')
        {
          tok.tag = TOK_KW_SUBTY;
          l.cur++;
        }
        else
        {
          tok.tag = (unsigned char)char_at;
        }
      }
      else
      {
        tok.tag = (uint32_t)char_at;
      }
    }
    else
    {
      l.cur++;
    }

  next_token:
    push_token(&res_buf, tok);
  }

  LexRes res;
  res.intern = res_buf.intern.buffer;
  res.tokens = res_buf.tokens.buffer;
  res.tkeptr = res_buf.tokens.buffer + res_buf.tokens.len * sizeof(Token);
  res.lits = res_buf.lits.buffer;

  return res;
}
