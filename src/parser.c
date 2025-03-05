/*
 * A parser for a fun language.
 *  The current approach is inspired by Carbon and pretty closely
 *  resembles A simple LL(N) parser.
 *
 *  currently it's very much unrefined and has lots of unnecassary clutter.
 */

#include "parser.h"
#include "hashtable.h"
#include "lexer.h"
#include <stdio.h>
#include <string.h>

typedef struct
{
  DynamicArray buf;
  DynamicArray cls;
  HSet names;
} PBuf;

// TODO: rework numerical constants
// TODO: consistent naming scheme
typedef enum
{
  ROOT = 0,

  FUNC = 1,
  TYPE = 2,

  IMPLICIT_ARG_LIST = 4,
  EXPLICIT_ARG_LIST = 9,

  STATEMENT  = 0x10,
  EXPRESSION = 0x70,
  EXPR_CONT  = 0xc0,

  CONTENT_LIT = 0x1000000, // is there a literal associated with the token
  CONTENT_STR = 0x2000000, // is there a string associated with the token
  CHOICE_END  = 0x4000000, // optional rule? y/n
  CHOICE      = 0x8000000, // optional rule? y/n

  // WCLSE = 0x10000000, // waiting for a close? y/n

  INTRO          = 0x20000000,                // introducer token? y/n
  CLOSE          = 0x40000000,                // bracketing closer? y/n
  CLOSE_FN_ARROW = CLOSE | INTRO | 0x1a,      // add ->
  CLOSE_TY_SUBTY = CLOSE | INTRO | 0x1b,      // add <:
  CLOSE_TY_JUDGE = CLOSE | INTRO | 0x1c,      // add :
  CLOSE_PFX_SUB  = CLOSE | INTRO | 0x1d,      // add prefix -
  CLOSE_IFX_ADD  = CLOSE | INTRO | EXPR_CONT, // add inifix +

  TERM              = 0x80000000,                          // terminal? y/n
  TERM_BL_OPN_INT   = TERM | INTRO | 0x100,                // {
  TERM_LET_BIND     = TERM | INTRO | STATEMENT,            // let
  TERM_ASSGN        = TERM | STATEMENT + 1,                // =
  TERM_RETURN       = TERM | INTRO | STATEMENT + 2,        // return
  TERM_FN_INT       = TERM | INTRO | FUNC,                 // fn
  TERM_FN_END       = TERM | CLOSE | FUNC,                 // }
  TERM_BIND_NAME    = TERM | CONTENT_STR | 0x103,          // name
  TERM_BIND_USE     = TERM | CONTENT_STR | EXPRESSION,     // name
  TERM_LITERAL_INT  = TERM | CONTENT_STR | EXPRESSION + 1, // name
  TERM_PREFIX_MINUS = TERM | CLOSE_PFX_SUB,                // -
  TERM_SEMI         = TERM | INTRO | CLOSE | STATEMENT,    // ;
  TERM_ADD_CONT     = TERM | CLOSE_IFX_ADD,                // +

  TERM_TY_FN_ARROW = TERM | CLOSE_FN_ARROW, // ->
  TERM_TY_SUBTY    = TERM | CLOSE_TY_SUBTY, // <:
  TERM_TY_JUDGE    = TERM | CLOSE_TY_JUDGE, // :
  TERM_BUILTIN_TY  = TERM | TYPE,           // u32, s32, ...
  TERM_TY_NAME_USE = TERM | TYPE + 1,       // Atype, ...

  TERM_IMP_ARGL_INT = TERM | INTRO | IMPLICIT_ARG_LIST, // [
  TERM_IMP_ARG_INT =
      TERM | CONTENT_STR | INTRO | IMPLICIT_ARG_LIST + 1,   // Tname
  TERM_IMP_ARG_SEP  = TERM | INTRO | IMPLICIT_ARG_LIST + 2, // ,
  TERM_IMP_ARGL_END = TERM | CLOSE | IMPLICIT_ARG_LIST,     // ]

  TERM_EXP_ARGL_INT = TERM | INTRO | EXPLICIT_ARG_LIST,                  // (
  TERM_EXP_ARG_INT = TERM | CONTENT_STR | INTRO | EXPLICIT_ARG_LIST + 1, // name
  TERM_EXP_ARG_SEP = TERM | INTRO | EXPLICIT_ARG_LIST + 2,               // ,
  TERM_EXP_ARGL_END = TERM | CLOSE | EXPLICIT_ARG_LIST,                  // )
} PStateKind;

typedef struct
{
  PStateKind kind;

  uint32_t chsz; // TODO: ?
  union
  {
    uint32_t tpos;
    struct
    {
      uint32_t tok_pos, nod_pos;
    } dclo;
  };
} PState;

typedef DynamicArray PStack;

#define DYNAMIC_MASK (CHOICE | CHOICE_END)
#define STATIC_MASK ~(unsigned int)DYNAMIC_MASK

static bool term_matches(PStateKind term, TokTag tag)
{
  if (!(term & TERM)) return false;

  // TODO: tag lut? or more complex PStateKind encoding?
  switch (term & STATIC_MASK)
  {
  case TERM_FN_INT:       return (tag & 0xff) == TOK_KW_FN;
  case TERM_FN_END:       return (tag & 0xff) == '}';
  case TERM_BL_OPN_INT:   return (tag & 0xff) == '{';
  case TERM_LET_BIND:     return (tag & 0xff) == TOK_KW_LET;
  case TERM_ASSGN:        return (tag & 0xff) == '=';
  case TERM_RETURN:       return (tag & 0xff) == TOK_KW_RETRN;
  case TERM_BIND_NAME:
  case TERM_BIND_USE:     return (tag & 0xff) == TOK_VAL_ID;
  case TERM_LITERAL_INT:  return (tag & 0xff) == TOK_LIT_INT;
  case TERM_PREFIX_MINUS: return (tag & 0xff) == '-';
  case TERM_SEMI:         return (tag & 0xff) == ';';
  case TERM_ADD_CONT:     return (tag & 0xff) == '+';
  case TERM_TY_FN_ARROW:  return (tag & 0xff) == TOK_KW_ARROW;
  case TERM_TY_SUBTY:     return (tag & 0xff) == TOK_KW_SUBTY;
  case TERM_TY_JUDGE:     return (tag & 0xff) == ':';
  case TERM_BUILTIN_TY:
    return memchr((char[]){(char)TOK_KW_U8, (char)TOK_KW_U16, (char)TOK_KW_U32,
                           (char)TOK_KW_U64, (char)TOK_KW_S8, (char)TOK_KW_S16,
                           (char)TOK_KW_S32, (char)TOK_KW_S64},
                  (int)tag, 8);
  case TERM_TY_NAME_USE:  return (tag & 0xff) == TOK_TYPE_ID;
  case TERM_IMP_ARGL_INT: return (tag & 0xff) == '[';
  case TERM_IMP_ARG_INT:  return (tag & 0xff) == TOK_TYPE_ID;
  case TERM_IMP_ARGL_END: return (tag & 0xff) == ']';
  case TERM_EXP_ARGL_INT: return (tag & 0xff) == '(';
  case TERM_EXP_ARG_INT:  return (tag & 0xff) == TOK_VAL_ID;
  case TERM_IMP_ARG_SEP:
  case TERM_EXP_ARG_SEP:  return (tag & 0xff) == ',';
  case TERM_EXP_ARGL_END: return (tag & 0xff) == ')';
  default:                assert(false && "unhandled terminal match");
  }
}

static StrView intern_lex_intern(HSet *names, char *intern, Intern i)
{
  return insert(names, intern + i.idx, i.len >> 8);
}

static void term_into_node(Token word, PStateKind state, PNode *n, LexRes *lr,
                           PBuf *tree)
{

  if (state & CONTENT_STR)
    n->str = intern_lex_intern(&tree->names, lr->intern, word.as_intern);
  else if (state & CONTENT_LIT)
    n->literal_int = lr->lits[n->literal_int];

  switch (state & STATIC_MASK)
  {
  case TERM_BL_OPN_INT:   n->kind = FUN_BLOCK; break;
  case TERM_LET_BIND:     n->kind = STMT_LET_BIND; break;
  case TERM_ASSGN:        n->kind = STMT_ASSGN_EQ; break;
  case TERM_RETURN:       n->kind = STMT_RETURN; break;
  case TERM_FN_INT:       n->kind = FUN_INT; break;
  case TERM_FN_END:       n->kind = FUN_END; break;
  case TERM_BIND_NAME:    n->kind = BIND_NAME; break;
  case TERM_BIND_USE:     n->kind = BIND_USE; break;
  case TERM_LITERAL_INT:  n->kind = LITERAL_INT; break;
  case TERM_PREFIX_MINUS: n->kind = PREFIX_MINUS; break;
  case TERM_SEMI:         n->kind = STMT_SEMI; break;
  case TERM_ADD_CONT:     n->kind = INFIX_PLUS; break;
  case TERM_TY_FN_ARROW:  n->kind = FUN_ARROW; break;
  case TERM_TY_SUBTY:     n->kind = BIND_TY_SUBTY; break;
  case TERM_TY_JUDGE:     n->kind = BIND_TY_JUDGE; break;
  case TERM_BUILTIN_TY:   n->kind = BUILTIN_TY; break; // TODO: kind
  case TERM_TY_NAME_USE:  n->kind = BIND_TY_USE; break;
  case TERM_EXP_ARGL_END: n->kind = EXP_ARGLIST_END; break;
  case TERM_EXP_ARGL_INT: n->kind = EXP_ARGLIST_BEG; break;
  case TERM_EXP_ARG_INT:  n->kind = BIND_NAME; break;
  case TERM_EXP_ARG_SEP:  n->kind = EXP_ARGLIST_SEP; break;
  case TERM_IMP_ARGL_END:
  case TERM_IMP_ARGL_INT:
  case TERM_IMP_ARG_INT:
  case TERM_IMP_ARG_SEP:
  default:
    printf("state = 0x%x\n", state);
    assert(false && "unmatched term writing to node");
  }
}

[[nodiscard]] ParseRes parse(LexRes lr)
{
  PBuf tree;
  PStack stack;

  PState focus = {.kind = ROOT};
  Token word   = *lr.tokens;
  uint32_t i   = 0;

  while (lr.tokens < lr.tkeptr) // TODO: handle eof
  {
    PNode n = {};
    n.pos   = word.pos;

    // printf("%d\n", i++);
    // printf("word = 0x%x\n", word.tag & 0xff);
    // printf("rule = 0x%x\n", focus.kind);
    // printf("rule & CLOSE = 0x%x\n", focus.kind & CLOSE);
    // printf("rule & STATIC_MASK = 0x%x\n", focus.kind & STATIC_MASK);
    if (i > 90) abort();

    if ((focus.kind & TERM) && term_matches(focus.kind, word.tag))
    {

      if (focus.kind & CHOICE) stack.len -= focus.chsz;

      term_into_node(word, focus.kind, &n, &lr, &tree);

      if (focus.kind & INTRO)
      {
        switch (focus.kind & STATIC_MASK)
        {
        case TERM_FN_INT: focus.kind = FUNC; break;
        case TERM_EXP_ARGL_INT:

          focus.kind = TERM_EXP_ARGL_END;
          focus.tpos = tree.buf.len;
          co_push(&stack, focus);

          focus.kind = TERM_EXP_ARG_INT | CHOICE;
          focus.chsz = 0;
          break;
        case TERM_EXP_ARG_INT:

          focus.kind = TERM_EXP_ARG_SEP | CHOICE;
          co_push(&stack, focus);
          focus.kind = TERM_TY_JUDGE;
          break;
        case TERM_EXP_ARG_SEP:

          focus.kind = TERM_EXP_ARG_INT;
          focus.chsz = 0;
          break;
        case TERM_TY_JUDGE:

          focus.kind         = CLOSE_TY_JUDGE;
          focus.dclo.nod_pos = tree.buf.len;
          focus.dclo.tok_pos = word.pos;
          co_push(&stack, focus);
          focus.kind = TYPE;
          // TODO: hacky
          goto delay_closing;
        case TERM_TY_FN_ARROW:

          focus.kind         = TYPE;
          focus.dclo.nod_pos = tree.buf.len;
          focus.dclo.tok_pos = word.pos;
          break;
        case TERM_BL_OPN_INT:

          // TODO: handle free blocks, if blocks, ...
          focus.kind = TERM_FN_END;
          co_push(&stack, focus);
          focus.kind = STATEMENT;
          break;
        case TERM_LET_BIND:

          // TODO: maybe move semi stuff here
          focus.kind = EXPRESSION;
          co_push(&stack, focus);

          focus.kind = TERM_ASSGN;
          co_push(&stack, focus);

          focus.kind = TERM_TY_JUDGE | CHOICE;
          focus.tpos = tree.buf.len;
          co_push(&stack, focus);

          focus.kind = TERM_BIND_NAME;
          break;
        case TERM_ADD_CONT:
          focus.kind         = CLOSE_IFX_ADD;
          focus.dclo.nod_pos = tree.buf.len;
          focus.dclo.tok_pos = word.pos;
          co_push(&stack, focus);

          focus.kind = EXPRESSION;
          // TODO: hacky
          goto delay_closing;
        case TERM_PREFIX_MINUS:
          focus.kind         = CLOSE_PFX_SUB;
          focus.dclo.nod_pos = tree.buf.len;
          focus.dclo.tok_pos = word.pos;
          co_push(&stack, focus);

          focus.kind = EXPRESSION;
          // TODO: hacky
          goto delay_closing;
        case TERM_RETURN: focus.kind = EXPRESSION; break;
        case TERM_SEMI:

          n.subtree_sz = tree.buf.len - focus.tpos;

          focus.kind = STATEMENT;
          break;
        default: assert(false && "unhandled introducer");
        }
      }
      else if (focus.kind & CLOSE)
      {
        n.subtree_sz = tree.buf.len - focus.tpos;
        // TODO: empty stack
        if (stack.len) co_pop(&stack, &focus);
      }
      else if (stack.len) { co_pop(&stack, &focus); }

      co_push(&tree.buf, n);

    delay_closing:
      word = *++lr.tokens;
    }
    else if (focus.kind & CLOSE &&
             !(focus.kind & TERM)) // "fixup" close bracketing nodes
    {
      switch (focus.kind & STATIC_MASK) // TODO: factor out
      {
      case CLOSE_TY_JUDGE: n.kind = BIND_TY_JUDGE; break;
      case CLOSE_TY_SUBTY: n.kind = BIND_TY_SUBTY; break;
      case CLOSE_FN_ARROW: n.kind = FUN_ARROW; break;
      case CLOSE_PFX_SUB:  n.kind = PREFIX_MINUS; break;
      case CLOSE_IFX_ADD:  n.kind = INFIX_PLUS; break;
      default:             assert(false && "unhandled close fixup");
      }
      n.pos        = focus.dclo.tok_pos;
      n.subtree_sz = tree.buf.len - focus.dclo.nod_pos;
      co_push(&tree.buf, n);
      co_pop(&stack, &focus);
    }
    else if (!(focus.kind & TERM))
    {
      /*
       * nonterminal rules.
       *
       *  in this parser they basically play the role of convenience functions
       *  where with mutually recursive functions you would call smaller
       *  functions to parse parts of your grammer, here you just schedule them
       *  on a stack
       */

      // TODO: maybe propagate some flags
      switch (focus.kind & STATIC_MASK)
      {
      case ROOT: focus.kind = TERM_FN_INT; break;
      case FUNC:
        focus.kind = TERM_BL_OPN_INT;
        focus.tpos = tree.buf.len;
        co_push(&stack, focus);

        focus.kind = TERM_TY_FN_ARROW | CHOICE;
        focus.chsz = 0;
        co_push(&stack, focus);

        focus.kind = TERM_EXP_ARGL_INT;
        co_push(&stack, focus);

        focus.kind = TERM_IMP_ARGL_INT | CHOICE;
        focus.chsz = 0;
        co_push(&stack, focus);

        focus.kind = TERM_BIND_NAME;
        break;
      case TYPE:
        focus.kind = TERM_TY_NAME_USE | CHOICE_END;
        focus.chsz = 0;
        co_push(&stack, focus);

        focus.kind = TERM_BUILTIN_TY | CHOICE;
        focus.chsz = 1;
        break;

      case STATEMENT:
        // TODO: 0 stmts, ...
        focus.kind = TERM_LET_BIND | CHOICE;
        focus.chsz = 0;
        co_push(&stack, focus);

        focus.kind = TERM_RETURN | CHOICE;
        focus.chsz = 1;
        break;
      case EXPRESSION:
        focus.kind = EXPR_CONT;
        co_push(&stack, focus);

        focus.kind = TERM_PREFIX_MINUS | CHOICE_END;
        focus.chsz = 0;
        co_push(&stack, focus);

        focus.kind = TERM_LITERAL_INT | CHOICE;
        focus.chsz = 1;
        co_push(&stack, focus);

        focus.kind = TERM_BIND_USE | CHOICE;
        focus.chsz = 2;

        break;
      case EXPR_CONT:
        focus.kind = TERM_SEMI | CHOICE;
        focus.chsz = 0;
        co_push(&stack, focus);

        focus.kind = TERM_ADD_CONT | CHOICE;
        focus.chsz = 1;

        break;
      default: assert(false && "unhandled nonterminal");
      }
    }
    else if (focus.kind & CHOICE) // unmatched optional token
    {
      co_pop(&stack, &focus);
    }
    else
    {
      // TODO: error
      printf("focus.kind = 0x%x, word = 0x%x\n", focus.kind, word.tag & 0xff);
      assert(false && "encountered parse error?");
    }
  }

  free(stack.buffer);

  ParseRes res = {
      .size = tree.buf.len, .tree = tree.buf.buffer, .names = tree.names};

  return res;
}

void print_pnode(PNode n)
{
  switch (n.kind)
  {
  case INVALID:         printf("{ .kind = INVALID }"); break;
  case FUN_INT:         printf("{ .kind = FUN_INT }"); break;
  case STMT_RETURN:     printf("{ .kind = STMT_RETURN }"); break;
  case STMT_LET_BIND:   printf("{ .kind = STMT_LET_BIND }"); break;
  case BUILTIN_TY:      printf("{ .kind = BUILTIN_TY, kind = ** }"); break;
  case EXP_ARGLIST_BEG: printf("{ .kind = EXP_ARGLIST_BEG }"); break;
  case FUN_BLOCK:       printf("{ .kind = FUNBLOCK }"); break;
  case EXP_ARGLIST_SEP: printf("{ .kind = EXP_ARGLIST_SET }"); break;
  case BIND_TY_USE:     printf("{ .kind = BIND_TY_USE }"); break;
  case STMT_ASSGN_EQ:   printf("{ .kind = STMT_ASSGN_EQ }"); break;
  case EXP_ARGLIST_END:
    printf("{ .kind = EXP_ARGLIST_BEG, subtree_sz = %d }", n.subtree_sz);
    break;
  case STMT_SEMI:
    printf("{ .kind = STMT_SEMI, subtree_sz = %d }", n.subtree_sz);
    break;
  case FUN_END:
    printf("{ .kind = FUN_END, subtree_sz = %d }", n.subtree_sz);
    break;
  case FUN_ARROW:
    printf("{ .kind = FUN_ARROW, subtree_sz = %d }", n.subtree_sz);
    break;
  case BIND_NAME:
    printf("{ .kind = BIND_NAME, str = %.*s }", n.str.len, n.str.txt);
    break;
  case BIND_TY_JUDGE:
    printf("{ .kind = BIND_TY_JUDGE, subtree_sz = %d }", n.subtree_sz);
    break;
  case BIND_TY_SUBTY:
    printf("{ .kind = BIND_TY_SUBTY, subtree_sz = %d }", n.subtree_sz);
    break;
  case BIND_USE:
    printf("{ .kind = BIND_USE, str = %.*s }", n.str.len, n.str.txt);
    break;
  case LITERAL_INT:
    printf("{ .kind = BIND_USE, literal_int = %lu }", n.literal_int);
    break;
  case PREFIX_MINUS:
    printf("{ .kind = PREFIX_MINUS, subtree_sz = %d }", n.subtree_sz);
    break;
  case INFIX_MINUS:
    printf("{ .kind = INFIX_MINUS, subtree_sz = %d }", n.subtree_sz);
    break;
  case INFIX_PLUS:
    printf("{ .kind = INFIX_PLUS, subtree_sz = %d }", n.subtree_sz);
    break;
  }
}
