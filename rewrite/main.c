// The main file of the test emulator
// This is not a serious emulator intended for practical use,
// it is simply just to test stuff and showcase the API.

#include "neonucleus.h"
#include <stdio.h>

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
	}
	return NN_OK;
}

static nn_Exit sandbox_arch(nn_ArchitectureRequest *req) {
	nn_Computer *c = req->computer;
	switch(req->action) {
	case NN_ARCH_INIT:
		return NN_OK;
	case NN_ARCH_DEINIT:
		return NN_OK;
	case NN_ARCH_FREEMEM:
		req->freeMemory = 0;
		return NN_OK;
	case NN_ARCH_TICK:
		nn_pushstring(c, "Hello from component call");
		nn_call(c, "sandbox", "log");
		return NN_OK;
	}
	return NN_OK;
}

int main() {
	nn_Context ctx;
	nn_initContext(&ctx);

	// create the universe
	nn_Universe *u = nn_createUniverse(&ctx);

	nn_Architecture arch = {
		.name = "Sandbox test",
		.state = NULL,
		.handler = sandbox_arch,
	};

	nn_ComponentMethod sandboxMethods[] = {
		{"log", "log(msg: string) - Log to stdout", true},
		{NULL},
	};
	nn_ComponentType *ctype = nn_createComponentType(u, "sandbox", NULL, sandboxMethods, sandbox_handler);

	nn_Computer *c = nn_createComputer(u, NULL, "computer0", 8 * NN_MiB, 256, 256);
	
	nn_setArchitecture(c, &arch);

	nn_addComponent(c, ctype, "sandbox", -1, NULL);

	printf("Component added: %s\n", nn_hasComponent(c, "sandbox") ? "TRUE" : "FALSE");
	printf("Method active: %s\n", nn_hasMethod(c, "sandbox", "log") ? "TRUE" : "FALSE");
	printf("Incorrect method active: %s\n", nn_hasMethod(c, "sandbox", "notlog") ? "TRUE" : "FALSE");

	nn_destroyComputer(c);
	nn_destroyComponentType(ctype);
	// rip the universe
	nn_destroyUniverse(u);
	return 0;
}
