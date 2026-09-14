CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2

CPPFLAGS += -Iinclude
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wpedantic

BUILD_DIR := build
TEST_BIN := $(BUILD_DIR)/vitacheat_host_tests
SOURCES := src/search.c tests/host/test_search.c
HEADERS := include/vitacheat/search.h

.PHONY: all test clean

all: $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TEST_BIN): $(SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	$(RM) -r $(BUILD_DIR)

