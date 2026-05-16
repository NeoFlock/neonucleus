BIN=neonucleus
DYNLIB=libneonucleus.so
LIB=libneonucleus.a

CC=cc
LD=$(CC)
AR=ar
RANLIB=ranlib
WARN=-Wall -Werror -Wno-format-truncation

ifeq ($(MODE), release)
OPT=-Oz
DEBUG=
else ifeq ($(MODE), release-lto)
OPT=-Oz -flto
DEBUG=
else
OPT=-O0
SANITIZE=undefined,address
DEBUG=-g
endif

NN_STD=gnu99
EMU_STD=gnu23

NNFLAGS=

SANITIZE_FLAGS=

ifdef SANITIZE
	SANITIZE_FLAGS += -fsanitize=$(SANITIZE)
endif

# no-omit-frame-pointer so if a crash does happen we can trace it
CFLAGS=-fPIC -fno-omit-frame-pointer $(OPT) $(SANITIZE_FLAGS) $(DEBUG) $(NNFLAGS) $(WARN)

LDFLAGS=$(OPT) $(DEBUG) $(SANITIZE_FLAGS)

LINKRAYLIB=-lraylib
INCLUA=-I /usr/include/lua5.3
LINKLUA=-llua5.3
LINKLIBM=-lm
LINKLIBC=

BUILD_DIR=build
SRC_DIR=src

all: bin lib dynlib

$(BUILD_DIR)/neonucleus.o: $(SRC_DIR)/neonucleus.c $(SRC_DIR)/neonucleus.h
	$(CC) -o $(BUILD_DIR)/neonucleus.o -c $(SRC_DIR)/neonucleus.c $(CFLAGS) -std=$(NN_STD)

$(BUILD_DIR)/ncomplib.o: $(SRC_DIR)/ncomplib.c $(SRC_DIR)/ncomplib.h
	$(CC) -o $(BUILD_DIR)/ncomplib.o -c $(SRC_DIR)/ncomplib.c $(CFLAGS) -std=$(NN_STD)

nn: $(BUILD_DIR)/neonucleus.o $(BUILD_DIR)/ncomplib.o

$(BUILD_DIR)/luaarch.o: $(SRC_DIR)/luaarch.c $(SRC_DIR)/machine.lua
	$(CC) -o $(BUILD_DIR)/luaarch.o -c $(SRC_DIR)/luaarch.c $(CFLAGS) $(INCLUA) -std=$(EMU_STD)

$(BUILD_DIR)/glyphcache.o: $(SRC_DIR)/glyphcache.c $(SRC_DIR)/glyphcache.h
	$(CC) -o $(BUILD_DIR)/glyphcache.o -c $(SRC_DIR)/glyphcache.c $(CFLAGS) -std=$(EMU_STD)

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c $(SRC_DIR)/minBIOS.lua
	$(CC) -o $(BUILD_DIR)/main.o -c $(SRC_DIR)/main.c $(CFLAGS) $(INCLUA) -std=$(EMU_STD)

bin: nn $(BUILD_DIR)/main.o $(BUILD_DIR)/luaarch.o $(BUILD_DIR)/glyphcache.o
	$(LD) $(LDFLAGS) -o $(BIN) $(BUILD_DIR)/neonucleus.o $(BUILD_DIR)/ncomplib.o $(BUILD_DIR)/main.o $(BUILD_DIR)/glyphcache.o $(BUILD_DIR)/luaarch.o $(LINKLIBC) $(LINKLIBM) $(LINKRAYLIB) $(LINKLUA)

lib: nn
	$(AR) rc $(LIB) $(BUILD_DIR)/neonucleus.o $(BUILD_DIR)/ncomplib.o
	$(RANLIB) $(LIB)

dynlib: nn
	$(LD) $(LDFLAGS) -o $(DYNLIB) -shared $(BUILD_DIR)/neonucleus.o $(BUILD_DIR)/ncomplib.o $(LINKLIBM) $(LINKLIBC)

cleancache:
	rm -rf $(BUILD_DIR)/*.o

clean:
	rm -rf $(BIN) $(DYNLIB) $(LIB)
