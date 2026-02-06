// The main file of the test emulator
// This is not a serious emulator intended for practical use,
// it is simply just to test stuff and showcase the API.
// Error handling has been omitted in most places.

#include "neonucleus.h"
#include <stdio.h>
#include <string.h>

nn_Architecture getLuaArch();

static const char minBIOS[] = {
#embed "minBIOS.lua"
,'\0'
};

static nn_Exit sandbox_handler(nn_ComponentRequest *req) {
	nn_Computer *c = req->computer;
	switch(req->action) {
	case NN_COMP_INIT:
		return NN_OK;
	case NN_COMP_DEINIT:
		return NN_OK;
	case NN_COMP_CALL:
		if(nn_getstacksize(c) != 1) {
			nn_setError(c, "bad argument count");
			return NN_EBADCALL;
		}
		const char *s = nn_tostring(c, 0);
		puts(s);
		return NN_OK;
	case NN_COMP_ENABLED:
		req->methodEnabled = true; // all methods always enabled
		return NN_OK;
	case NN_COMP_FREETYPE:
		return NN_OK;
	}
	return NN_OK;
}

int main() {
	nn_Context ctx;
	nn_initContext(&ctx);

	// create the universe
	nn_Universe *u = nn_createUniverse(&ctx);

	nn_Architecture arch = getLuaArch();

	nn_Method sandboxMethods[] = {
		{"log", "log(msg: string) - Log to stdout", true},
		{NULL},
	};
	nn_ComponentType *ctype = nn_createComponentType(u, "sandbox", NULL, sandboxMethods, sandbox_handler);

	nn_EEPROM eeprom = {
		.size = 4 * NN_KiB,
		.dataSize = NN_KiB,
		.readCallCost = 1,
		.readEnergyCost = 0,
		.readDataCallCost = 1,
		.readDataEnergyCost = 0,
		.writeCallCost = 1,
		.writeEnergyCost = 0,
		.writeDataCallCost = 1,
		.writeDataEnergyCost = 0,
		.handler = NULL,
	};
	nn_VEEPROM veeprom = {
		.code = minBIOS,
		.codelen = strlen(minBIOS),
		.data = NULL,
		.datalen = 0,
		.label = NULL,
		.labellen = 0,
		.arch = NULL,
		.isReadonly = false,
	};

	nn_ComponentType *etype = nn_createVEEPROM(u, &eeprom, &veeprom);

	nn_Computer *c = nn_createComputer(u, NULL, "computer0", 8 * NN_MiB, 256, 256);
	
	nn_setArchitecture(c, &arch);
	nn_addSupportedArchitecture(c, &arch);

	nn_addComponent(c, ctype, "sandbox", -1, NULL);
	nn_addComponent(c, etype, "eeprom", 0, etype);
	
	while(true) {
		nn_Exit e = nn_tick(c);
		if(e != NN_OK) {
			nn_setErrorFromExit(c, e);
			printf("error: %s\n", nn_getError(c));
			goto cleanup;
		}

		nn_ComputerState state = nn_getComputerState(c);
		if(state == NN_POWEROFF) break;
		if(state == NN_CRASHED) {
			printf("error: %s\n", nn_getError(c));
			goto cleanup;
		}

		if(state == NN_CHARCH) {
			printf("new arch: %s\n", nn_getDesiredArchitecture(c).name);
			goto cleanup;
		}
		if(state == NN_BLACKOUT) {
			printf("out of energy\n");
			goto cleanup;
		}
		if(state == NN_RESTART) {
			printf("restart requested\n");
			goto cleanup;
		}
	}

cleanup:;
	nn_destroyComputer(c);
	nn_destroyComponentType(ctype);
	nn_destroyComponentType(etype);
	// rip the universe
	nn_destroyUniverse(u);
	return 0;
}
