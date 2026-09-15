CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2

CPPFLAGS += -Iinclude
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wpedantic

BUILD_DIR := build
SEARCH_TEST_BIN := $(BUILD_DIR)/vitacheat_host_tests
PSV_TEST_BIN := $(BUILD_DIR)/vitacheat_psv_tests
ACTIVATION_TEST_BIN := $(BUILD_DIR)/vitacheat_activation_tests
TEST_BINS := $(SEARCH_TEST_BIN) $(PSV_TEST_BIN) $(ACTIVATION_TEST_BIN)
CORE_SOURCES := src/search.c src/legacy_psv.c src/menu_activation.c
HEADERS := include/vitacheat/search.h include/vitacheat/legacy_psv.h include/vitacheat/menu_activation.h

.PHONY: all test clean

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SEARCH_TEST_BIN): $(CORE_SOURCES) tests/host/test_search.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_search.c -o $(SEARCH_TEST_BIN)

$(PSV_TEST_BIN): $(CORE_SOURCES) tests/host/test_legacy_psv.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_legacy_psv.c -o $(PSV_TEST_BIN)

$(ACTIVATION_TEST_BIN): $(CORE_SOURCES) tests/host/test_menu_activation.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_menu_activation.c -o $(ACTIVATION_TEST_BIN)

test: $(TEST_BINS)
	./$(SEARCH_TEST_BIN)
	./$(PSV_TEST_BIN)
	./$(ACTIVATION_TEST_BIN)

clean:
	$(RM) -r $(BUILD_DIR)

