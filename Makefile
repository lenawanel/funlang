CFLAGS = -march=native -ggdb -Wall -Wextra -Wconversion -Wdouble-promotion -std=c23 \
     	 -fsanitize=undefined,address -pipe

BUILD   = ./build
SRC     = ./src

CC      = clang

objects = $(BUILD)/hashtable.o $(BUILD)/lexer.o $(BUILD)/parser.o
     	 
$(BUILD)/typer: $(SRC)/typer.h $(SRC)/typer.c $(objects) $(SRC)/common.h $(BUILD)
	$(CC) $(CFLAGS) $(SRC)/typer.c $(objects) -o $@ 

$(objects): $(BUILD)/%.o: $(SRC)/%.c $(SRC)/%.h $(SRC)/common.h $(BUILD)
	$(CC) -O1 $(CFLAGS) -c $< -o $@

$(BUILD)/lexer_harness: $(BUILD)/afl_lexer.o $(BUILD)
	afl-clang-lto -std=c23 -O3 -march=native -DNDEBUG $(BUILD)/afl_lexer.o $(SRC)/lexer_harness.c -o $@

$(BUILD)/afl_lexer.o: $(SRC)/lexer.c $(SRC)/lexer.h $(SRC)/common.h $(BUILD)
	afl-clang-lto -std=c23 -O3 -march=native -DNDEBUG -c $(SRC)/lexer.c -o $@

$(BUILD)/lexer_harness_cmplog: $(SRC)/lexer_harness.c $(BUILD)/afl_lexer_cmplog.o $(BUILD)
	AFL_LLVM_CMPLOG=1 afl-clang-lto -std=c23 -O3 -march=native -DNDEBUG $< $(BUILD)/afl_lexer_cmplog.o -o $@

$(BUILD)/afl_lexer_cmplog.o: $(SRC)/lexer.c $(SRC)/lexer.h $(SRC)/common.h $(BUILD)
	AFL_LLVM_CMPLOG=1 afl-clang-lto -std=c23 -O3 -march=native -DNDEBUG -c $< -o $@

$(BUILD):
	mkdir $(BUILD)

.PHONY: clean fuzz

fuzz: $(BUILD)/lexer_harness $(BUILD)/lexer_harness_cmplog

clean:
	rm -rf build
	rm -rf afl-out
