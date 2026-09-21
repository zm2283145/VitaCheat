CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2

CPPFLAGS += -Iinclude
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wpedantic

BUILD_DIR := build
SEARCH_TEST_BIN := $(BUILD_DIR)/vitacheat_host_tests
PSV_TEST_BIN := $(BUILD_DIR)/vitacheat_psv_tests
ACTIVATION_TEST_BIN := $(BUILD_DIR)/vitacheat_activation_tests
PAUSE_TEST_BIN := $(BUILD_DIR)/vitacheat_pause_tests
LAUNCH_BROKER_TEST_BIN := $(BUILD_DIR)/vitacheat_launch_broker_tests
TEST_BINS := $(SEARCH_TEST_BIN) $(PSV_TEST_BIN) $(ACTIVATION_TEST_BIN) \
	$(PAUSE_TEST_BIN) $(LAUNCH_BROKER_TEST_BIN)
CORE_SOURCES := src/search.c src/legacy_psv.c src/menu_activation.c src/pause.c \
	src/launch_broker.c
HEADERS := include/vitacheat/search.h include/vitacheat/legacy_psv.h \
	include/vitacheat/menu_activation.h include/vitacheat/pause.h \
	include/vitacheat/launch_broker.h
VITA_CC ?= arm-vita-eabi-gcc
VITA_ELF_CREATE ?= vita-elf-create
VITA_MAKE_FSELF ?= vita-make-fself
VITA_MKSFOEX ?= vita-mksfoex
VITA_PACK_VPK ?= vita-pack-vpk
VITA_BUILD_DIR := $(BUILD_DIR)/vita-self-test
VITA_COMMON_DIR ?= $(VITASDK)/share/gcc-arm-vita-eabi/samples/common
VITA_TITLE_ID := VCHT00001
VITA_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -I$(VITA_COMMON_DIR)
VITA_LDFLAGS := -Wl,-q -Wl,-z,nocopyreloc -Wl,--defsym=__sce_headroom=0x1000
VITA_LDLIBS := -lSceDisplay_stub -lSceCtrl_stub -lSceKernelThreadMgr_stub \
	-lSceProcessmgr_stub -lSceLibKernel_stub
VITA_OBJECTS := $(VITA_BUILD_DIR)/main.o $(VITA_BUILD_DIR)/search.o \
	$(VITA_BUILD_DIR)/menu_activation.o $(VITA_BUILD_DIR)/debugScreen.o

.PHONY: all test vita-self-test clean

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SEARCH_TEST_BIN): $(CORE_SOURCES) tests/host/test_search.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_search.c -o $(SEARCH_TEST_BIN)

$(PSV_TEST_BIN): $(CORE_SOURCES) tests/host/test_legacy_psv.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_legacy_psv.c -o $(PSV_TEST_BIN)

$(ACTIVATION_TEST_BIN): $(CORE_SOURCES) tests/host/test_menu_activation.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_menu_activation.c -o $(ACTIVATION_TEST_BIN)

$(PAUSE_TEST_BIN): $(CORE_SOURCES) tests/host/test_pause.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_pause.c -o $(PAUSE_TEST_BIN)

$(LAUNCH_BROKER_TEST_BIN): $(CORE_SOURCES) tests/host/test_launch_broker.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_launch_broker.c -o $(LAUNCH_BROKER_TEST_BIN)

test: $(TEST_BINS)
	./$(SEARCH_TEST_BIN)
	./$(PSV_TEST_BIN)
	./$(ACTIVATION_TEST_BIN)
	./$(PAUSE_TEST_BIN)
	./$(LAUNCH_BROKER_TEST_BIN)

vita-self-test: $(VITA_BUILD_DIR)/vitacheat-self-test.vpk

$(VITA_BUILD_DIR)/main.o: vita-self-test/main.c include/vitacheat/search.h include/vitacheat/menu_activation.h $(VITA_COMMON_DIR)/debugScreen.h $(VITA_COMMON_DIR)/debugScreen_custom.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/search.o: src/search.c include/vitacheat/search.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/menu_activation.o: src/menu_activation.c include/vitacheat/menu_activation.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/debugScreen.o: $(VITA_COMMON_DIR)/debugScreen.c $(VITA_COMMON_DIR)/debugScreen.h $(VITA_COMMON_DIR)/debugScreen_custom.h $(VITA_COMMON_DIR)/debugScreenFont.c Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR):
	mkdir -p $(VITA_BUILD_DIR)

$(VITA_BUILD_DIR)/vitacheat-self-test.elf: $(VITA_OBJECTS)
	$(VITA_CC) $(VITA_LDFLAGS) $^ $(VITA_LDLIBS) -o $@

$(VITA_BUILD_DIR)/vitacheat-self-test.velf: $(VITA_BUILD_DIR)/vitacheat-self-test.elf
	$(VITA_ELF_CREATE) $< $@

$(VITA_BUILD_DIR)/eboot.bin: $(VITA_BUILD_DIR)/vitacheat-self-test.velf
	$(VITA_MAKE_FSELF) $< $@

$(VITA_BUILD_DIR)/param.sfo: Makefile | $(VITA_BUILD_DIR)
	$(VITA_MKSFOEX) -s TITLE_ID=$(VITA_TITLE_ID) 'VitaCheat safe self-test' $@

$(VITA_BUILD_DIR)/vitacheat-self-test.vpk: $(VITA_BUILD_DIR)/param.sfo $(VITA_BUILD_DIR)/eboot.bin
	$(VITA_PACK_VPK) -s $(VITA_BUILD_DIR)/param.sfo -b $(VITA_BUILD_DIR)/eboot.bin $@

clean:
	$(RM) -r $(BUILD_DIR)
