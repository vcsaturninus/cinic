# globals
LUA_VERSION:=lua5.3
CINIC_MAJOR:=1
CINIC_MINOR:=1

# artifacts
OUT_DIR:=out
VALGRIND_OUT:=valgrind.txt

CLIB_BASENAME:=libcinic.so
CLIB_SONAME:=$(CLIB_BASENAME).$(CINIC_MAJOR)
CLIB_REALNAME:=$(CLIB_SONAME).$(CINIC_MINOR)
CLIB_OUT:=$(CLIB_REALNAME)

LUALIB_BASENAME:=cinic.so
LUALIB_SONAME:=$(LUALIB_BASENAME).$(CINIC_MAJOR)
LUALIB_REALNAME:=$(LUALIB_SONAME).$(CINIC_MINOR)
LUALIB_OUT:=$(LUALIB_REALNAME)

CTESTS_BIN:=ctests
EXAMPLE_BIN:=example

# sources
HEADERS:=$(wildcard src/*.h)
CLIB_SRC:=$(wildcard src/*.c)
LUALIB_SRC:= $(wildcard lua/*.c)   # C code for lua compiled lib module
CTEST_SRC:=$(wildcard tests/*.c)
EXAMPLE_SRC:=$(wildcard examples/*.c)
LUATEST_SRC:=tests/tests.lua       # single entrypoint for lua tests

# CFLAGS
CFLAGS += -Wall -Wpedantic -std=c99 -Werror -O3
ifdef DEBUG_MODE
CFLAGS += -g
CPPFLAGS += -DDEBUG_MODE
endif
CLIB_CFLAGS:= $(CFLAGS) -shared -fPIC -Wl,-soname,$(CLIB_SONAME)
LUALIB_CFLAGS:= $(CFLAGS) -shared -fPIC -Wl,-soname,$(LUALIB_SONAME)

# LDFLAGS
LD_LIBRARY_PATH:=$(OUT_DIR)/:$$LD_LIBRARY_PATH
LDFLAGS:=-L$(realpath $(OUT_DIR))
LUALIB_LDFLAGS:=-l$(LUA_VERSION) -lcinic $(LDFLAGS)
CTEST_LDFLAGS:=-lcinic $(LDFLAGS)

# augment header file search path with src/ and lua/ dirs
CPPFLAGS += -Isrc -Ilua
CPPFLAGS += -D_POSIX_C_SOURCE=200809L   # getline()


###########
# RECIPES #
###########

# implicit rules
$(OUT_DIR)/%.o: src/%.c $(HEADERS)
	$(CC) $(CLIB_CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OUT_DIR)/%.o: lua/%.c $(HEADERS)
	$(CC) $(LUALIB_CFLAGS) $(LUALIB_LDFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OUT_DIR)/%.o: tests/%.c $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_LDFLAGS) -c -o $@ $<

$(OUT_DIR)/%.o: examples/%.c $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_LDFLAGS) -c -o $@ $<

.PHONY: all clean tests clib lualib build_lualib build_ctests ctests luatests grind

all: clib lualib

# cinic C library
clib: $(addprefix $(OUT_DIR)/, $(notdir $(CLIB_SRC:.c=.o)))
	@echo "\n[ ] Building $(CLIB_OUT) ..."
	$(CC) $(CLIB_CFLAGS) $(CPPFLAGS) $^ -o $(OUT_DIR)/$(CLIB_OUT)
	ln -sf $(CLIB_OUT) $(OUT_DIR)/$(CLIB_BASENAME)
	ln -sf $(CLIB_OUT) $(OUT_DIR)/$(CLIB_SONAME)

# cinic Lua C library module
lualib: clib build_lualib

build_lualib: $(addprefix $(OUT_DIR)/, $(notdir $(LUALIB_SRC:.c=.o)))
	@echo "\n[ ] Building $(LUALIB_OUT)"
	$(CC) $(LUALIB_CFLAGS) $(CPPFLAGS) $^ $(LUALIB_LDFLAGS) -o $(OUT_DIR)/$(LUALIB_OUT)
	ln -sf $(LUALIB_OUT) $(OUT_DIR)/$(LUALIB_BASENAME)
	ln -sf $(LUALIB_OUT) $(OUT_DIR)/$(LUALIB_SONAME)

tests: ctests luatests

# tests binary
ctests: clib build_ctests
	@LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) ./$(OUT_DIR)/$(CTESTS_BIN)

build_ctests: $(addprefix $(OUT_DIR)/, $(notdir $(CTEST_SRC:.c=.o)))
	@echo "\n[ ] Building $(CTESTS_BIN)"
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ $(CTEST_LDFLAGS) -o $(OUT_DIR)/$(CTESTS_BIN)

# lua tests script
luatests: lualib
	@LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) ./tests/tests.lua

# statically-linked example
example: clean build_example

build_example: $(addprefix $(OUT_DIR)/, $(notdir $(EXAMPLE_SRC:.c=.o))) \
               $(addprefix $(OUT_DIR)/, $(notdir $(CLIB_SRC:.c=.o)))
	@echo "\n[ ] Building $(EXAMPLE_BIN)"
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -o $(OUT_DIR)/$(EXAMPLE_BIN)

clean:
	@echo "\n[ ] Cleaning up ..."
	rm -rf $(OUT_DIR) $(VALGRIND_OUT)
	mkdir -p $(OUT_DIR)

grind: build_ctests
	LD_LIBRARY_PATH=$(LD_LIBRARY_PATH) \
	valgrind --leak-check=full --show-leak-kinds=all \
		--track-origins=yes --verbose \
		--log-file=$(VALGRIND_OUT) \
		$(OUT_DIR)/$(CTESTS_BIN)

