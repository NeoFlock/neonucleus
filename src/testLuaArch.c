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

static nn_value testLuaArch_getValue(lua_State *L, int index) {
    if(lua_isinteger(L, index)) {
        return nn_values_integer(lua_tointeger(L, index));
    }
    if(lua_isnumber(L, index)) {
        return nn_values_number(lua_tonumber(L, index));
    }
    if(lua_isboolean(L, index)) {
        return nn_values_boolean(lua_toboolean(L, index));
    }
    if(lua_isnoneornil(L, index)) {
        return nn_values_nil();
    }
    if(lua_isstring(L, index)) {
        size_t l = 0;
        const char *s = lua_tolstring(L, index, &l);
        return nn_values_string(s, l);
    }
    //TODO: bring it back once I make everything else not leak memory
    //luaL_argcheck(L, false, index, luaL_typename(L, index));
    return nn_values_nil();
}

static void testLuaArch_pushValue(lua_State *L, nn_value val) {
    int t = nn_values_getType(val);
    if(t == NN_VALUE_NIL) {
        lua_pushnil(L);
        return;
    }
    if(t == NN_VALUE_INT) {
        lua_pushinteger(L, val.integer);
        return;
    }
    if(t == NN_VALUE_NUMBER) {
        lua_pushnumber(L, val.number);
        return;
    }
    if(t == NN_VALUE_BOOL) {
        lua_pushboolean(L, val.boolean);
        return;
    }
    if(t == NN_VALUE_STR) {
        lua_pushlstring(L, val.string->data, val.string->len);
        return;
    }
    if(t == NN_VALUE_CSTR) {
        lua_pushstring(L, val.cstring);
        return;
    }
    if(t == NN_VALUE_ARRAY) {
        nn_array *arr = val.array;
        lua_createtable(L, arr->len, 0);
        int luaVal = lua_gettop(L);
        for(size_t i = 0; i < arr->len; i++) {
            testLuaArch_pushValue(L, arr->values[i]);
            lua_seti(L, luaVal, i+1);
        }
        return;
    }
    if(t == NN_VALUE_TABLE) {
        nn_table *tbl = val.table;
        lua_createtable(L, 0, tbl->len);
        int luaVal = lua_gettop(L);
        for(size_t i = 0; i < tbl->len; i++) {
            testLuaArch_pushValue(L, tbl->pairs[i].key);
            testLuaArch_pushValue(L, tbl->pairs[i].val);
            lua_settable(L, luaVal);
        }
        return;
    }
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

static int testLuaArch_computer_uptime(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushnumber(L, nn_getUptime(c));
    return 1;
}

// TODO: beep
static int testLuaArch_computer_beep(lua_State *L) {
    return 0;
}

static int testLuaArch_computer_energy(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushinteger(L, nn_getEnergy(c));
    return 1;
}

static int testLuaArch_computer_maxEnergy(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushinteger(L, nn_getEnergy(c));
    return 1;
}

static int testLuaArch_computer_getArchitecture(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushstring(L, nn_getArchitecture(c)->archName);
    return 1;
}

static int testLuaArch_computer_getArchitectures(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_createtable(L, 3, 0);
    int arr = lua_gettop(L);
    size_t i = 0;
    while(true) {
        nn_architecture *arch = nn_getSupportedArchitecture(c, i);
        if(arch == NULL) break;
        i++;
        lua_pushstring(L, arch->archName);
        lua_seti(L, arr, i);
    }
    return 1;
}

static int testLuaArch_computer_setArchitecture(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    const char *requested = luaL_checkstring(L, 1);
    for(size_t i = 0;; i++) {
        nn_architecture *arch = nn_getSupportedArchitecture(c, i);
        if(arch == NULL) break;
        if(strcmp(arch->archName, requested) == 0) {
            nn_setState(c, NN_STATE_SWITCH);
            nn_setNextArchitecture(c, arch);
            return 0;
        }
    }
    luaL_error(L, "unsupported architecture: %s", requested);
    return 0;
}

static int testLuaArch_computer_isOverheating(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushboolean(L, nn_isOverheating(c));
    return 1;
}

static int testLuaArch_computer_getTemperature(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushnumber(L, nn_getTemperature(c));
    return 1;
}

static int testLuaArch_computer_addHeat(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    double n = luaL_checknumber(L, 1);
    nn_addHeat(c, n);
    return 0;
}

static int testLuaArch_computer_pushSignal(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    luaL_checkstring(L, 1);
    int argc = lua_gettop(L);
    if(argc > NN_MAX_ARGS) luaL_error(L, "too many arguments");
    nn_value args[argc];
    for(size_t i = 0; i < argc; i++) {
        args[i] = testLuaArch_getValue(L, i+1);
    }
    const char *err = nn_pushSignal(c, args, argc);
    if(err != NULL) {
        for(size_t i = 0; i < argc; i++) {
            nn_values_drop(args[i]);
        }
        luaL_error(L, "%s", err);
        return 0;
    }
    return 0;
}

static int testLuaArch_computer_popSignal(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    size_t retc = nn_signalSize(c);
    for(size_t i = 0; i < retc; i++) {
        testLuaArch_pushValue(L, nn_fetchSignalValue(c, i));
    }
    nn_popSignal(c);
    return retc;
}

static int testLuaArch_component_list(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    size_t len = 0;
    nn_component **components = nn_listComponent(c, &len);
    lua_createtable(L, 0, len);
    int list = lua_gettop(L);
    for(size_t i = 0; i < len; i++) {
        nn_component *component = components[i];
        nn_componentTable *table = nn_getComponentTable(component);
        nn_address addr = nn_getComponentAddress(component);
        const char *type = nn_getComponentType(table);

        lua_pushstring(L, type);
        lua_setfield(L, list, addr);
    }
    nn_free(components);
    return 1;
}

static int testLuaArch_component_doc(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    const char *addr = luaL_checkstring(L, 1);
    const char *method = luaL_checkstring(L, 2);
    nn_component *component = nn_findComponent(c, (char *)addr);
    if(component == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "no such component");
        return 2;
    }
    const char *doc = nn_methodDoc(nn_getComponentTable(component), method);
    if(doc == NULL) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, doc);
    }
    return 1;
}


static int testLuaArch_component_invoke(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    const char *addr = luaL_checkstring(L, 1);
    const char *method = luaL_checkstring(L, 2);
    int argc = lua_gettop(L) - 2;
    nn_component *component = nn_findComponent(c, (char *)addr);
    if(component == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "no such component");
        return 2;
    }
    nn_resetCall(c);
    for(size_t i = 0; i < argc; i++) {
        nn_addArgument(c, testLuaArch_getValue(L, 2 + argc));
    }
    if(!nn_invokeComponentMethod(component, method)) {
        nn_resetCall(c);
        lua_pushnil(L);
        lua_pushstring(L, "no such method");
        return 2;
    }
    size_t retc = nn_getReturnCount(c);
    for(size_t i = 0; i < retc; i++) {
        testLuaArch_pushValue(L, nn_getReturn(c, i));
    }
    nn_resetCall(c);
    return retc;
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
    lua_pushcfunction(L, testLuaArch_computer_uptime);
    lua_setfield(L, computer, "uptime");
    lua_pushcfunction(L, testLuaArch_computer_beep);
    lua_setfield(L, computer, "beep");
    lua_pushcfunction(L, testLuaArch_computer_energy);
    lua_setfield(L, computer, "energy");
    lua_pushcfunction(L, testLuaArch_computer_maxEnergy);
    lua_setfield(L, computer, "maxEnergy");
    lua_pushcfunction(L, testLuaArch_computer_getArchitecture);
    lua_setfield(L, computer, "getArchitecture");
    lua_pushcfunction(L, testLuaArch_computer_getArchitectures);
    lua_setfield(L, computer, "getArchitectures");
    lua_pushcfunction(L, testLuaArch_computer_setArchitecture);
    lua_setfield(L, computer, "setArchitecture");
    lua_pushcfunction(L, testLuaArch_computer_isOverheating);
    lua_setfield(L, computer, "isOverheating");
    lua_pushcfunction(L, testLuaArch_computer_getTemperature);
    lua_setfield(L, computer, "getTemperature");
    lua_pushcfunction(L, testLuaArch_computer_addHeat);
    lua_setfield(L, computer, "addHeat");
    lua_setglobal(L, "computer");

    lua_createtable(L, 0, 10);
    int component = lua_gettop(L);
    lua_pushcfunction(L, testLuaArch_component_list);
    lua_setfield(L, component, "list");
    lua_pushcfunction(L, testLuaArch_component_doc);
    lua_setfield(L, component, "doc");
    lua_pushcfunction(L, testLuaArch_component_invoke);
    lua_setfield(L, component, "invoke");
    lua_setglobal(L, "component");
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
