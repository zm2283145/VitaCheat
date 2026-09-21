CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2

CPPFLAGS += -Iinclude
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wpedantic

BUILD_DIR := build
SEARCH_TEST_BIN := $(BUILD_DIR)/vitacheat_host_tests
PSV_TEST_BIN := $(BUILD_DIR)/vitacheat_psv_tests
LEGACY_EMIT_TEST_BIN := $(BUILD_DIR)/vitacheat_legacy_emit_tests
LEGACY_PLAN_TEST_BIN := $(BUILD_DIR)/vitacheat_legacy_plan_tests
LEGACY_POINTER_PLAN_TEST_BIN := $(BUILD_DIR)/vitacheat_legacy_pointer_plan_tests
ACTIVATION_TEST_BIN := $(BUILD_DIR)/vitacheat_activation_tests
PAUSE_TEST_BIN := $(BUILD_DIR)/vitacheat_pause_tests
LEGACY_CORPUS_BIN := $(BUILD_DIR)/vitacheat_legacy_corpus
LEGACY_CORPUS_REVISION := bb8158a1c696914a8ea2299889d42ab9a57a3ab2
TEST_BINS := $(SEARCH_TEST_BIN) $(PSV_TEST_BIN) $(LEGACY_EMIT_TEST_BIN) \
	$(LEGACY_PLAN_TEST_BIN) \
	$(LEGACY_POINTER_PLAN_TEST_BIN) \
	$(ACTIVATION_TEST_BIN) $(PAUSE_TEST_BIN)
CORE_SOURCES := src/search.c src/legacy_psv.c src/legacy_plan.c \
	src/legacy_emit.c \
	src/menu_activation.c src/pause.c
HEADERS := include/vitacheat/search.h include/vitacheat/legacy_psv.h \
	include/vitacheat/legacy_emit.h \
	include/vitacheat/legacy_plan.h include/vitacheat/menu_activation.h \
	include/vitacheat/pause.h
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

.PHONY: all test legacy-corpus-test vita-self-test clean

all: $(TEST_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SEARCH_TEST_BIN): $(CORE_SOURCES) tests/host/test_search.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_search.c -o $(SEARCH_TEST_BIN)

$(PSV_TEST_BIN): $(CORE_SOURCES) tests/host/test_legacy_psv.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_legacy_psv.c -o $(PSV_TEST_BIN)

$(LEGACY_EMIT_TEST_BIN): $(CORE_SOURCES) tests/host/test_legacy_emit.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_legacy_emit.c -o $(LEGACY_EMIT_TEST_BIN)

$(LEGACY_PLAN_TEST_BIN): $(CORE_SOURCES) tests/host/test_legacy_plan.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_legacy_plan.c -o $(LEGACY_PLAN_TEST_BIN)

$(LEGACY_POINTER_PLAN_TEST_BIN): $(CORE_SOURCES) tests/host/test_legacy_pointer_plan.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_legacy_pointer_plan.c -o $(LEGACY_POINTER_PLAN_TEST_BIN)

$(ACTIVATION_TEST_BIN): $(CORE_SOURCES) tests/host/test_menu_activation.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_menu_activation.c -o $(ACTIVATION_TEST_BIN)

$(PAUSE_TEST_BIN): $(CORE_SOURCES) tests/host/test_pause.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_pause.c -o $(PAUSE_TEST_BIN)

$(LEGACY_CORPUS_BIN): $(CORE_SOURCES) tools/legacy_psv_corpus.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tools/legacy_psv_corpus.c -o $(LEGACY_CORPUS_BIN)

test: $(TEST_BINS) $(LEGACY_CORPUS_BIN)
	./$(SEARCH_TEST_BIN)
	./$(PSV_TEST_BIN)
	./$(LEGACY_EMIT_TEST_BIN)
	./$(LEGACY_PLAN_TEST_BIN)
	./$(LEGACY_POINTER_PLAN_TEST_BIN)
	./$(ACTIVATION_TEST_BIN)
	./$(PAUSE_TEST_BIN)
	$(LEGACY_CORPUS_BIN) --self-test

legacy-corpus-test: $(LEGACY_CORPUS_BIN)
	@test -n "$(LEGACY_CORPUS_DIR)" || \
		(echo "set LEGACY_CORPUS_DIR to a pinned r0ah/vitacheat checkout or db path" >&2; exit 2)
	@test "$$(git -C "$(LEGACY_CORPUS_DIR)" rev-parse HEAD)" = "$(LEGACY_CORPUS_REVISION)" || \
		(echo "legacy corpus must be checked out at $(LEGACY_CORPUS_REVISION)" >&2; exit 2)
	$(LEGACY_CORPUS_BIN) "$(LEGACY_CORPUS_DIR)"

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
