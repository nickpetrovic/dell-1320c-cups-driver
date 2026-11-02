VERSION := $(shell cat VERSION)
OS := $(shell uname -s)

ifeq ($(origin CXX),default)
  CXX := c++
endif
CUPS_CONFIG ?= cups-config
CXXFLAGS ?= -O2 -g
CPPFLAGS ?=
LDFLAGS ?=
DESTDIR ?=
BUILD := build

STANDARD := -std=c++23
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wcast-qual \
            -Wformat=2 -Wundef -Wnon-virtual-dtor -Woverloaded-virtual -Wimplicit-fallthrough -Wnull-dereference
HARDENING := -fstack-protector-strong
LIBCXX_VERSION := $(shell printf '\043include <version>\n' | $(CXX) $(STANDARD) -x c++ -dM -E - 2>/dev/null | sed -n 's/^\#define _LIBCPP_VERSION //p')
ifeq ($(shell [ -n "$(LIBCXX_VERSION)" ] && [ "$(LIBCXX_VERSION)" -lt 180000 ] && echo old),old)
  LIBCXX_HARDENING := -D_LIBCPP_ENABLE_HARDENED_MODE=1
else
  LIBCXX_HARDENING := -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST
endif
override CPPFLAGS += -D_GLIBCXX_ASSERTIONS $(LIBCXX_HARDENING)
override CXXFLAGS += $(STANDARD) $(WARNINGS) $(HARDENING)
ifeq ($(WERROR),1)
  override CXXFLAGS += -Werror
endif
ifeq ($(SANITIZE),1)
  override CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
  override LDFLAGS += -fsanitize=address,undefined
endif
ifeq ($(STATIC_RUNTIME),1)
  override LDFLAGS += -static-libstdc++ -static-libgcc
endif

PACKAGE_ID := xyz.petrovic.dell1320c

ifeq ($(OS),Darwin)
  PLATFORM := macos
  MACOS_MINIMUM := 13.3
  ARCHS ?= arm64 x86_64
  ARCH_FLAGS := $(foreach arch,$(ARCHS),-arch $(arch)) -mmacosx-version-min=$(MACOS_MINIMUM)
  TIDY_FLAGS := -isysroot $(shell xcrun --show-sdk-path 2>/dev/null) -mmacosx-version-min=$(MACOS_MINIMUM)
  override CPPFLAGS += -D_DARWIN_C_SOURCE
  DRIVER_DIR := /Library/Printers/Dell1320c
  FILTER_DIR := $(DRIVER_DIR)
  CUPS_FILTER := $(DRIVER_DIR)/rastertodell1320c
  PPD_DIR := /Library/Printers/PPDs/Contents/Resources
  PPD_NAME := Dell Color Laser 1320c.gz
  BIN_DIR := /usr/local/bin
  MAN_DIR := /usr/local/share/man/man1
  DOC_DIR := $(DRIVER_DIR)
  CUPS_CFLAGS :=
  CUPS_LIBS := -lcups
else
  PLATFORM := linux
  PREFIX ?= /usr/local
  ARCH_FLAGS :=
  TIDY_FLAGS :=
  override CPPFLAGS += -D_DEFAULT_SOURCE
  override CPPFLAGS += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
  override CXXFLAGS += -fPIE -fstack-clash-protection
  override LDFLAGS += -pie -Wl,-z,relro,-z,now
  CUPS_SERVERBIN ?= $(shell $(CUPS_CONFIG) --serverbin 2>/dev/null || echo /usr/lib/cups)
  DRIVER_DIR := $(PREFIX)/share/dell1320c
  FILTER_DIR := $(CUPS_SERVERBIN)/filter
  CUPS_FILTER := rastertodell1320c
  PPD_DIR := $(PREFIX)/share/ppd/dell1320c
  PPD_NAME := dell1320c.ppd
  BIN_DIR := $(PREFIX)/bin
  MAN_DIR := $(PREFIX)/share/man/man1
  DOC_DIR := $(PREFIX)/share/doc/dell1320c
  CUPS_CFLAGS := $(shell $(CUPS_CONFIG) --cflags 2>/dev/null)
  CUPS_LIBS := $(shell $(CUPS_CONFIG) --libs 2>/dev/null || echo -lcups)
endif
COLOR_DIR := $(DRIVER_DIR)/color
PPD_PATH := $(PPD_DIR)/$(PPD_NAME)

MODULES := settings color_table sq21 hbpl file_descriptor raster_reader page_converter
HEADERS := $(wildcard src/*.hpp)
OBJECTS := $(MODULES:%=$(BUILD)/%.o)
FILTER := $(BUILD)/rastertodell1320c
PPD := $(BUILD)/dell1320c.ppd
PRINTER_COMMAND := $(BUILD)/dell1320c
MANUAL_PAGE := $(BUILD)/dell1320c.1
UNIT_TESTS := $(BUILD)/unit-tests
SQ21_ENCODE := $(BUILD)/sq21-encode
BUILD_CONFIG := $(BUILD)/build-config
BUILD_SETTINGS := $(CXX) $(CPPFLAGS) $(CUPS_CFLAGS) $(ARCH_FLAGS) $(CXXFLAGS) | $(LDFLAGS) $(CUPS_LIBS) | $(PLATFORM) $(DRIVER_DIR) \
                  $(FILTER_DIR) $(COLOR_DIR) $(PPD_PATH) $(BIN_DIR) $(MAN_DIR) $(CUPS_FILTER) $(PACKAGE_ID)

COMPILE = $(CXX) $(CPPFLAGS) $(CUPS_CFLAGS) $(ARCH_FLAGS) $(CXXFLAGS)
LINK = $(CXX) $(ARCH_FLAGS) $(CXXFLAGS) $(LDFLAGS)

LLVM_VERSION := 23.1.3
CLANG_FORMAT := uvx clang-format@$(LLVM_VERSION)
CLANG_TIDY := uvx clang-tidy@$(LLVM_VERSION)
RUFF := uvx ruff@0.17.3
ACTIONLINT := uvx --from actionlint-py==1.7.12.25 actionlint
PYTHON_FILES := $(wildcard tools/*/*.py)
CXX_SOURCES := $(wildcard src/*.cpp test/unit/*.cpp tools/*/*.cpp)
FORMAT_FILES := $(CXX_SOURCES) $(HEADERS) $(wildcard test/unit/*.hpp)
SHELL_SCRIPTS := src/dell1320c.sh.in test/golden/run.sh $(wildcard test/packages/*.sh) packaging/macos/postinstall $(wildcard packaging/*/*.sh tools/*/*.sh)

.PHONY: all tools install uninstall check unit-test golden-test lint format format-check tidy compile-commands clean pkg linux-packages package-test FORCE
.DELETE_ON_ERROR:

all: $(FILTER) $(PPD) $(PRINTER_COMMAND) $(MANUAL_PAGE)

tools: $(SQ21_ENCODE)

$(BUILD):
	mkdir -p $@

$(BUILD_CONFIG): FORCE | $(BUILD)
	@printf '%s\n' '$(BUILD_SETTINGS)' > $@.new
	@if cmp -s $@.new $@; then rm -f $@.new; else mv $@.new $@; fi

$(BUILD)/%.o: src/%.cpp $(HEADERS) $(BUILD_CONFIG) | $(BUILD)
	$(COMPILE) -c -o $@ $<

$(BUILD)/rastertodell1320c.o: src/rastertodell1320c.cpp $(HEADERS) $(BUILD_CONFIG) Makefile | $(BUILD)
	$(COMPILE) -DDELL1320C_COLOR_DIR='"$(COLOR_DIR)"' -c -o $@ $<

$(FILTER): $(BUILD)/rastertodell1320c.o $(OBJECTS) $(BUILD_CONFIG)
	$(LINK) -o $@ $(BUILD)/rastertodell1320c.o $(OBJECTS) $(CUPS_LIBS) -lz

$(SQ21_ENCODE): tools/sq21/sq21_encode.cpp $(BUILD)/sq21.o $(HEADERS) $(BUILD_CONFIG) | $(BUILD)
	$(COMPILE) -Isrc $(LDFLAGS) -o $@ tools/sq21/sq21_encode.cpp $(BUILD)/sq21.o

$(PPD): data/ppd/dell1320c.ppd.in VERSION $(BUILD_CONFIG) Makefile | $(BUILD)
	sed -e 's|@CUPS_FILTER@|$(CUPS_FILTER)|' -e 's|@VERSION@|$(VERSION)|' $< > $@

$(PRINTER_COMMAND): src/dell1320c.sh.in VERSION $(BUILD_CONFIG) Makefile | $(BUILD)
	sed -e 's|@VERSION@|$(VERSION)|' -e 's|@PLATFORM@|$(PLATFORM)|' -e 's|@PPD@|$(PPD_PATH)|' -e 's|@DRIVER_DIR@|$(DRIVER_DIR)|' \
	    -e 's|@BIN_DIR@|$(BIN_DIR)|' -e 's|@MAN_DIR@|$(MAN_DIR)|' -e 's|@PACKAGE_ID@|$(PACKAGE_ID)|' $< > $@
	chmod 755 $@

$(MANUAL_PAGE): src/dell1320c.1.in VERSION $(BUILD_CONFIG) Makefile | $(BUILD)
	sed -e 's|@VERSION@|$(VERSION)|' -e 's|@PPD@|$(PPD_PATH)|' -e 's|@DRIVER_DIR@|$(DRIVER_DIR)|' $< > $@

install: all
	install -d "$(DESTDIR)$(FILTER_DIR)" "$(DESTDIR)$(COLOR_DIR)" "$(DESTDIR)$(PPD_DIR)" "$(DESTDIR)$(BIN_DIR)" "$(DESTDIR)$(MAN_DIR)" \
	    "$(DESTDIR)$(DOC_DIR)"
	install -m 644 LICENSE NOTICE "$(DESTDIR)$(DOC_DIR)/"
	install -m 755 $(FILTER) "$(DESTDIR)$(FILTER_DIR)/rastertodell1320c"
	install -m 644 data/color/*.lut "$(DESTDIR)$(COLOR_DIR)/"
ifeq ($(PLATFORM),macos)
	gzip -n -9 -c $(PPD) > "$(DESTDIR)$(PPD_PATH)"
	chmod 644 "$(DESTDIR)$(PPD_PATH)"
else
	install -m 644 $(PPD) "$(DESTDIR)$(PPD_PATH)"
endif
	install -m 755 $(PRINTER_COMMAND) "$(DESTDIR)$(BIN_DIR)/dell1320c"
	install -m 644 $(MANUAL_PAGE) "$(DESTDIR)$(MAN_DIR)/dell1320c.1"

uninstall:
	rm -f "$(DESTDIR)$(FILTER_DIR)/rastertodell1320c" "$(DESTDIR)$(PPD_PATH)" "$(DESTDIR)$(BIN_DIR)/dell1320c" "$(DESTDIR)$(MAN_DIR)/dell1320c.1"
	rm -rf "$(DESTDIR)$(COLOR_DIR)"
	rm -f "$(DESTDIR)$(DOC_DIR)/LICENSE" "$(DESTDIR)$(DOC_DIR)/NOTICE"
	-rmdir "$(DESTDIR)$(DRIVER_DIR)" 2>/dev/null
ifeq ($(PLATFORM),linux)
	-rmdir "$(DESTDIR)$(PPD_DIR)" "$(DESTDIR)$(DOC_DIR)" 2>/dev/null
endif

$(UNIT_TESTS): $(wildcard test/unit/*.cpp test/unit/*.hpp) $(OBJECTS) $(HEADERS) $(BUILD_CONFIG) | $(BUILD)
	$(COMPILE) -Isrc -o $@ $(wildcard test/unit/*.cpp) $(OBJECTS) $(LDFLAGS) $(CUPS_LIBS) -lz

unit-test: $(UNIT_TESTS) $(PPD)
	DELL1320C_COLOR_DIR=data/color DELL1320C_TEST_PPD=$(PPD) $(UNIT_TESTS)

golden-test: $(FILTER) $(PPD)
	FILTER=$(FILTER) PPD_FILE=$(PPD) DELL1320C_COLOR_DIR=data/color test/golden/run.sh

package-test:
	test/packages/run.sh $(BUILD)/dist

check: unit-test golden-test

lint: $(PPD)
	shellcheck $(SHELL_SCRIPTS)
	uv run -q --isolated --python '>=3.10' python -I tools/ppd/generate.py $(BUILD)/generated.ppd.in
	cmp -s $(BUILD)/generated.ppd.in data/ppd/dell1320c.ppd.in || \
	    { echo "error: data/ppd/dell1320c.ppd.in is out of date; run uv run tools/ppd/generate.py data/ppd/dell1320c.ppd.in" >&2; exit 1; }
	cupstestppd -I filters -W translations -W sizes $(PPD)
	$(RUFF) check $(PYTHON_FILES)
	$(RUFF) format --check $(PYTHON_FILES)
	$(ACTIONLINT)

format:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

compile-commands: | $(BUILD)
	uv run -q --isolated python -I tools/dev/compile_commands.py $(BUILD)/compile_commands.json $(CXX) \
	    "$(CPPFLAGS) $(CUPS_CFLAGS) $(TIDY_FLAGS) $(STANDARD) $(WARNINGS)" $(CXX_SOURCES)

tidy: compile-commands
	$(CLANG_TIDY) --quiet --warnings-as-errors='*' -p $(BUILD) $(CXX_SOURCES)

pkg: all
	BUILD=$(BUILD) VERSION=$(VERSION) PACKAGE_ID=$(PACKAGE_ID) packaging/macos/build.sh

linux-packages:
	BUILD=$(BUILD) VERSION=$(VERSION) ARCHITECTURES="$(ARCHITECTURES)" packaging/linux/build.sh

clean:
	rm -rf $(BUILD)
