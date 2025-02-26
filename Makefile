CFLAGS = -ggdb -Wall -Wextra -Wconversion -Wdouble-promotion -std=c23 \
     	 -fsanitize=undefined,address -pipe -fanalyzer
     	 
build/parser: parser.c build/lexer.o  build/hashtable.o common.h build
	gcc $(CFLAGS) parser.c -o build/parser build/lexer.o build/hashtable.o

build/lexer.o: lexer.c lexer.h common.h build
	gcc $(CFLAGS) -c lexer.c -o build/lexer.o

build/hashtable.o: hashtable.c hashtable.h common.h build
	gcc $(CFLAGS) -c hashtable.c -o build/hashtable.o

build/lexer_harness: build/afl_lexer.o build
	afl-clang-lto -std=c23 -O3 -march=native -DNDEBUG build/afl_lexer.o lexer_harness.c -o build/lexer_harness

build/afl_lexer.o: lexer.c lexer.h common.h build
	afl-clang-lto -std=c23 -O3 -march=native -DNDEBUG -c lexer.c -o build/afl_lexer.o

build:
	mkdir build

.PHONY: clean fuzz

fuzz: build/lexer_harness

clean:
	rm -rf build
