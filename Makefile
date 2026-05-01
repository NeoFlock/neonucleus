BIN=neonucleus
DYNLIB=libneonucleus.so
LIB=libneonucleus.a

CC=cc
OPT=-O3
SANITIZE=
DEBUG=
NNFLAGS=
CFLAGS=-fPIC $(OPT) $(SANITIZE) $(DEBUG) $(NNFLAGS)

LD=$(CC)
LDFLAGS=$(OPT) $(DEBUG)

AR=ar
RANLIB=ranlib

LINKRAYLIB=-lraylib
INCLUA=-I /usr/include/lua5.3
LINKLUA=-llua5.3
LINKLIBM=-lm
LINKLIBC=

BUILD_DIR=build
SRC_DIR=src

$(BUILD_DIR)/neonucleus.o: $(SRC_DIR)/neonucleus.c $(SRC_DIR)/neonucleus.h
	$(CC) -o $(BUILD_DIR)/neonucleus.o -c $(SRC_DIR)/neonucleus.c $(CFLAGS)

$(BUILD_DIR)/ncomplib.o: $(SRC_DIR)/ncomplib.c $(SRC_DIR)/ncomplib.h
	$(CC) -o $(BUILD_DIR)/ncomplib.o -c $(SRC_DIR)/ncomplib.c $(CFLAGS)

nn: $(BUILD_DIR)/neonucleus.o $(BUILD_DIR)/ncomplib.o

$(BUILD_DIR)/luaarch.o: $(SRC_DIR)/luaarch.c $(SRC_DIR)/machine.lua
	$(CC) -o $(BUILD_DIR)/luaarch.o -c $(SRC_DIR)/luaarch.c $(CFLAGS) $(INCLUA)

$(BUILD_DIR)/glyphcache.o: $(SRC_DIR)/glyphcache.c $(SRC_DIR)/glyphcache.h
	$(CC) -o $(BUILD_DIR)/glyphcache.o -c $(SRC_DIR)/glyphcache.c $(CFLAGS)

$(BUILD_DIR)/main.o: $(SRC_DIR)/main.c $(SRC_DIR)/minBIOS.lua
	$(CC) -o $(BUILD_DIR)/main.o -c $(SRC_DIR)/main.c $(CFLAGS) $(INCLUA)

bin: nn $(BUILD_DIR)/main.o $(BUILD_DIR)/luaarch.o $(BUILD_DIR)/glyphcache.o
	$(LD) $(LDFLAGS) -o $(BIN) $(BUILD_DIR)/neonucleus.o $(BUILD_DIR)/ncomplib.o $(BUILD_DIR)/main.o $(BUILD_DIR)/glyphcache.o $(BUILD_DIR)/luaarch.o $(LINKLIBC) $(LINKLIBM) $(LINKRAYLIB) $(LINKLUA)

lib: nn
	$(AR) rc $(LIB) $(BUILD_DIR)/neonucleus.o $(BUILD_DIR)/ncomplib.o
	$(RANLIB) $(LIB)

dynlib: nn
	$(LD) $(LDFLAGS) -o $(DYNLIB) -shared $(BUILD_DIR)/neonucleus.o $(BUILD_DIR)/ncomplib.o $(LINKLIBM) $(LINKLIBC)

all: bin lib dynlib

cleancache:
	rm -rf $(BUILD_DIR)/*.o

clean:
	rm -rf $(BIN) $(DYNLIB) $(LIB)
