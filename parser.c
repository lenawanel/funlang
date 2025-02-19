#include "lexer.h"
#include <string.h>
#define __FUNLANG_COMMON_H_IMPL
#include "common.h"
#include <assert.h>
#include <err.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum __attribute__((aligned(4))) AstNodeKind
{
  BINDING_NAME,
  BINDING_USE,
  LITERAL,
} ParseNodeKind;

typedef struct __attribute__((aligned(8))) ParseNode
{
#pragma pack(push, 4)
  union
  {
    struct
    {
      char    *text;
      uint32_t size;
    } str_view;
    uint64_t lit;
  };
#pragma pack(pop)
  ParseNodeKind kind;
  uint32_t pos;
  uint32_t subtree_size;
} ParseNode;

typedef struct ParseBuffer
{
  char    *intern;
  uint32_t intern_len, intern_cap;

  ParseNode *tree_buf;
  uint32_t   tree_len, tree_cap;
} ParseBuffer;

static void push_node(ParseBuffer *restrict buf, ParseNode parse_node)
{
  push_elem((DynamicArray *)&buf->tree_buf, sizeof(ParseNode), &parse_node);
}

void intern_lex_interned_into(ParseBuffer *buf, char *lex_intern, Intern intern,
                              ParseNode *node)
{
  node->str_view.text = buf->intern + buf->intern_len;

  uint32_t intern_len = intern.len >> 8;
  node->str_view.size = intern_len;
  buf->intern_len += intern_len;

  if (buf->intern_len > buf->intern_cap)
    grow_array((DynamicArray *)&buf->intern, sizeof(char));

  memcpy(buf->intern + buf->intern_len - intern_len, lex_intern + intern.idx,
         intern_len);
}

typedef struct ParseRes
{
  char *intern;

  ParseNode *tree_buf;
  uint32_t   tree_len;
} ParseRes;

void destroy_ast(ParseRes a)
{
  free(a.tree_buf);
  free(a.intern);
}

// TODO: consistently name the variants
typedef enum
{
  // if this flag is set, we may just not encounter any expected value
  NOTHING = (0x1 << 0x1f),
  MANY = (0x1 << 0x1e),
  // length is stored in data field
  SEQUENCE_START = 0x1 << 0x1d,
  // length is stored in data field
  SEQUENCE_END = 0x1 << 0x1c,
  // a function may be defined if this flag is set
  FUNCTION = 0x1 << 0x1b, // TODO: is this useful?
                          // we are expecting a value ident if flag is set
                          // misc flag used by rules
  MISC = 1 << 0x1a,

  // a list of argumets of the for (name1: type, ..)
  ARGBINDLIST_BEGIN = 0x1,
  ARGBINDLIST_BODY = 0x2,
  ARGBINDLIST_END  = 0x3,
  // a list of implicit arguments of the form [Type0, Type1 <: Type2, ..]
  IMPLICIT_ARGS_BEGIN = 0x4,
  // a block of statements optionally ending with an expression
  RULE_BLOCK = 0x5,
  // an arrow "->", like in the
  RULE_ARROW = 0x6,
  RULE_COLON = 0x7,
  // a Type currently only supports type names
  // TODO: make the types of all values (e.g functions) nameable
  RULE_TYPE  = 0x8,
  RULE_VAL_ID = 0x9,
} ParserStateKind;

static ParserStateKind extract_rule(ParserStateKind st) { return st & 0xffff; }

static bool flags_present(ParserStateKind st, ParserStateKind flags)
{
  return (st & flags) == flags;
}

typedef struct
{
  ParserStateKind kind;
  uint32_t data;
} ParserState;

typedef struct
{
  ParserState *stack;
  uint32_t len, cap;
} ParseStack;

static void push_state(register ParseStack *restrict state,
                       ParserState parser_state)
{
  push_elem((DynamicArray *)&state->stack, sizeof(ParserState), &parser_state);
}

static ParserState pop_state(register ParseStack *restrict state)
{
  ParserState p;
  pop_elem((DynamicArray *)&state->stack, sizeof(ParserState), &p);
  return p;
}

static ParserState cur_state(register ParseStack *restrict state)
{
  return state->stack[state->len - 1];
}

ParseRes parse(LexRes lexres)
{
  ParseBuffer buf  = {};
  ParseStack state = {};

  push_state(&state, (ParserState){.data = 0, .kind = FUNCTION});

  for (uint32_t tok_idx = 0; tok_idx < lexres.tok_num;)
  {
    ParserState next_state;
    next_state.data = 0;

    // TODO: handle errors
    Token tok = lexres.tokens[tok_idx];
    ParserState current = pop_state(&state);

    bool matched;

    switch (tok.tag & 0xff)
    {
    case VAL_ID:
      // make sure that we are expecting a val id here
      matched = extract_rule(current.kind) & RULE_VAL_ID;
      if (!matched)
        break;

      ParseNode parse_node = {};
      parse_node.kind = current.kind & MISC ? BINDING_NAME : BINDING_USE;

      intern_lex_interned_into(&buf, lexres.intern, tok.as_val_ident,
                               &parse_node);
      push_node(&buf, parse_node);

      break;
    case PAREN_O:
      // TODO: function application, parenthized expressions
      switch (cur_state(&state).kind)
      {
      case ARGBINDLIST_BEGIN:
        matched = true;

        pop_state(&state);
        next_state.kind = ARGBINDLIST_BODY;
        push_state(&state, next_state);

        next_state.kind = RULE_TYPE | SEQUENCE_END | MANY;
        next_state.data = 3;
        push_state(&state, next_state);

        next_state.kind = RULE_COLON;
        next_state.data = 0;
        push_state(&state, next_state);

        next_state.kind = RULE_VAL_ID | SEQUENCE_START | NOTHING | MISC;
        next_state.data = 3;
        push_state(&state, next_state);

        break;
      default: // error
        matched = false;
        break;
      }
    case KW_FN:
      matched = current.kind & FUNCTION;
      if (!matched)
        break;

      // we are at `fn`
      // what we expect is:
      //
      // fn    funct_name [Type1,  Type2] (arg1: T) -> Typ { ... }
      // ^    |^        ^|^             ^|^       ^|^    ^|^     ^
      // [cur]|<BINDNAME>|<IMPLICIT ARGS>|<ARGLIST>|<RETY>|<BLOCK>

      next_state.kind = RULE_BLOCK;
      push_state(&state, next_state);
      next_state.kind = RULE_TYPE | NOTHING;
      push_state(&state, next_state);
      next_state.kind = RULE_ARROW | NOTHING;
      push_state(&state, next_state);
      next_state.kind = ARGBINDLIST_BEGIN;
      push_state(&state, next_state);
      next_state.kind = IMPLICIT_ARGS_BEGIN | NOTHING;
      push_state(&state, next_state);
      next_state.kind = MISC | RULE_VAL_ID;
      push_state(&state, next_state);

      break;

    default:
      err(1, "encountered unkown token");
    }

    if (!matched && current.kind & NOTHING)
    { // if an optional rule didn't match, continue trying the next
      // one
      if (flags_present(current.kind, SEQUENCE_START))
        state.len -=
            current.data - 1; // we didn't match the start of a sequence
                              // so we skip every other part too
      continue;
    }
    if (!matched)
      abort(); // TODO: error handling

    if (flags_present(current.kind, SEQUENCE_END | MANY))
      state.len += current.data; // we matched a full reccuring sequence
    else if (flags_present(current.kind, MANY))
      state.len++; // we matched a rule that may infinitely repeat
                   // so we repush this exact rule
                   // in practice it's unlikely that I'll use this
  }

  ParseRes res = {
      .tree_buf = buf.tree_buf, .tree_len = buf.tree_len, .intern = buf.intern};

  return res;
}

int main(int argc, char **argv)
{
  if (argc < 2)
  {
    printf("provide a file to lex\n");
    exit(1);
  }
  FILE *f = fopen(argv[1], "rb");
  (void)fseek(f, 0, SEEK_END);
  size_t fsize = (size_t)ftell(f);
  (void)fseek(f, 0, SEEK_SET); // same as rewind(f);

  char *string = malloc(fsize + 1);
  (void)fread(string, fsize, 1, f);
  (void)fclose(f);

  string[fsize] = 0;
  Lexer l = {.source = string, .pos = 0, .end = (uint32_t)fsize - 1};

  {
    LexRes lexres = lex(l);
    free(string);

    printf("lexed %d tokens\n", lexres.tok_num);

    destroy_lexres(lexres);
  }

  return 0;
}
