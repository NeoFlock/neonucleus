#include "neonucleus.h"
#include <lualib.h>
#include <stdlib.h>
#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>
#include <string.h>

// This architecture is written horrendously.
// This code is garbage, and is entirely just for testing.
// This architecture has effectively 0 sandboxing.
// The error handling in this architecture is effectively nothing.
// Also if the total memory available changes during execution, this Lua arch will NOT CARE.

const char luaArch_machineLua[] = {
#embed "machine.lua"
,'\0'
};

typedef struct luaArch {
	nn_Computer *computer;
	lua_State *L;
	size_t freeMem;
} luaArch;

void *luaArch_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
	luaArch *arch = ud;
	if(nsize == 0) {
		free(ptr);
		arch->freeMem += osize;
		return NULL;
	}
	if(ptr == NULL) {
		//if(arch->freeMem < nsize) return NULL;
		void *mem = malloc(nsize);
		if(mem == NULL) return NULL;
		arch->freeMem -= nsize;
		return mem;
	}
	//if(arch->freeMem + osize < nsize) return NULL;
	void *mem = realloc(ptr, nsize);
	if(mem == NULL) return NULL;
	arch->freeMem += osize;
	arch->freeMem -= nsize;
	return mem;
}

static luaArch *luaArch_from(lua_State *L) {
	lua_getfield(L, LUA_REGISTRYINDEX, "archPtr");
	luaArch *arch = lua_touserdata(L, -1);
	lua_pop(L, 1);
	return arch;
}

// pushes an NN value from a Lua stack index
static nn_Exit luaArch_luaToNN(luaArch *arch, lua_State *L, int luaIdx) {
	nn_Computer *C = arch->computer;

	if(lua_isnoneornil(L, luaIdx)) {
		return nn_pushnull(C);
	}
	if(lua_isnumber(L, luaIdx)) {
		return nn_pushnumber(C, lua_tonumber(L, luaIdx));
	}
	if(lua_isstring(L, luaIdx)) {
		size_t len;
		const char *s = lua_tolstring(L, luaIdx, &len);
		return nn_pushlstring(C, s, len);
	}
	if(lua_isboolean(L, luaIdx)) {
		return nn_pushbool(C, lua_toboolean(L, luaIdx));
	}
	luaL_error(L, "bad Lua value: %s", luaL_typename(L, luaIdx));
	return NN_EBADSTATE;
}

// pushes a Lua value from an NN stack index
static void luaArch_nnToLua(luaArch *arch, lua_State *L, size_t nnIdx) {
	nn_Computer *C = arch->computer;

	if(nn_isnull(C, nnIdx)) {
		lua_pushnil(L);
		return;
	}
	if(nn_isnumber(C, nnIdx)) {
		lua_pushnumber(L, nn_tonumber(C, nnIdx));
		return;
	}
	if(nn_isstring(C, nnIdx)) {
		size_t len;
		const char *s = nn_tolstring(C, nnIdx, &len);
		lua_pushlstring(L, s, len);
		return;
	}
	if(nn_isboolean(C, nnIdx)) {
		lua_pushboolean(L, nn_toboolean(C, nnIdx));
		return;
	}
	if(nn_istable(C, nnIdx)) {
		size_t start = nn_getstacksize(C);
		size_t len;
		nn_dumptable(C, nnIdx, &len);
		lua_createtable(L, 0, len);
		for(size_t i = 0; i < len; i++) {
			luaArch_nnToLua(arch, L, start + i * 2);
			luaArch_nnToLua(arch, L, start + i * 2 + 1);
			lua_settable(L, -3);
		}
		nn_popn(C, len * 2);
		return;
	}

	luaL_error(L, "bad NN value: %s", nn_typenameof(C, nnIdx));
}

static int luaArch_computer_freeMemory(lua_State *L) {
	lua_pushinteger(L, luaArch_from(L)->freeMem);
	return 1;
}

static int luaArch_computer_totalMemory(lua_State *L) {
	lua_pushinteger(L, nn_getTotalMemory(luaArch_from(L)->computer));
	return 1;
}

static int luaArch_computer_uptime(lua_State *L) {
	lua_pushnumber(L, nn_getUptime(luaArch_from(L)->computer));
	return 1;
}

static int luaArch_computer_address(lua_State *L) {
	const char *addr = nn_getComputerAddress(luaArch_from(L)->computer);
	lua_pushstring(L, addr);
	return 1;
}

static int luaArch_computer_tmpAddress(lua_State *L) {
	const char *addr = nn_getTmpAddress(luaArch_from(L)->computer);
	if(addr == NULL) lua_pushnil(L);
	else lua_pushstring(L, addr);
	return 1;
}

static int luaArch_computer_users(lua_State *L) {
	nn_Computer *c = luaArch_from(L)->computer;
	size_t userc = 0;
	while(1) {
		const char *user = nn_getUser(c, userc);
		if(user == NULL) break;
		lua_pushstring(L, user);
		userc++;
	}
	return userc;
}

static int luaArch_computer_addUser(lua_State *L) {
	nn_Computer *c = luaArch_from(L)->computer;
	const char *user = luaL_checkstring(L, 1);
	nn_Exit err = nn_addUser(c, user);
	if(err) {
		nn_setErrorFromExit(c, err);
		lua_pushnil(L);
		lua_pushstring(L, nn_getError(c));
		return 2;
	}
	lua_pushboolean(L, true);
	return 1;
}

static int luaArch_computer_removeUser(lua_State *L) {
	nn_Computer *c = luaArch_from(L)->computer;
	const char *user = luaL_checkstring(L, 1);
	lua_pushboolean(L, nn_removeUser(c, user));
	return 1;
}

static int luaArch_computer_energy(lua_State *L) {
	lua_pushnumber(L, nn_getEnergy(luaArch_from(L)->computer));
	return 1;
}

static int luaArch_computer_maxEnergy(lua_State *L) {
	lua_pushnumber(L, nn_getTotalEnergy(luaArch_from(L)->computer));
	return 1;
}

static int luaArch_computer_getArchitecture(lua_State *L) {
	lua_pushstring(L, nn_getArchitecture(luaArch_from(L)->computer).name);
	return 1;
}

static int luaArch_computer_setArchitecture(lua_State *L) {
	nn_Computer *c = luaArch_from(L)->computer;
	const char *archname = luaL_checkstring(L, 1);
	nn_Architecture arch = nn_findSupportedArchitecture(c, archname);
	if(arch.name == NULL) {
		lua_pushnil(L);
		lua_pushstring(L, "unknown architecture");
		return 2;
	}
	nn_setComputerState(c, NN_CHARCH);
	nn_setDesiredArchitecture(c, &arch);
	return 0;
}

static int luaArch_computer_getArchitectures(lua_State *L) {
	nn_Computer *c = luaArch_from(L)->computer;
	size_t len;
	const nn_Architecture *arch = nn_getSupportedArchitectures(c, &len);
	lua_createtable(L, len, 0);
	for(size_t i = 0; i < len; i++) {
		lua_pushstring(L, arch[i].name);
		lua_seti(L, -2, i+1);
	}
	return 1;
}

static int luaArch_computer_shutdown(lua_State *L) {
	nn_Computer *c = luaArch_from(L)->computer;
	bool restart = lua_toboolean(L, 1);
	nn_setComputerState(c, restart ? NN_RESTART : NN_POWEROFF);
	return 0;
}

static int luaArch_computer_isOverused(lua_State *L) {
	nn_Computer *c = luaArch_from(L)->computer;
	lua_pushboolean(L, nn_componentsOverused(c));
	return 1;
}

static int luaArch_computer_pushSignal(lua_State *L) {
	luaArch *arch = luaArch_from(L);
	nn_Computer *c = arch->computer;
	size_t signalCount = lua_gettop(L);
	nn_Exit err;
	for(int i = 1; i <= signalCount; i++) {
		err = luaArch_luaToNN(arch, L, i);
		if(err) { 
			nn_setErrorFromExit(c, err);
			luaL_error(L, "%s", nn_getError(c));
		}
	}
	err = nn_pushSignal(c, signalCount);
	if(err) {
		nn_setErrorFromExit(c, err);
		luaL_error(L, "%s", nn_getError(c));
	}
	return 0;
}

static int luaArch_computer_popSignal(lua_State *L) {
	luaArch *arch = luaArch_from(L);
	nn_Computer *c = arch->computer;
	// no signals queued
	if(nn_countSignals(c) == 0) return 0;
	nn_clearstack(c);
	size_t signalCount;
	nn_Exit err = nn_popSignal(c, &signalCount);
	if(err) goto fail;
	for(size_t i = 0; i < signalCount; i++) {
		luaArch_nnToLua(arch, L, i);
	}
	nn_clearstack(c);
	return signalCount;
fail:
	nn_setErrorFromExit(c, err);
	luaL_error(L, "%s", nn_getError(c));
	return 0;
}

static int luaArch_component_list(lua_State *L) {
	luaArch *arch = luaArch_from(L);
	lua_createtable(L, 64, 0);
	for(size_t i = 0; true; i++) {
		const char *addr = nn_getComponentAddress(arch->computer, i);
		if(addr == NULL) break;
		lua_pushstring(L, nn_getComponentType(arch->computer, addr));
		lua_setfield(L, -2, addr);
	}
	return 1;
}

static int luaArch_component_invoke(lua_State *L) {
	luaArch *arch = luaArch_from(L);
	const char *address = luaL_checkstring(L, 1);
	const char *method = luaL_checkstring(L, 2);
	size_t argc = lua_gettop(L);
	
	if(!nn_hasComponent(arch->computer, address)) {
		lua_pushnil(L);
		lua_pushstring(L, "no such component");
		return 2;
	}
	if(!nn_hasMethod(arch->computer, address, method)) {
		lua_pushnil(L);
		lua_pushstring(L, "no such method");
		return 2;
	}
	
	nn_clearstack(arch->computer);
	for(size_t i = 3; i <= argc; i++) {
		luaArch_luaToNN(arch, L, i);
	}
	nn_Exit err = nn_call(arch->computer, address, method);
	if(err != NN_OK) {
		lua_pushnil(L);
		lua_pushstring(L, nn_getError(arch->computer));
		return 2;
	}
	size_t retc = nn_getstacksize(arch->computer);
	for(size_t i = 0; i < retc; i++) {
		luaArch_nnToLua(arch, L, i);
	}
	nn_clearstack(arch->computer);
	return retc;
}

static int luaArch_component_type(lua_State *L) {
	luaArch *arch = luaArch_from(L);
	const char *address = luaL_checkstring(L, 1);

	if(!nn_hasComponent(arch->computer, address)) {
		lua_pushnil(L);
		lua_pushstring(L, "no such component");
		return 2;
	}
	lua_pushstring(L, nn_getComponentType(arch->computer, address));
	return 1;
}

static int luaArch_component_doc(lua_State *L) {
	luaArch *arch = luaArch_from(L);
	const char *address = luaL_checkstring(L, 1);
	const char *method = luaL_checkstring(L, 2);

	if(!nn_hasComponent(arch->computer, address)) {
		lua_pushnil(L);
		lua_pushstring(L, "no such component");
		return 2;
	}
	if(!nn_hasMethod(arch->computer, address, method)) {
		lua_pushnil(L);
		lua_pushstring(L, "no such method");
		return 2;
	}
	lua_pushstring(L, nn_getComponentDoc(arch->computer, address, method));
	return 1;
}

static int luaArch_component_slot(lua_State *L) {
	luaArch *arch = luaArch_from(L);
	const char *address = luaL_checkstring(L, 1);

	if(!nn_hasComponent(arch->computer, address)) {
		lua_pushnil(L);
		lua_pushstring(L, "no such component");
		return 2;
	}
	lua_pushinteger(L, nn_getComponentSlot(arch->computer, address));
	return 1;
}

static int luaArch_component_methods(lua_State *L) {
	luaArch *arch = luaArch_from(L);
	const char *address = luaL_checkstring(L, 1);

	if(!nn_hasComponent(arch->computer, address)) {
		lua_pushnil(L);
		lua_pushstring(L, "no such component");
		return 2;
	}
	size_t len;
	const nn_Method *methods = nn_getComponentMethods(arch->computer, address, &len);
	lua_createtable(L, 0, len);
	for(size_t i = 0; i < len; i++) {
		if(methods[i].flags & NN_FIELD_MASK) continue; // skip

		lua_pushboolean(L, (methods[i].flags & NN_DIRECT) != 0);
		lua_setfield(L, -2, methods[i].name);
	}
	return 1;
}

static int luaArch_component_fields(lua_State *L) {
	luaArch *arch = luaArch_from(L);
	const char *address = luaL_checkstring(L, 1);

	if(!nn_hasComponent(arch->computer, address)) {
		lua_pushnil(L);
		lua_pushstring(L, "no such component");
		return 2;
	}
	size_t len;
	const nn_Method *methods = nn_getComponentMethods(arch->computer, address, &len);
	lua_createtable(L, 0, len);
	for(size_t i = 0; i < len; i++) {
		if((methods[i].flags & NN_FIELD_MASK) == 0) continue; // skip

		lua_createtable(L, 0, 3);
		lua_pushboolean(L, (methods[i].flags & NN_DIRECT) != 0);
		lua_setfield(L, -2, "direct");
		lua_pushboolean(L, (methods[i].flags & NN_GETTER) != 0);
		lua_setfield(L, -2, "getter");
		lua_pushboolean(L, (methods[i].flags & NN_SETTER) != 0);
		lua_setfield(L, -2, "setter");
		lua_setfield(L, -2, methods[i].name);
	}
	return 1;
}

static int luaArch_unicode_char(lua_State *L) {
	size_t argc = lua_gettop(L);
	size_t len = 0;
	for(int i = 1; i <= argc; i++) {
		nn_codepoint codepoint = lua_tointeger(L, i);
		size_t size = nn_unicode_codepointSize(codepoint);
		if(size == 0) luaL_error(L, "codepoint #%d out of range", i);
		len += size;
	}
	nn_Context *ctx = nn_getComputerContext(luaArch_from(L)->computer);
	char *buf = nn_alloc(ctx, len);
	size_t buflen = len;
	len = 0;
	for(int i = 1; i <= argc; i++) {
		nn_codepoint codepoint = lua_tointeger(L, i);
		size_t size = nn_unicode_codepointToChar(buf + len, codepoint);
		len += size;
	}
	lua_pushlstring(L, buf, len);
	nn_free(ctx, buf, buflen);
	return 1;
}

static int luaArch_unicode_len(lua_State *L) {
	size_t len;
	const char *s = lua_tolstring(L, 1, &len);
	len = nn_unicode_lenPermissive(s, len);
	lua_pushinteger(L, len);
	return 1;
}

static int luaArch_unicode_sub(lua_State *L) {
	size_t slen;
	const char *s = lua_tolstring(L, 1, &slen);
	if(lua_gettop(L) < 2) lua_pushinteger(L, 1);
	if(lua_gettop(L) < 3) lua_pushinteger(L, -1);

	size_t len = nn_unicode_lenPermissive(s, slen);
   
	// OpenOS does this...
	if(len == 0) {
        lua_pushstring(L, "");
        return 1;
    }

	int start = lua_tointeger(L, 2);
	int end = lua_tointeger(L, 3);

	if(end == 0) {
		lua_pushstring(L, "");
		return 1;
	}

	if(start > 0) start--;
	if(start < 0) start = len + start;
	if(end > 0) end--;
	if(end < 0) end = len + end;

	if(start < 0) start = 0;
	if(start >= len) start = len-1;
	if(end < 0) end = 0;
	if(end >= len) end = len-1;

	if(start > end) {
		lua_pushstring(L, "");
		return 1;
	}

	nn_Context *ctx = nn_getComputerContext(luaArch_from(L)->computer);
	nn_codepoint *cp = nn_alloc(ctx, sizeof(*cp) * len);
	nn_unicode_codepointsPermissive(s, slen, cp);

	size_t substrlen = nn_unicode_countBytes(cp + start, end - start + 1);
	char *buf = nn_alloc(ctx, substrlen);
	nn_unicode_writeBytes(buf, cp + start, end - start + 1);
	lua_pushlstring(L, buf, substrlen);
	nn_free(ctx, buf, substrlen);
	nn_free(ctx, cp, sizeof(*cp) * len);
	return 1;
}

static int luaArch_unicode_wlen(lua_State *L) {
	size_t slen;
	const char *s = lua_tolstring(L, 1, &slen);
	size_t len = nn_unicode_wlenPermissive(s, slen);
	lua_pushinteger(L, len);
	return 1;
}

static int luaArch_unicode_wtrunc(lua_State *L) {
	lua_pushvalue(L, 1);
	return 1;
}

static void luaArch_loadEnv(lua_State *L) {
	lua_createtable(L, 0, 10);
	int computer = lua_gettop(L);
	lua_pushcfunction(L, luaArch_computer_freeMemory);
	lua_setfield(L, computer, "freeMemory");
	lua_pushcfunction(L, luaArch_computer_totalMemory);
	lua_setfield(L, computer, "totalMemory");
	lua_pushcfunction(L, luaArch_computer_uptime);
	lua_setfield(L, computer, "uptime");
	lua_pushcfunction(L, luaArch_computer_address);
	lua_setfield(L, computer, "address");
	lua_pushcfunction(L, luaArch_computer_tmpAddress);
	lua_setfield(L, computer, "tmpAddress");
	lua_pushcfunction(L, luaArch_computer_users);
	lua_setfield(L, computer, "users");
	lua_pushcfunction(L, luaArch_computer_addUser);
	lua_setfield(L, computer, "addUser");
	lua_pushcfunction(L, luaArch_computer_removeUser);
	lua_setfield(L, computer, "removeUser");
	lua_pushcfunction(L, luaArch_computer_energy);
	lua_setfield(L, computer, "energy");
	lua_pushcfunction(L, luaArch_computer_maxEnergy);
	lua_setfield(L, computer, "maxEnergy");
	lua_pushcfunction(L, luaArch_computer_getArchitecture);
	lua_setfield(L, computer, "getArchitecture");
	lua_pushcfunction(L, luaArch_computer_getArchitectures);
	lua_setfield(L, computer, "getArchitectures");
	lua_pushcfunction(L, luaArch_computer_setArchitecture);
	lua_setfield(L, computer, "setArchitecture");
	lua_pushcfunction(L, luaArch_computer_shutdown);
	lua_setfield(L, computer, "shutdown");
	lua_pushcfunction(L, luaArch_computer_isOverused);
	lua_setfield(L, computer, "isOverused");
	lua_pushcfunction(L, luaArch_computer_pushSignal);
	lua_setfield(L, computer, "pushSignal");
	lua_pushcfunction(L, luaArch_computer_popSignal);
	lua_setfield(L, computer, "popSignal");
	lua_setglobal(L, "computer");
	lua_createtable(L, 0, 10);
	int component = lua_gettop(L);
	lua_pushcfunction(L, luaArch_component_list);
	lua_setfield(L, component, "list");
	lua_pushcfunction(L, luaArch_component_invoke);
	lua_setfield(L, component, "invoke");
	lua_pushcfunction(L, luaArch_component_doc);
	lua_setfield(L, component, "doc");
	lua_pushcfunction(L, luaArch_component_type);
	lua_setfield(L, component, "type");
	lua_pushcfunction(L, luaArch_component_slot);
	lua_setfield(L, component, "slot");
	lua_pushcfunction(L, luaArch_component_methods);
	lua_setfield(L, component, "methods");
	lua_pushcfunction(L, luaArch_component_fields);
	lua_setfield(L, component, "fields");
	lua_setglobal(L, "component");
	lua_createtable(L, 0, 10);
	int unicode = lua_gettop(L);
	lua_pushcfunction(L, luaArch_unicode_char);
	lua_setfield(L, component, "char");
	lua_pushcfunction(L, luaArch_unicode_len);
	lua_setfield(L, component, "len");
	lua_pushcfunction(L, luaArch_unicode_sub);
	lua_setfield(L, component, "sub");
	lua_pushcfunction(L, luaArch_unicode_len);
	lua_setfield(L, component, "wlen");
	lua_pushcfunction(L, luaArch_unicode_wtrunc);
	lua_setfield(L, component, "wtrunc");
	lua_setglobal(L, "unicode");
}

static nn_Exit luaArch_handler(nn_ArchitectureRequest *req) {
	nn_Computer *computer = req->computer;
	luaArch *arch = req->localState;
	nn_Context *ctx = nn_getComputerContext(computer);
	switch(req->action) {
	case NN_ARCH_FREEMEM:
		req->freeMemory = arch->freeMem;
		return NN_OK;
	case NN_ARCH_INIT:
		// wrapped in a block to prevent L from leaking, because L is common in Lua code so it may be used by mistake
		{
			arch = nn_alloc(ctx, sizeof(*arch));
			arch->freeMem = nn_getTotalMemory(computer);
			arch->computer = computer;
			lua_State *L = luaL_newstate();
			arch->L = L;
			req->localState = arch;
			luaL_openlibs(L);
			lua_pushlightuserdata(L, arch);
			lua_setfield(L, LUA_REGISTRYINDEX, "archPtr");

			luaArch_loadEnv(L);

			lua_settop(L, 0);
			luaL_loadbufferx(L, luaArch_machineLua, strlen(luaArch_machineLua), "=machine.lua", "t");
		}
		return NN_OK;
	case NN_ARCH_DEINIT:
		lua_close(arch->L);
		nn_free(ctx, arch, sizeof(*arch));
		return NN_OK;
	case NN_ARCH_TICK:;
		lua_settop(arch->L, 1);
		int ret = 0;
		int res = lua_resume(arch->L, NULL, 0, &ret);
		//printf("res: %d\n", res);
		if(res == LUA_OK) {
			// halted, fuck
			lua_pop(arch->L, ret);
			nn_setError(computer, "machine halted");
			nn_setComputerState(computer, NN_CRASHED);
			return NN_OK;
		} else if(res != LUA_YIELD) {
			const char *s = lua_tostring(arch->L, -1);
			nn_setError(computer, s);
			nn_setComputerState(computer, NN_CRASHED);
			return NN_OK;
		}
		return NN_OK;
	}
	return NN_OK;
}

nn_Architecture getLuaArch() {
	return (nn_Architecture) {
		.name = LUA_VERSION,
		.state = NULL,
		.handler = luaArch_handler,
	};
}
