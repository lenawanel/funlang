#include "lexer.h"
#define __FUNLANG_COMMON_H_IMPL
#include "common.h"
#include <sys/types.h>
#include <unistd.h>

#ifndef __AFL_FUZZ_TESTCASE_LEN
  ssize_t fuzz_len;
  #define __AFL_FUZZ_TESTCASE_LEN fuzz_len
  unsigned char fuzz_buf[1024000];
  #define __AFL_FUZZ_TESTCASE_BUF fuzz_buf
  #define __AFL_FUZZ_INIT() void sync(void);
  #define __AFL_LOOP(x) ((fuzz_len = read(0, fuzz_buf, sizeof(fuzz_buf))) > 0 ? 1 : 0)
  #define __AFL_INIT() sync()
#endif

__AFL_FUZZ_INIT();

int main() {
    // anything else here, e.g. command line arguments, initialization, etc.

#ifdef __AFL_HAVE_MANUAL_CONTROL
  __AFL_INIT();
#endif

  unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;  // must be after __AFL_INIT
                                                 // and before __AFL_LOOP!

  while (__AFL_LOOP(10000)) {

    int len = __AFL_FUZZ_TESTCASE_LEN;  // don't use the macro directly in a
                                        // call!
    if (!len) continue;

    Lexer l = {.source = buf, .pos = 0, .end = len - 1 };

    LexRes res = lex(l);

    destroy_lexres(res);
  }

  return 0;
}
