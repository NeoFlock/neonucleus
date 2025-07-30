#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "neonucleus.h"

char *testLuaSandbox = NULL;

#if LUA_VERSION_NUM == 502

#include <math.h>

// monkey patching

bool lua_isinteger(lua_State *L, int i) {
    if(lua_type(L, i) != LUA_TNUMBER) return false;
    double x = lua_tonumber(L, i);
    if(isinf(x)) return false;
    if(isnan(x)) return false;
    return trunc(x) == x;
}

void lua_seti(lua_State *L, int arr, int i) {
    lua_rawseti(L, arr, i);
}

#endif

typedef struct testLuaArch {
    lua_State *L;
    nn_computer *computer;
    size_t memoryUsed;
} testLuaArch;

testLuaArch *testLuaArch_get(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "archPtr");
    testLuaArch *arch = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return arch;
}

nn_Alloc *testLuaArch_getAlloc(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "archPtr");
    testLuaArch *arch = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return nn_getAllocator(nn_getUniverse(arch->computer));
}

const char *testLuaArch_pushlstring(lua_State *L, const char *s, size_t len) {
    if (lua_checkstack(L, 1) == 0) {
        return NULL;
    }
    testLuaArch* arch = testLuaArch_get(L);
    size_t freeSpace = nn_getComputerMemoryTotal(arch->computer) - arch->memoryUsed;
    if ((len * 2 + 64) > freeSpace) { // dk how much space this really needs and its unstable so :/
        return NULL;
    }
    return lua_pushlstring(L, s, len);
}

const char *testLuaArch_pushstring(lua_State *L, const char *s) {
    size_t len = strlen(s);
    return testLuaArch_pushlstring(L, s, len);
}

void *testLuaArch_alloc(testLuaArch *arch, void *ptr, size_t osize, size_t nsize) {
    nn_Alloc *alloc = nn_getAllocator(nn_getUniverse(arch->computer));
    if(nsize == 0) {
        arch->memoryUsed -= osize;
        nn_dealloc(alloc, ptr, osize);
        return NULL;
    } else {
        size_t actualOldSize = osize;
        if(ptr == NULL) actualOldSize = 0;
        if(arch->memoryUsed - actualOldSize + nsize > nn_getComputerMemoryTotal(arch->computer)) {
            return NULL; // OOM condition
        }
        arch->memoryUsed -= actualOldSize;
        arch->memoryUsed += nsize;
        return nn_resize(alloc, ptr, actualOldSize, nsize);
    }
}

nn_computer *testLuaArch_getComputer(lua_State *L) {
    return testLuaArch_get(L)->computer;
}

static nn_value testLuaArch_getValue(lua_State *L, int index) {
    int type = lua_type(L, index);
    nn_Alloc *alloc = testLuaArch_getAlloc(L);
    
    if(type == LUA_TBOOLEAN) {
        return nn_values_boolean(lua_toboolean(L, index));
    }
    if(lua_isnoneornil(L, index)) {
        return nn_values_nil();
    }
    if(type == LUA_TSTRING) {
        size_t l = 0;
        const char *s = lua_tolstring(L, index, &l);
        return nn_values_string(alloc, s, l);
    }
    if(type == LUA_TNUMBER && lua_isinteger(L, index)) {
        return nn_values_integer(lua_tointeger(L, index));
    }
    if(type == LUA_TNUMBER && lua_isnumber(L, index)) {
        return nn_values_number(lua_tonumber(L, index));
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
	luaL_error(L, "invalid return type: %d", t);
}

static int testLuaArch_computer_clearError(lua_State *L) {
    testLuaArch *s = testLuaArch_get(L);
    nn_clearError(s->computer);
    return 0;
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
    nn_computer *c = testLuaArch_getComputer(L);
	// defaults
	double frequency = 200;
	double duration = 0.25;
	double volume = 1;

	if(lua_type(L, 1) == LUA_TNUMBER) {
		frequency = lua_tonumber(L, 1);
	}
	if(lua_type(L, 2) == LUA_TNUMBER) {
		duration = lua_tonumber(L, 2);
	}
	if(lua_type(L, 3) == LUA_TNUMBER) {
		volume = lua_tonumber(L, 3);
	}

	nn_computer_setBeep(c, frequency, duration, volume);
    return 0;
}

static int testLuaArch_computer_energy(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushnumber(L, nn_getEnergy(c));
    return 1;
}

static int testLuaArch_computer_maxEnergy(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushnumber(L, nn_getEnergy(c));
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

static int testLuaArch_computer_isOverworked(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushboolean(L, nn_isOverworked(c));
    return 1;
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

static int testLuaArch_computer_users(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    size_t i = 0;
    while(true) {
        const char *name = nn_indexUser(c, i);
        if(name == NULL) break;
        lua_pushstring(L, name);
        i++;
    }
    return i;
}

static int testLuaArch_computer_getState(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_pushinteger(L, nn_getState(c));
    return 1;
}

static int testLuaArch_computer_setState(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    int s = luaL_checkinteger(L, 1);
    nn_setState(c, s);
    return 1;
}

static int testLuaArch_computer_getDeviceInfo(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);

	nn_deviceInfoList_t *list = nn_getComputerDeviceInfoList(c);
	nn_size_t deviceCount = nn_getDeviceCount(list);

	lua_createtable(L, 0, deviceCount);
	int infoTable = lua_gettop(L);

	for(nn_size_t i = 0; i < deviceCount; i++) {
		nn_deviceInfo_t *info = nn_getDeviceInfoAt(list, i);
		lua_createtable(L, 0, 16);
		int deviceTable = lua_gettop(L);

		nn_size_t j = 0;
		while(true) {
			const char *value = NULL;
			const char *key = nn_iterateDeviceInfoKeys(info, j, &value);
			j++;
			if(key == NULL) break;
			lua_pushstring(L, value);
			lua_setfield(L, deviceTable, key);
		}

		lua_setfield(L, infoTable, nn_getDeviceInfoAddress(info));
	}

	return 1;
}

static int testLuaArch_component_list(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    lua_createtable(L, 0, 10);
    size_t iter = 0;
    int list = lua_gettop(L);
    while(true) {
        nn_component *component = nn_iterComponent(c, &iter);
        if(component == NULL) break;
        nn_componentTable *table = nn_getComponentTable(component);
        nn_address addr = nn_getComponentAddress(component);
        const char *type = nn_getComponentType(table);

        lua_pushstring(L, type);
        lua_setfield(L, list, addr);
    }
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

static int testLuaArch_component_fields(lua_State *L) {
    lua_createtable(L, 0, 0);
    return 1;
}

static int testLuaArch_component_methods(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    const char *addr = luaL_checkstring(L, 1);
    nn_component *component = nn_findComponent(c, (char *)addr);
    if(component == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "no such component");
        return 2;
    }
    nn_componentTable *table = nn_getComponentTable(component);
    lua_createtable(L, 0, 0);
    int methods = lua_gettop(L);

    size_t i = 0;
    while(true) {
        bool direct = false;
        const char *name = nn_getTableMethod(table, i, &direct);
        if(name == NULL) break;
        i++;
        if(!nn_isMethodEnabled(component, name)) continue;
        lua_pushboolean(L, direct);
        lua_setfield(L, methods, name);
    }

    return 1;
}

static int testLuaArch_component_slot(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    const char *addr = luaL_checkstring(L, 1);
    nn_component *component = nn_findComponent(c, (char *)addr);
    if(component == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "no such component");
        return 2;
    }
    lua_pushinteger(L, nn_getComponentSlot(component));
    return 1;
}

static int testLuaArch_component_type(lua_State *L) {
    nn_computer *c = testLuaArch_getComputer(L);
    const char *addr = luaL_checkstring(L, 1);
    nn_component *component = nn_findComponent(c, (char *)addr);
    if(component == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "no such component");
        return 2;
    }
    lua_pushstring(L, nn_getComponentType(nn_getComponentTable(component)));
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
        nn_addArgument(c, testLuaArch_getValue(L, 3 + i));
    }
    if(!nn_invokeComponentMethod(component, method)) {
        nn_resetCall(c);
        lua_pushnil(L);
        lua_pushstring(L, "no such method");
        return 2;
    }
    if(nn_getError(c) != NULL) {
        nn_resetCall(c);
        luaL_error(L, "%s", nn_getError(c));
    }
    size_t retc = nn_getReturnCount(c);
    for(size_t i = 0; i < retc; i++) {
        testLuaArch_pushValue(L, nn_getReturn(c, i));
    }
    nn_resetCall(c);
    return retc;
}

int testLuaArch_unicode_sub(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    nn_Alloc *alloc = testLuaArch_getAlloc(L);
    int start = luaL_checkinteger(L, 2);
    int len = nn_unicode_lenPermissive(s);
    if(len < 0) {
        luaL_error(L, "length overflow");
    }
    int stop = len;
    if(lua_isinteger(L, 3)) {
        stop = luaL_checkinteger(L, 3);
    }
    // OpenOS does this...
    if(len == 0) {
        lua_pushstring(L, "");
        return 1;
    }

    if(start == 0) start = 1;
    if(stop == 0) {
        lua_pushstring(L, "");
        return 1;
    }
    if(start < 0) start = len + start + 1;
    if(stop < 0) stop = len + stop + 1;

    if(stop >= len) {
        stop = len;
    }
    
    if(start > stop) {
        lua_pushstring(L, "");
        return 1;
    }

    nn_size_t startByte = nn_unicode_indexPermissive(s, start - 1);
    nn_size_t termByte = nn_unicode_indexPermissive(s, stop);
    const char *res = testLuaArch_pushlstring(L, s + startByte, termByte - startByte);
    if (!res) {
        luaL_error(L, "out of memory");
    }

    return 1;
}

int testLuaArch_unicode_char(lua_State *L) {
    int argc = lua_gettop(L);
    nn_Alloc *alloc = testLuaArch_getAlloc(L);
    unsigned int *codepoints = nn_alloc(alloc, sizeof(unsigned int) * argc);
    if(codepoints == NULL) {
        luaL_error(L, "out of memory");
        return 0; // tell lsp to shut the fuck up
    }
    for(int i = 0; i < argc; i++) {
        int idx = i + 1;
        if(!lua_isinteger(L, idx)) {
            nn_dealloc(alloc, codepoints, sizeof(unsigned int) * argc);
            luaL_argerror(L, idx, "integer expected");
            return 0;
        }
        codepoints[i] = lua_tointeger(L, idx);
    }
    char *s = nn_unicode_char(alloc, codepoints, argc);
    const char *res = testLuaArch_pushstring(L, s);
    nn_deallocStr(alloc, s);
    nn_dealloc(alloc, codepoints, sizeof(unsigned int) * argc);
    if (!res) {
        luaL_error(L, "out of memory");
    }
    return 1;
}

int testLuaArch_unicode_len(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    lua_pushinteger(L, nn_unicode_lenPermissive(s));
    return 1;
}

int testLuaArch_unicode_wlen(lua_State *L) {
    const char *s = luaL_checkstring(L, 1);
    lua_pushinteger(L, nn_unicode_lenPermissive(s));
    return 1;
}

void testLuaArch_loadEnv(lua_State *L) {
    lua_createtable(L, 0, 10);
    int computer = lua_gettop(L);
    lua_pushcfunction(L, testLuaArch_computer_clearError);
    lua_setfield(L, computer, "clearError");
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
    lua_pushcfunction(L, testLuaArch_computer_isOverworked);
    lua_setfield(L, computer, "isOverworked");
    lua_pushcfunction(L, testLuaArch_computer_isOverheating);
    lua_setfield(L, computer, "isOverheating");
    lua_pushcfunction(L, testLuaArch_computer_getTemperature);
    lua_setfield(L, computer, "getTemperature");
    lua_pushcfunction(L, testLuaArch_computer_addHeat);
    lua_setfield(L, computer, "addHeat");
    lua_pushcfunction(L, testLuaArch_computer_pushSignal);
    lua_setfield(L, computer, "pushSignal");
    lua_pushcfunction(L, testLuaArch_computer_popSignal);
    lua_setfield(L, computer, "popSignal");
    lua_pushcfunction(L, testLuaArch_computer_users);
    lua_setfield(L, computer, "users");
    lua_pushcfunction(L, testLuaArch_computer_getState);
    lua_setfield(L, computer, "getState");
    lua_pushcfunction(L, testLuaArch_computer_setState);
    lua_setfield(L, computer, "setState");
    lua_pushcfunction(L, testLuaArch_computer_getDeviceInfo);
    lua_setfield(L, computer, "getDeviceInfo");
    lua_setglobal(L, "computer");

    lua_createtable(L, 0, 10);
    int component = lua_gettop(L);
    lua_pushcfunction(L, testLuaArch_component_list);
    lua_setfield(L, component, "list");
    lua_pushcfunction(L, testLuaArch_component_doc);
    lua_setfield(L, component, "doc");
    lua_pushcfunction(L, testLuaArch_component_fields);
    lua_setfield(L, component, "fields");
    lua_pushcfunction(L, testLuaArch_component_methods);
    lua_setfield(L, component, "methods");
    lua_pushcfunction(L, testLuaArch_component_invoke);
    lua_setfield(L, component, "invoke");
    lua_pushcfunction(L, testLuaArch_component_slot);
    lua_setfield(L, component, "slot");
    lua_pushcfunction(L, testLuaArch_component_type);
    lua_setfield(L, component, "type");
    lua_setglobal(L, "component");

    lua_createtable(L, 0, 7);
    int states = lua_gettop(L);
    lua_pushinteger(L, NN_STATE_SETUP);
    lua_setfield(L, states, "setup");
    lua_pushinteger(L, NN_STATE_RUNNING);
    lua_setfield(L, states, "running");
    lua_pushinteger(L, NN_STATE_BUSY);
    lua_setfield(L, states, "busy");
    lua_pushinteger(L, NN_STATE_BLACKOUT);
    lua_setfield(L, states, "blackout");
    lua_pushinteger(L, NN_STATE_CLOSING);
    lua_setfield(L, states, "closing");
    lua_pushinteger(L, NN_STATE_REPEAT);
    lua_setfield(L, states, "REPEAT");
    lua_pushinteger(L, NN_STATE_SWITCH);
    lua_setfield(L, states, "switch");
    lua_setglobal(L, "states");

    lua_createtable(L, 0, 20);
    int unicode = lua_gettop(L);
    lua_pushcfunction(L, testLuaArch_unicode_sub);
    lua_setfield(L, unicode, "sub");
    lua_pushcfunction(L, testLuaArch_unicode_len);
    lua_setfield(L, unicode, "len");
    lua_pushcfunction(L, testLuaArch_unicode_wlen);
    lua_setfield(L, unicode, "wlen");
    lua_pushcfunction(L, testLuaArch_unicode_char);
    lua_setfield(L, unicode, "char");
    lua_setglobal(L, "unicode");
}

testLuaArch *testLuaArch_setup(nn_computer *computer, void *_) {
    nn_Alloc *alloc = nn_getAllocator(nn_getUniverse(computer));
    testLuaArch *s = nn_alloc(alloc, sizeof(testLuaArch));
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
    if(luaL_loadbufferx(L, testLuaSandbox, strlen(testLuaSandbox), "=machine.lua", "t") != LUA_OK) {
        lua_close(L);
        nn_dealloc(alloc, s, sizeof(testLuaArch));
        return NULL;
    }
    return s;
}

void testLuaArch_teardown(nn_computer *computer, testLuaArch *arch, void *_) {
    nn_Alloc *alloc = nn_getAllocator(nn_getUniverse(computer));
    lua_close(arch->L);
    nn_dealloc(alloc, arch, sizeof(testLuaArch));
}

void testLuaArch_tick(nn_computer *computer, testLuaArch *arch, void *_) {
    int ret = 0;
#if LUA_VERSION_NUM == 504
    int res = lua_resume(arch->L, NULL, 0, &ret);
#endif
#if LUA_VERSION_NUM == 503
    int res = lua_resume(arch->L, NULL, 0);
#endif
#if LUA_VERSION_NUM == 502
    int res = lua_resume(arch->L, NULL, 0);
#endif
    if(res == LUA_OK) {
        // machine halted, this is no good
        lua_pop(arch->L, ret);
        nn_setCError(computer, "machine halted");
    } else if(res == LUA_YIELD) {
        lua_pop(arch->L, ret);
    } else {
        const char *s = lua_tostring(arch->L, -1);
        nn_setError(computer, s);
        lua_pop(arch->L, ret);
    }
}

size_t testLuaArch_getMemoryUsage(nn_computer *computer, testLuaArch *arch, void *_) {
    return arch->memoryUsed;
}

char *testLuaArch_serialize(nn_computer *computer, nn_Alloc *alloc, testLuaArch *arch, void *_, size_t *len) {
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
        testLuaSandbox = malloc(l+1);
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
