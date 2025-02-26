CFLAGS = -O1 -march=native -ggdb -Wall -Wextra -Wconversion -Wdouble-promotion -std=c23 \
     	 -fsanitize=undefined,address -pipe

BUILD   = ./build
SRC     = ./src

CC      = gcc

objects = $(BUILD)/hashtable.o $(BUILD)/lexer.o
     	 
$(BUILD)/parser: $(SRC)/parser.c $(objects) $(SRC)/common.h $(BUILD)
	$(CC) $(CFLAGS) $(SRC)/parser.c $(objects) -o $@ 

$(objects): $(BUILD)/%.o: $(SRC)/%.c $(SRC)/%.h $(SRC)/common.h $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/lexer_harness: $(BUILD)/afl_lexer.o $(BUILD)
	afl-clang-lto -std=c23 -O3 -march=native -DNDEBUG $(BUILD)/afl_lexer.o $(SRC)/lexer_harness.c -o $@

$(BUILD)/afl_lexer.o: $(SRC)/lexer.c $(SRC)/lexer.h $(SRC)/common.h $(BUILD)
	afl-clang-lto -std=c23 -O3 -march=native -DNDEBUG -c $(SRC)/lexer.c -o $@

$(BUILD):
	mkdir $(BUILD)

.PHONY: clean fuzz

fuzz: build/lexer_harness

clean:
	rm -rf build
