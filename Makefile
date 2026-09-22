CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2

CPPFLAGS += -Iinclude
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wpedantic
ANALYZER_FLAGS ?= -fanalyzer

BUILD_DIR := build
SEARCH_TEST_BIN := $(BUILD_DIR)/vitacheat_host_tests
PSV_TEST_BIN := $(BUILD_DIR)/vitacheat_psv_tests
ACTIVATION_TEST_BIN := $(BUILD_DIR)/vitacheat_activation_tests
PAUSE_TEST_BIN := $(BUILD_DIR)/vitacheat_pause_tests
LAUNCH_BROKER_TEST_BIN := $(BUILD_DIR)/vitacheat_launch_broker_tests
LAUNCH_SERVICE_TEST_BIN := $(BUILD_DIR)/vitacheat_launch_service_tests
QUICK_MENU_LAUNCHER_TEST_BIN := $(BUILD_DIR)/vitacheat_quick_menu_launcher_tests
LAUNCH_CLAIMANT_TEST_BIN := $(BUILD_DIR)/vitacheat_launch_claimant_tests
MENU_COORDINATOR_TEST_BIN := $(BUILD_DIR)/vitacheat_menu_coordinator_tests
TARGET_ATTESTATION_TEST_BIN := $(BUILD_DIR)/vitacheat_target_attestation_tests
MEMORY_READ_TEST_BIN := $(BUILD_DIR)/vitacheat_memory_read_tests
MEMORY_SERVICE_TEST_BIN := $(BUILD_DIR)/vitacheat_memory_service_tests
LAUNCH_SERVICE_FUZZ_SMOKE_BIN := $(BUILD_DIR)/vitacheat_launch_service_fuzz_smoke
QUICK_MENU_LAUNCHER_FUZZ_SMOKE_BIN := $(BUILD_DIR)/vitacheat_quick_menu_launcher_fuzz_smoke
LAUNCH_CLAIMANT_FUZZ_SMOKE_BIN := $(BUILD_DIR)/vitacheat_launch_claimant_fuzz_smoke
MENU_COORDINATOR_FUZZ_SMOKE_BIN := $(BUILD_DIR)/vitacheat_menu_coordinator_fuzz_smoke
TARGET_ATTESTATION_FUZZ_SMOKE_BIN := $(BUILD_DIR)/vitacheat_target_attestation_fuzz_smoke
MEMORY_SERVICE_FUZZ_SMOKE_BIN := $(BUILD_DIR)/vitacheat_memory_service_fuzz_smoke
HARDWARE_GATE_TEST_BIN := $(BUILD_DIR)/vitacheat_hardware_gate_tests
HARDWARE_GATE_FUZZ_SMOKE_BIN := $(BUILD_DIR)/vitacheat_hardware_gate_fuzz_smoke
HARDWARE_GATE_SOURCE := experimental/hardware-gate/src/hardware_gate.c
HARDWARE_GATE_HEADER := experimental/hardware-gate/include/vitacheat/hardware_gate.h
HARDWARE_GATE_CPPFLAGS := -Iexperimental/hardware-gate/include
FOREIGN_GATE_TEST_BIN := $(BUILD_DIR)/vitacheat_foreign_target_gate_tests
FOREIGN_STARTUP_TEST_BIN := $(BUILD_DIR)/vitacheat_foreign_target_startup_tests
FOREIGN_GATE_FUZZ_SMOKE_BIN := $(BUILD_DIR)/vitacheat_foreign_target_gate_fuzz_smoke
FOREIGN_GATE_SOURCES := experimental/foreign-target-gate/src/foreign_target_gate.c \
	experimental/foreign-target-gate/src/foreign_target_startup.c
FOREIGN_GATE_HEADERS := experimental/foreign-target-gate/include/vitacheat/foreign_target_gate.h \
	experimental/foreign-target-gate/include/vitacheat/foreign_target_startup.h
FOREIGN_GATE_CPPFLAGS := -Iexperimental/foreign-target-gate/include
TEST_BINS := $(SEARCH_TEST_BIN) $(PSV_TEST_BIN) $(ACTIVATION_TEST_BIN) \
	$(PAUSE_TEST_BIN) $(LAUNCH_BROKER_TEST_BIN) $(LAUNCH_SERVICE_TEST_BIN) \
	$(QUICK_MENU_LAUNCHER_TEST_BIN) $(LAUNCH_CLAIMANT_TEST_BIN) \
	$(MENU_COORDINATOR_TEST_BIN) $(TARGET_ATTESTATION_TEST_BIN) \
	$(MEMORY_READ_TEST_BIN) $(MEMORY_SERVICE_TEST_BIN)
CORE_SOURCES := src/search.c src/legacy_psv.c src/menu_activation.c src/pause.c \
	src/launch_broker.c src/launch_service.c src/quick_menu_launcher.c \
	src/launch_claimant.c src/menu_coordinator.c src/target_attestation.c \
	src/memory_read.c src/memory_service.c
HEADERS := include/vitacheat/search.h include/vitacheat/legacy_psv.h \
	include/vitacheat/menu_activation.h include/vitacheat/pause.h \
	include/vitacheat/launch_broker.h include/vitacheat/launch_service.h \
	include/vitacheat/quick_menu_launcher.h include/vitacheat/launch_claimant.h \
	include/vitacheat/menu_coordinator.h include/vitacheat/target_attestation.h \
	include/vitacheat/memory_read.h include/vitacheat/memory_service.h
VITA_CC ?= arm-vita-eabi-gcc
VITA_ELF_CREATE ?= vita-elf-create
VITA_MAKE_FSELF ?= vita-make-fself
VITA_MKSFOEX ?= vita-mksfoex
VITA_PACK_VPK ?= vita-pack-vpk
VITA_BUILD_DIR := $(BUILD_DIR)/vita-self-test
VITA_COMMON_DIR ?= $(VITASDK)/share/gcc-arm-vita-eabi/samples/common
VITA_TITLE_ID := VCHT00001
VITA_CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -Iinclude -I$(VITA_COMMON_DIR)
VITA_PORTABLE_CFLAGS := $(VITA_CFLAGS) -Wpedantic
VITA_LDFLAGS := -Wl,-q -Wl,-z,nocopyreloc -Wl,--defsym=__sce_headroom=0x1000
VITA_LDLIBS := -lSceDisplay_stub -lSceCtrl_stub -lSceKernelThreadMgr_stub \
	-lSceProcessmgr_stub -lSceLibKernel_stub
VITA_OBJECTS := $(VITA_BUILD_DIR)/main.o $(VITA_BUILD_DIR)/search.o \
	$(VITA_BUILD_DIR)/menu_activation.o $(VITA_BUILD_DIR)/debugScreen.o
VITA_PORTABLE_CHECK_OBJECTS := $(VITA_BUILD_DIR)/pause-portable.o \
	$(VITA_BUILD_DIR)/launch-claimant-portable.o \
	$(VITA_BUILD_DIR)/menu-coordinator-portable.o \
	$(VITA_BUILD_DIR)/menu-coordinator-fuzz-portable.o \
	$(VITA_BUILD_DIR)/target-attestation-portable.o \
	$(VITA_BUILD_DIR)/target-attestation-fuzz-portable.o \
	$(VITA_BUILD_DIR)/memory-read-portable.o \
	$(VITA_BUILD_DIR)/memory-service-portable.o \
	$(VITA_BUILD_DIR)/memory-service-fuzz-portable.o

.PHONY: all test launch-service-fuzz-smoke quick-menu-launcher-fuzz-smoke \
	launch-claimant-fuzz-smoke menu-coordinator-fuzz-smoke \
	target-attestation-fuzz-smoke memory-service-fuzz-smoke \
	hardware-gate-test hardware-gate-fuzz-smoke hardware-gate-analyze \
	foreign-target-gate-test foreign-target-gate-fuzz-smoke \
	foreign-target-gate-analyze \
	analyze vita-self-test clean

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

$(LAUNCH_SERVICE_TEST_BIN): $(CORE_SOURCES) tests/host/test_launch_service.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_launch_service.c -o $(LAUNCH_SERVICE_TEST_BIN)

$(QUICK_MENU_LAUNCHER_TEST_BIN): $(CORE_SOURCES) tests/host/test_quick_menu_launcher.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_quick_menu_launcher.c -o $(QUICK_MENU_LAUNCHER_TEST_BIN)

$(LAUNCH_CLAIMANT_TEST_BIN): $(CORE_SOURCES) tests/host/test_launch_claimant.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_launch_claimant.c -o $(LAUNCH_CLAIMANT_TEST_BIN)

$(MENU_COORDINATOR_TEST_BIN): $(CORE_SOURCES) tests/host/test_menu_coordinator.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_menu_coordinator.c -o $(MENU_COORDINATOR_TEST_BIN)

$(TARGET_ATTESTATION_TEST_BIN): $(CORE_SOURCES) tests/host/test_target_attestation.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_target_attestation.c -o $(TARGET_ATTESTATION_TEST_BIN)

$(MEMORY_READ_TEST_BIN): $(CORE_SOURCES) tests/host/test_memory_read.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_memory_read.c -o $(MEMORY_READ_TEST_BIN)

$(MEMORY_SERVICE_TEST_BIN): $(CORE_SOURCES) tests/host/test_memory_service.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE_SOURCES) tests/host/test_memory_service.c -o $(MEMORY_SERVICE_TEST_BIN)

$(LAUNCH_SERVICE_FUZZ_SMOKE_BIN): $(CORE_SOURCES) tests/fuzz/fuzz_launch_service.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVC_LAUNCH_SERVICE_FUZZ_STANDALONE \
		$(CORE_SOURCES) tests/fuzz/fuzz_launch_service.c -o $@

$(QUICK_MENU_LAUNCHER_FUZZ_SMOKE_BIN): $(CORE_SOURCES) tests/fuzz/fuzz_quick_menu_launcher.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVC_QUICK_MENU_LAUNCHER_FUZZ_STANDALONE \
		$(CORE_SOURCES) tests/fuzz/fuzz_quick_menu_launcher.c -o $@

$(LAUNCH_CLAIMANT_FUZZ_SMOKE_BIN): $(CORE_SOURCES) tests/fuzz/fuzz_launch_claimant.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVC_LAUNCH_CLAIMANT_FUZZ_STANDALONE \
		$(CORE_SOURCES) tests/fuzz/fuzz_launch_claimant.c -o $@

$(MENU_COORDINATOR_FUZZ_SMOKE_BIN): $(CORE_SOURCES) tests/fuzz/fuzz_menu_coordinator.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVC_MENU_COORDINATOR_FUZZ_STANDALONE \
		$(CORE_SOURCES) tests/fuzz/fuzz_menu_coordinator.c -o $@

$(TARGET_ATTESTATION_FUZZ_SMOKE_BIN): $(CORE_SOURCES) tests/fuzz/fuzz_target_attestation.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVC_TARGET_ATTESTATION_FUZZ_STANDALONE \
		$(CORE_SOURCES) tests/fuzz/fuzz_target_attestation.c -o $@

$(MEMORY_SERVICE_FUZZ_SMOKE_BIN): $(CORE_SOURCES) tests/fuzz/fuzz_memory_service.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVC_MEMORY_SERVICE_FUZZ_STANDALONE \
		$(CORE_SOURCES) tests/fuzz/fuzz_memory_service.c -o $@

test: $(TEST_BINS)
	./$(SEARCH_TEST_BIN)
	./$(PSV_TEST_BIN)
	./$(ACTIVATION_TEST_BIN)
	./$(PAUSE_TEST_BIN)
	./$(LAUNCH_BROKER_TEST_BIN)
	./$(LAUNCH_SERVICE_TEST_BIN)
	./$(QUICK_MENU_LAUNCHER_TEST_BIN)
	./$(LAUNCH_CLAIMANT_TEST_BIN)
	./$(MENU_COORDINATOR_TEST_BIN)
	./$(TARGET_ATTESTATION_TEST_BIN)
	./$(MEMORY_READ_TEST_BIN)
	./$(MEMORY_SERVICE_TEST_BIN)

launch-service-fuzz-smoke: $(LAUNCH_SERVICE_FUZZ_SMOKE_BIN)
	./$(LAUNCH_SERVICE_FUZZ_SMOKE_BIN)

quick-menu-launcher-fuzz-smoke: $(QUICK_MENU_LAUNCHER_FUZZ_SMOKE_BIN)
	./$(QUICK_MENU_LAUNCHER_FUZZ_SMOKE_BIN)

launch-claimant-fuzz-smoke: $(LAUNCH_CLAIMANT_FUZZ_SMOKE_BIN)
	./$(LAUNCH_CLAIMANT_FUZZ_SMOKE_BIN)

menu-coordinator-fuzz-smoke: $(MENU_COORDINATOR_FUZZ_SMOKE_BIN)
	./$(MENU_COORDINATOR_FUZZ_SMOKE_BIN)

target-attestation-fuzz-smoke: $(TARGET_ATTESTATION_FUZZ_SMOKE_BIN)
	./$(TARGET_ATTESTATION_FUZZ_SMOKE_BIN)

memory-service-fuzz-smoke: $(MEMORY_SERVICE_FUZZ_SMOKE_BIN)
	./$(MEMORY_SERVICE_FUZZ_SMOKE_BIN)

$(HARDWARE_GATE_TEST_BIN): $(HARDWARE_GATE_SOURCE) \
		experimental/hardware-gate/tests/test_hardware_gate.c \
		$(HARDWARE_GATE_HEADER) | $(BUILD_DIR)
	$(CC) $(HARDWARE_GATE_CPPFLAGS) $(CFLAGS) \
		$(HARDWARE_GATE_SOURCE) \
		experimental/hardware-gate/tests/test_hardware_gate.c -o $@

hardware-gate-test: $(HARDWARE_GATE_TEST_BIN)
	./$(HARDWARE_GATE_TEST_BIN)

$(HARDWARE_GATE_FUZZ_SMOKE_BIN): $(HARDWARE_GATE_SOURCE) \
		experimental/hardware-gate/fuzz/fuzz_hardware_gate.c \
		$(HARDWARE_GATE_HEADER) | $(BUILD_DIR)
	$(CC) $(HARDWARE_GATE_CPPFLAGS) $(CFLAGS) \
		-DVC_HG_FUZZ_STANDALONE \
		$(HARDWARE_GATE_SOURCE) \
		experimental/hardware-gate/fuzz/fuzz_hardware_gate.c -o $@

hardware-gate-fuzz-smoke: $(HARDWARE_GATE_FUZZ_SMOKE_BIN)
	./$(HARDWARE_GATE_FUZZ_SMOKE_BIN)

hardware-gate-analyze:
	$(CC) $(HARDWARE_GATE_CPPFLAGS) $(CFLAGS) $(ANALYZER_FLAGS) \
		-fsyntax-only $(HARDWARE_GATE_SOURCE)

$(FOREIGN_GATE_TEST_BIN): $(FOREIGN_GATE_SOURCES) \
		experimental/foreign-target-gate/tests/test_foreign_target_gate.c \
		$(FOREIGN_GATE_HEADERS) | $(BUILD_DIR)
	$(CC) $(FOREIGN_GATE_CPPFLAGS) $(CFLAGS) \
		$(FOREIGN_GATE_SOURCES) \
		experimental/foreign-target-gate/tests/test_foreign_target_gate.c -o $@

$(FOREIGN_STARTUP_TEST_BIN): \
		experimental/foreign-target-gate/src/foreign_target_startup.c \
		experimental/foreign-target-gate/tests/test_foreign_target_startup.c \
		experimental/foreign-target-gate/include/vitacheat/foreign_target_startup.h \
		| $(BUILD_DIR)
	$(CC) $(FOREIGN_GATE_CPPFLAGS) $(CFLAGS) \
		experimental/foreign-target-gate/src/foreign_target_startup.c \
		experimental/foreign-target-gate/tests/test_foreign_target_startup.c -o $@

foreign-target-gate-test: $(FOREIGN_GATE_TEST_BIN) $(FOREIGN_STARTUP_TEST_BIN)
	./$(FOREIGN_GATE_TEST_BIN)
	./$(FOREIGN_STARTUP_TEST_BIN)

$(FOREIGN_GATE_FUZZ_SMOKE_BIN): $(FOREIGN_GATE_SOURCES) \
		experimental/foreign-target-gate/fuzz/fuzz_foreign_target_gate.c \
		$(FOREIGN_GATE_HEADERS) | $(BUILD_DIR)
	$(CC) $(FOREIGN_GATE_CPPFLAGS) $(CFLAGS) \
		-DVC_FTG_FUZZ_STANDALONE \
		$(FOREIGN_GATE_SOURCES) \
		experimental/foreign-target-gate/fuzz/fuzz_foreign_target_gate.c -o $@

foreign-target-gate-fuzz-smoke: $(FOREIGN_GATE_FUZZ_SMOKE_BIN)
	./$(FOREIGN_GATE_FUZZ_SMOKE_BIN)

foreign-target-gate-analyze:
	$(CC) $(FOREIGN_GATE_CPPFLAGS) $(CFLAGS) $(ANALYZER_FLAGS) \
		-fsyntax-only $(FOREIGN_GATE_SOURCES)

analyze:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ANALYZER_FLAGS) -fsyntax-only $(CORE_SOURCES)

vita-self-test: $(VITA_BUILD_DIR)/vitacheat-self-test.vpk $(VITA_PORTABLE_CHECK_OBJECTS)

$(VITA_BUILD_DIR)/main.o: vita-self-test/main.c include/vitacheat/search.h include/vitacheat/menu_activation.h $(VITA_COMMON_DIR)/debugScreen.h $(VITA_COMMON_DIR)/debugScreen_custom.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/search.o: src/search.c include/vitacheat/search.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/menu_activation.o: src/menu_activation.c include/vitacheat/menu_activation.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/pause-portable.o: src/pause.c include/vitacheat/pause.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_PORTABLE_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/launch-claimant-portable.o: src/launch_claimant.c include/vitacheat/launch_claimant.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_PORTABLE_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/menu-coordinator-portable.o: src/menu_coordinator.c include/vitacheat/menu_coordinator.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_PORTABLE_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/menu-coordinator-fuzz-portable.o: tests/fuzz/fuzz_menu_coordinator.c include/vitacheat/menu_coordinator.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_PORTABLE_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/target-attestation-portable.o: src/target_attestation.c include/vitacheat/target_attestation.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_PORTABLE_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/target-attestation-fuzz-portable.o: tests/fuzz/fuzz_target_attestation.c include/vitacheat/target_attestation.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_PORTABLE_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/memory-read-portable.o: src/memory_read.c include/vitacheat/memory_read.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_PORTABLE_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/memory-service-portable.o: src/memory_service.c include/vitacheat/memory_service.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_PORTABLE_CFLAGS) -c $< -o $@

$(VITA_BUILD_DIR)/memory-service-fuzz-portable.o: tests/fuzz/fuzz_memory_service.c include/vitacheat/memory_service.h Makefile | $(VITA_BUILD_DIR)
	$(VITA_CC) $(VITA_PORTABLE_CFLAGS) -c $< -o $@

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
