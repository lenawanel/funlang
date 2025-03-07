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

#define TERM(misc, node, token) (0x80000000 | ((node) << 8) | (token) | (misc))
#define INTRO(misc, node, token) (0x20000000 | TERM(misc, node, token))
#define CLOSE(misc, node, token) (0x40000000 | ((node) << 8) | (token) | (misc))
#define CLOSE_TERM(misc, node, token) (0x40000000 | TERM(misc, node, token))
#define CONTENT_LIT                                                            \
  0x1000000 /* is there a literal associated with the token                    \
             */
#define CONTENT_STR                                                            \
  0x2000000 /* is there a string associated with the token                     \
             */

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

  // WCLSE = 0x10000000, // waiting for a close? y/n

  CLOSE_FN_ARROW = CLOSE(0, FUN_ARROW, TOK_KW_ARROW),     // add ->
  CLOSE_TY_SUBTY = CLOSE(0, BIND_TY_SUBTY, TOK_KW_SUBTY), // add <:
  CLOSE_TY_JUDGE = CLOSE(0, BIND_TY_JUDGE, ':'),          // add :
  CLOSE_PFX_SUB  = CLOSE(0, PREFIX_MINUS, '-'),           // add prefix -
  CLOSE_IFX_ADD  = CLOSE(0, INFIX_PLUS, '+'),             // add inifix +

  TERM_FN_BL_OPEN  = TERM(0, FUN_BLOCK, '{'),                     // {
  TERM_CLOSE_FN    = CLOSE_TERM(0, FUN_END, '}'),                 // }
  TERM_BIND_NAME   = TERM(CONTENT_STR, BIND_NAME, TOK_VAL_ID),    // name
  TERM_BIND_USE    = TERM(CONTENT_STR, BIND_USE, TOK_VAL_ID),     // name
  TERM_LITERAL_INT = TERM(CONTENT_LIT, LITERAL_INT, TOK_LIT_INT), // 12314
  TERM_ASSGN       = TERM(0, STMT_ASSGN_EQ, '='),                 // =

  INTRO_LET_BIND     = INTRO(0, STMT_LET_BIND, TOK_KW_LET),   // let
  INTRO_RETURN       = INTRO(0, STMT_RETURN, TOK_KW_RETRN),   // return
  INTRO_FN_INT       = INTRO(0, FUN_INT, TOK_KW_FN),          // fn
  INTRO_PREFIX_MINUS = INTRO(0, PREFIX_MINUS, '-'),           // -
  INTRO_CLOSE_SEMI   = INTRO(CLOSE(0, 0, 0), STMT_SEMI, ';'), // ;
  INTRO_ADD_CONT     = INTRO(0, INFIX_PLUS, '+'),             // +

  INTRO_FN_ARROW = INTRO(0, FUN_ARROW, TOK_KW_ARROW),     // ->
  INTRO_SUBTY    = INTRO(0, BIND_TY_SUBTY, TOK_KW_SUBTY), // <:
  INTRO_JUDGE    = INTRO(0, BIND_TY_JUDGE, ':'),          // :

  TERM_BUILTIN_TY  = TERM(0, BUILTIN_TY, TYPE), // u32, s32, ...
  TERM_NAME_USE_TY = TERM(CONTENT_STR, BIND_TY_USE, TOK_TYPE_ID), // Atype, ...

  INTRO_IMP_ARGL    = INTRO(0, IMP_ARGLIST_BEG, '['),                // [
  INTRO_IMP_ARG     = INTRO(CONTENT_STR, BIND_TY_NAME, TOK_TYPE_ID), // Tname
  INTRO_IMP_ARG_SEP = INTRO(0, IMP_ARGLIST_SEP, ','),                // ,
  TERM_IMP_ARGL_END = CLOSE_TERM(0, IMP_ARGLIST_END, ']'),           // ]

  INTRO_EXP_ARGL    = INTRO(0, EXP_ARGLIST_BEG, '('),            // (
  INTRO_EXP_ARG     = INTRO(CONTENT_STR, BIND_NAME, TOK_VAL_ID), // name
  INTRO_EXP_ARG_SEP = INTRO(0, EXP_ARGLIST_SEP, ','),            // ,
  TERM_EXP_ARGL_END = CLOSE_TERM(0, EXP_ARGLIST_END, ')'),       // )
} PStateKind;

#undef CLOSE
#undef TERM
#undef INTRO
#define CLOSE 0x40000000     /* bracketing closer? y/n */
#define INTRO 0x20000000     /* introducer token? y/n */
#define TERM 0x80000000      /* terminal? y/n */
#define CHOICE_END 0x4000000 /* end of a sequence of optional rules? y/n */
#define CHOICE 0x8000000     /* optional rule? y/n */

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

  char builtin_tys[] = {(char)TOK_KW_U8,  (char)TOK_KW_U16, (char)TOK_KW_U32,
                        (char)TOK_KW_U64, (char)TOK_KW_S8,  (char)TOK_KW_S16,
                        (char)TOK_KW_S32, (char)TOK_KW_S64};

  if ((term & STATIC_MASK) == TERM_BUILTIN_TY)
    return memchr(builtin_tys, (int)tag & 0xff,
                  sizeof(builtin_tys) / sizeof(char));

  return (tag & 0xff) == (term & 0xff);
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
  {
    n->literal_int = lr->lits[word.as_lit_idx >> 8];
    printf("word.as_lit_idx >> 8 = %d\n", word.as_lit_idx>>8);
    printf("n->literal_int = %lu\n", n->literal_int);
    
  }

  n->kind = (state >> 8) & 0xff;
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
    printf("rule & STATIC_MASK = 0x%x, word.tag = 0x%x\n",
           focus.kind & STATIC_MASK, word.tag & 0xff);
    if (i > 90) abort();

    if ((focus.kind & TERM) && term_matches(focus.kind, word.tag))
    {

      if (focus.kind & CHOICE) stack.len -= focus.chsz;

      term_into_node(word, focus.kind, &n, &lr, &tree);

      if (focus.kind & INTRO)
      {
        switch (focus.kind & STATIC_MASK)
        {
        case INTRO_FN_INT: focus.kind = FUNC; break;
        case INTRO_EXP_ARGL:

          focus.kind = TERM_EXP_ARGL_END;
          focus.tpos = tree.buf.len;
          co_push(&stack, focus);

          focus.kind = INTRO_EXP_ARG | CHOICE;
          focus.chsz = 0;
          break;
        case INTRO_EXP_ARG:

          focus.kind = INTRO_EXP_ARG_SEP | CHOICE;
          co_push(&stack, focus);
          focus.kind = INTRO_JUDGE;
          break;
        case INTRO_EXP_ARG_SEP:

          focus.kind = INTRO_EXP_ARG;
          focus.chsz = 0;
          break;
        case INTRO_JUDGE:

          focus.kind         = CLOSE_TY_JUDGE;
          focus.dclo.nod_pos = tree.buf.len;
          focus.dclo.tok_pos = word.pos;
          co_push(&stack, focus);
          focus.kind = TYPE;
          // TODO: hacky
          goto delay_closing;
        case INTRO_FN_ARROW:

          focus.kind         = TYPE;
          focus.dclo.nod_pos = tree.buf.len;
          focus.dclo.tok_pos = word.pos;
          break;
        case INTRO_LET_BIND:

          // TODO: maybe move semi stuff here
          focus.kind = EXPRESSION;
          co_push(&stack, focus);

          focus.kind = TERM_ASSGN;
          co_push(&stack, focus);

          focus.kind = INTRO_JUDGE | CHOICE;
          focus.tpos = tree.buf.len;
          co_push(&stack, focus);

          focus.kind = TERM_BIND_NAME;
          break;
        case INTRO_ADD_CONT:
          focus.kind         = CLOSE_IFX_ADD;
          focus.dclo.nod_pos = tree.buf.len;
          focus.dclo.tok_pos = word.pos;
          co_push(&stack, focus);

          focus.kind = EXPRESSION;
          // TODO: hacky
          goto delay_closing;
        case INTRO_PREFIX_MINUS:
          focus.kind         = CLOSE_PFX_SUB;
          focus.dclo.nod_pos = tree.buf.len;
          focus.dclo.tok_pos = word.pos;
          co_push(&stack, focus);

          focus.kind = EXPRESSION;
          // TODO: hacky
          goto delay_closing;
        case INTRO_RETURN: focus.kind = EXPRESSION; break;
        case INTRO_CLOSE_SEMI:

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
      case ROOT: focus.kind = INTRO_FN_INT; break;
      case FUNC:
        focus.kind = TERM_CLOSE_FN;
        focus.tpos = tree.buf.len;
        co_push(&stack, focus);

        focus.kind = STATEMENT;
        co_push(&stack, focus);

        focus.kind = TERM_FN_BL_OPEN;
        focus.tpos = tree.buf.len;
        co_push(&stack, focus);

        focus.kind = INTRO_FN_ARROW | CHOICE;
        focus.chsz = 0;
        co_push(&stack, focus);

        focus.kind = INTRO_EXP_ARGL;
        co_push(&stack, focus);

        focus.kind = INTRO_IMP_ARGL | CHOICE;
        focus.chsz = 0;
        co_push(&stack, focus);

        focus.kind = TERM_BIND_NAME;
        break;
      case TYPE:
        focus.kind = TERM_NAME_USE_TY | CHOICE_END;
        focus.chsz = 1;
        co_push(&stack, focus);

        focus.kind = TERM_BUILTIN_TY | CHOICE;
        focus.chsz = 1;
        break;

      case STATEMENT:
        // TODO: 0 stmts, ...
        focus.kind = INTRO_LET_BIND | CHOICE;
        focus.chsz = 0;
        co_push(&stack, focus);

        focus.kind = INTRO_RETURN | CHOICE;
        focus.chsz = 1;
        break;
      case EXPRESSION:
        focus.kind = EXPR_CONT;
        co_push(&stack, focus);

        focus.kind = INTRO_PREFIX_MINUS | CHOICE_END;
        focus.chsz = 2;
        co_push(&stack, focus);

        focus.kind = TERM_LITERAL_INT | CHOICE;
        focus.chsz = 1;
        co_push(&stack, focus);

        focus.kind = TERM_BIND_USE | CHOICE;
        focus.chsz = 2;
        break;
      case EXPR_CONT:
        focus.kind = INTRO_CLOSE_SEMI | CHOICE;
        focus.chsz = 0;
        co_push(&stack, focus);

        focus.kind = INTRO_ADD_CONT | CHOICE;
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
      printf("focus.kind & STATIC_MASK = 0x%x, word = 0x%x\n",
             focus.kind & STATIC_MASK, word.tag & 0xff);
      assert(false && "encountered parse error?");
    }
  }

  free(stack.buffer);

  ParseRes res = {
      .size = tree.buf.len, .tree = tree.buf.buffer, .names = tree.names};

  return res;
}

#define PNodeKindMacroFormat1(V, ...)                                          \
  case V: printf("{ .kind = " #V " }"); break;
#define PNodeKindMacroFormat2(V, _, S, F)                                      \
  case V: printf("{ .kind = " #V ", ." #F " = " S " }", n.F); break;
#define PNodeKindMacroFormat3(V, _, S, F1, F2)                                 \
  case V: printf("{ .kind = " #V ", .str = " S " }", n.F1, n.F2); break;

void print_pnode(PNode n)
{
  switch (n.kind)
  {
    PNodeKindMacro(PNodeKindMacroFormat1, PNodeKindMacroFormat2,
                   PNodeKindMacroFormat3)
  }
}
