#ifndef _TYPER_H
#define _TYPER_H

#include "parser.h"
#include "types.h"

typedef struct {
  enum {
    INBUILT,
  } kind;
  union {
    InbuiltType as_inbuilt;
  };
} Type;

#endif // _TYPER_H
