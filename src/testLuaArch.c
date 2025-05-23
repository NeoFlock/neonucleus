#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <assert.h>
#include <string.h>
#include "neonucleus.h"

char *testLuaSandbox = NULL;

typedef struct testLuaArch {
    lua_State *L;
    nn_computer *computer;
    size_t memoryUsed;
} testLuaArch;

void *testLuaArch_alloc(testLuaArch *arch, void *ptr, size_t osize, size_t nsize) {
    if(nsize == 0) {
        arch->memoryUsed -= osize;
        nn_free(ptr);
        return NULL;
    } else {
        if(arch->memoryUsed - osize + nsize > nn_getComputerMemoryTotal(arch->computer)) {
            return NULL; // OOM condition
        }
        if(ptr != NULL) {
            // if ptr is NULL, osize will actually encode the type.
            // We do not want that to mess us up.
            arch->memoryUsed -= osize;
        }
        arch->memoryUsed += nsize;
        return nn_realloc(ptr, nsize);
    }
}

testLuaArch *testLuaArch_get(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "archPtr");
    testLuaArch *arch = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return arch;
}

nn_computer *testLuaArch_getComputer(lua_State *L) {
    return testLuaArch_get(L)->computer;
}

static int testLuaArch_computer_usedMemory(lua_State *L) {
    testLuaArch *s = testLuaArch_get(L);
    lua_pushinteger(L, s->memoryUsed);
    return 1;
}

static int testLuaArch_computer_freeMemory(lua_State *L) {
    testLuaArch *s = testLuaArch_get(L);
    lua_pushinteger(L, nn_getComputerMemoryTotal(s->computer) - s->memoryUsed);
    return 1;
}

static int testLuaArch_computer_totalMemory(lua_State *L) {
    testLuaArch *s = testLuaArch_get(L);
    lua_pushinteger(L, nn_getComputerMemoryTotal(s->computer));
    return 1;
}

static int testLuaArch_computer_address(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushstring(L, nn_getComputerAddress(c));
    return 1;
}

static int testLuaArch_computer_tmpAddress(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushstring(L, nn_getTmpAddress(c));
    return 1;
}

void testLuaArch_loadEnv(lua_State *L) {
    lua_createtable(L, 0, 10);
    int computer = lua_gettop(L);
    lua_pushcfunction(L, testLuaArch_computer_usedMemory);
    lua_setfield(L, computer, "usedMemory");
    lua_pushcfunction(L, testLuaArch_computer_freeMemory);
    lua_setfield(L, computer, "freeMemory");
    lua_pushcfunction(L, testLuaArch_computer_totalMemory);
    lua_setfield(L, computer, "totalMemory");
    lua_pushcfunction(L, testLuaArch_computer_address);
    lua_setfield(L, computer, "address");
    lua_pushcfunction(L, testLuaArch_computer_tmpAddress);
    lua_setfield(L, computer, "tmpAddress");
    lua_setglobal(L, "computer");
}

testLuaArch *testLuaArch_setup(nn_computer *computer, void *_) {
    testLuaArch *s = nn_malloc(sizeof(testLuaArch));
    if(s == NULL) return NULL;
    s->memoryUsed = 0;
    s->computer = computer;
    lua_State *L = lua_newstate((void *)testLuaArch_alloc, s);
    assert(L != NULL);
    luaL_openlibs(L);
    lua_pushlightuserdata(L, s);
    lua_setfield(L, LUA_REGISTRYINDEX, "archPtr");
    s->L = L;
    testLuaArch_loadEnv(L);
    assert(luaL_loadbufferx(L, testLuaSandbox, strlen(testLuaSandbox), "=machine.lua", "t") == LUA_OK);
    return s;
}

void testLuaArch_teardown(nn_computer *computer, testLuaArch *arch, void *_) {
    lua_close(arch->L);
    nn_free(arch);
}

void testLuaArch_tick(nn_computer *computer, testLuaArch *arch, void *_) {
    int ret = 0;
    int res = lua_resume(arch->L, NULL, 0, &ret);
    if(res == LUA_OK) {
        // machine halted, this is no good
        lua_pop(arch->L, ret);
        nn_setCError(computer, "machine halted");
    } else if(res == LUA_YIELD) {
        lua_pop(arch->L, ret);
    } else {
        const char *s = lua_tostring(arch->L, -1);
        nn_setError(computer, s);
        lua_pop(arch->L, 1);
    }
}

size_t testLuaArch_getMemoryUsage(nn_computer *computer, testLuaArch *arch, void *_) {
    return arch->memoryUsed;
}

char *testLuaArch_serialize(nn_computer *computer, testLuaArch *arch, void *_, size_t *len) {
    *len = 0;
    return NULL;
}

void testLuaArch_deserialize(nn_computer *computer, const char *data, size_t len, testLuaArch *arch, void *_) {}

nn_architecture testLuaArchTable = {
    .archName = "Lua Test",
    .userdata = NULL,
    .setup = (void *)testLuaArch_setup,
    .teardown = (void *)testLuaArch_teardown,
    .tick = (void *)testLuaArch_tick,
    .getMemoryUsage = (void*)testLuaArch_getMemoryUsage,
};

nn_architecture *testLuaArch_getArchitecture(const char *sandboxPath) {
    if(testLuaSandbox == NULL) {
        FILE *f = fopen(sandboxPath, "r");
        if(f == NULL) return NULL;
        fseek(f, 0, SEEK_END);
        size_t l = ftell(f);
        testLuaSandbox = nn_malloc(l+1);
        if(testLuaSandbox == NULL) {
            fclose(f);
            return NULL;
        }
        fseek(f, 0, SEEK_SET);
        fread(testLuaSandbox, sizeof(char), l, f);
        testLuaSandbox[l] = '\0';
        fclose(f);
    }
    return &testLuaArchTable;
}
