CC1_DIR:=../src/cc
CPP_DIR:=../src/cpp
UTIL_DIR:=../src/util
LIB_DIR:=../lib
OBJ_DIR:=obj

OPTIMIZE:=-O2 -g3
CFLAGS:=-ansi -std=c11 -pedantic -MD -Wall -Wextra -Werror -Wold-style-definition \
	-Wno-missing-field-initializers -Wno-typedef-redefinition -Wno-empty-body
CFLAGS+=-I$(CC1_DIR) -I$(CPP_DIR) -I$(UTIL_DIR) $(OPTIMIZE)

WCC_DIR:=src
WCC_SRCS:=$(wildcard $(WCC_DIR)/*.c) \
	$(CC1_DIR)/lexer.c $(CC1_DIR)/type.c $(CC1_DIR)/var.c $(CC1_DIR)/ast.c $(CC1_DIR)/parser.c $(CC1_DIR)/parser_expr.c \
	$(CPP_DIR)/preprocessor.c $(CPP_DIR)/pp_parser.c $(CPP_DIR)/macro.c \
	$(UTIL_DIR)/util.c $(UTIL_DIR)/table.c
WCC_OBJS:=$(addprefix $(OBJ_DIR)/,$(notdir $(WCC_SRCS:.c=.o)))

.PHONY: all
all:	wcc

.PHONY: release
release:
	$(MAKE) OPTIMIZE=-O2

wcc: $(WCC_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

-include $(OBJ_DIR)/*.d

$(OBJ_DIR)/%.o: $(CC1_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: $(CPP_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: $(UTIL_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: $(WCC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: test
test:	all
	$(MAKE) -C tests clean all

.PHONY: test-all
test-all: test test-gen2 diff-gen23

.PHONY: clean
clean:
	rm -rf wcc $(OBJ_DIR) a.wasm gen2 gen3 public
	$(MAKE) -C tests clean

### Self hosting

.PHONY: gen2
gen2:	wcc
	$(MAKE) HOST_TARGET=wcc HOST_CC="./wcc" TARGET=gen2 self-hosting
.PHONY: test-gen2
test-gen2: gen2
	$(MAKE) TARGET_CC="node ../runtime/runwasm.js ../gen2/cc.wasm" test-self-hosting

.PHONY: gen3
gen3:	gen2
	$(MAKE) HOST_TARGET=gen2 HOST_CC="node ./runtime/runwasm.js ./gen2/cc.wasm" TARGET=gen3 self-hosting

.PHONY: diff-gen23
diff-gen23:	gen2 gen3
	diff -b gen2/cc.wasm gen3/cc.wasm

.PHONY: self-hosting
self-hosting:	$(TARGET)/cc.wasm

.PHONY: test-self-hosting
test-self-hosting:
	$(MAKE) WCC="$(TARGET_CC)" -C tests

WCC_LIBS=runtime/lib.c $(LIB_DIR)/sprintf.c $(LIB_DIR)/umalloc.c $(LIB_DIR)/assert.c

$(TARGET)/cc.wasm:	$(WCC_SRCS) $(WCC_LIBS)
	mkdir -p $(TARGET)
	$(HOST_CC) -emain -o$(TARGET)/cc.wasm -DSELF_HOSTING \
		-I../inc -I$(CC1_DIR) -I$(CPP_DIR) -I$(UTIL_DIR) -I$(LIB_DIR) \
		$(WCC_SRCS) \
		$(WCC_LIBS)


### www

ASSETS_DIR=public

.PHONY:	assets
assets:	gen2 $(ASSETS_DIR)/cc.wasm $(ASSETS_DIR)/libs.json

$(ASSETS_DIR)/cc.wasm:	gen2/cc.wasm
	mkdir -p $(ASSETS_DIR)
	cp gen2/cc.wasm $@

$(ASSETS_DIR)/libs.json:	lib_list.json
	mkdir -p $(ASSETS_DIR)
	node tool/pack_libs.js lib_list.json > $@

.PHONY: deploy
deploy:	assets
	npm run release
