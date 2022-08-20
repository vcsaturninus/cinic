CFLAGS += -Wall -Wpedantic -std=c99 -Werror -O3
ifdef DEBUG_MODE
 CFLAGS += -g
endif

CFLAGS+= -g

CPPFLAGS += -Isrc -Ilua
#CPPFLAGS += -D_POSIX_C_SOURCE=200112L
CPPFLAGS += -D_POSIX_C_SOURCE=200809L   # getline()

C_SRC:=$(wildcard src/*.c)
CLIB_FLAGS:=-shared -fPIC
OUT_CLIB:=libcinic.so

LUA_SRC:= $(wildcard lua/*.c)
LUALIB_CFLAGS:=-shared -fPIC
LUA_LDFLAGS:=-L$(realpath $(OUT_DIR)) -lcinic
OUT_LUALIB:=libluainic.so

TEST_SRC:=$(wildcard tests/*.c)
TEST_LDFLAGS:=-L$(realpath $(OUT_DIR)) -lcinic
OUT_CTESTS:=ctests

OUT_DIR:=out
VALGRIND_REPORT:=valgrind.txt


.PHONY: all clean tests

all: clean compile

clean:
	@echo "\n[ ] Cleaning up ..."
	rm -rf $(OUT_DIR) $(VALGRIND_REPORT)
	mkdir -p $(OUT_DIR)

$(OUT_DIR)/%.o: src/%.c #lua/%.c
#$(OUT_DIR)/%.o: $(C_SRC) $(LUA_SRC)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CLIB_FLAGS) -c -o $@ $<

$(OUT_DIR)/%.o: lua/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OUT_DIR)/%.o: tests/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

compile: clib #lualib

clib: $(addprefix $(OUT_DIR)/, $(notdir $(C_SRC:.c=.o)))
	@echo "\n[ ] Building $(OUT_CLIB) ..."
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ $(CLIB_FLAGS) -o $(OUT_DIR)/$(OUT_CLIB)

lualib: $(addprefix $(OUT_DIR)/, $(notdir $(LUA_SRC:.c=.o)))
	@echo "\n[ ] Building $(OUT_LUALIB)"
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ $(LUA_LDFLAGS) -o $(OUT_DIR)/$(OUT_LUALIB)

ctests: $(addprefix $(OUT_DIR)/, $(notdir $(TEST_SRC:.c=.o)))
	@echo "\n[ ] Building $(OUT_CTESTS)"
	$(CC) $(CFLAGS) $(CPPFLAGS) $^ -L$(realpath $(OUT_DIR)) -lcinic -o $(OUT_DIR)/$(OUT_CTESTS)


tests: clean clib ctests run

run:
	@LD_LIBRARY_PATH=$$(realpath $(OUT_DIR)) ./$(OUT_DIR)/$(OUT_CTESTS)

grind: clean clib ctests
	LD_LIBRARY_PATH=$$(realpath $(OUT_DIR)/):$(LD_LIBRARY_PATH) \
	valgrind --leak-check=full --show-leak-kinds=all \
		--track-origins=yes --verbose \
		--log-file=$(VALGRIND_REPORT) \
		$(OUT_DIR)/$(OUT_CTESTS)

