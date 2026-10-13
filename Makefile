CC := gcc
CFLAGS := -std=c23 -Wall -Wextra -Werror -g -O1 -Iinclude -Itests -Isrc
SRC := src/rbtree.c src/pool.c
TSRC := tests/test_rbtree.c tests/fault_alloc.c
BIN := build/test_rbtree
FUZZBIN := build/fuzz
FAULTBIN := build/test_fault
POOLBIN := build/test_pool
all: $(BIN) $(FUZZBIN) $(FAULTBIN) $(POOLBIN)
$(BIN): $(SRC) $(TSRC) include/rbtree.h tests/fault_alloc.h src/pool.h src/rbtree_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) $(TSRC) -o $@ -lpthread
$(FUZZBIN): $(SRC) tests/fuzz.c tests/fault_alloc.c include/rbtree.h tests/fault_alloc.h src/pool.h src/rbtree_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) tests/fuzz.c tests/fault_alloc.c -o $@ -lpthread
$(FAULTBIN): $(SRC) tests/test_fault.c tests/fault_alloc.c include/rbtree.h tests/fault_alloc.h src/pool.h src/rbtree_internal.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) tests/test_fault.c tests/fault_alloc.c -o $@ -lpthread
$(POOLBIN): $(SRC) tests/test_pool.c tests/fault_alloc.c include/rbtree.h tests/fault_alloc.h src/pool.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) tests/test_pool.c tests/fault_alloc.c -o $@ -lpthread
fault: $(FAULTBIN)
	./$(FAULTBIN)
test: $(BIN) $(FUZZBIN) $(FAULTBIN) $(POOLBIN)
	./$(BIN) && ./$(FUZZBIN) 100000 12345 && ./$(FAULTBIN) && ./$(POOLBIN)
asan: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
asan: clean test
memcheck:
	$(MAKE) clean
	$(MAKE) all
	valgrind --leak-check=full --show-leak-kinds=all \
	--error-exitcode=1 ./$(BIN)
	valgrind --leak-check=full --show-leak-kinds=all \
	--error-exitcode=1 ./$(FUZZBIN) 20000 12345
	valgrind --leak-check=full --show-leak-kinds=all \
	--error-exitcode=1 ./$(FAULTBIN)
	valgrind --leak-check=full --show-leak-kinds=all \
	--error-exitcode=1 ./$(POOLBIN)
clean:
	rm -rf build
.PHONY: all test asan memcheck fault clean
