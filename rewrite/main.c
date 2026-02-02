// The main file of the test emulator
// This is not a serious emulator intended for practical use,
// it is simply just to test stuff and showcase the API.

#include "neonucleus.h"

int main() {
	nn_Context ctx;
	nn_initContext(&ctx);

	// create the universe
	nn_Universe *u = nn_createUniverse(&ctx);

	nn_ComponentMethod sandboxMethods[] = {
		{"log", "log(msg: string) - Log to stdout", true},
		NULL,
	};
	nn_ComponentType *ctype = nn_createComponentType(u, "sandbox", NULL, sandboxMethods, NULL);

	nn_Computer *c = nn_createComputer(u, NULL, "computer0", 8 * NN_MiB, 256, 256);

cleanup:;
	nn_destroyComponentType(ctype);
	nn_destroyComputer(c);
	// rip the universe
	nn_destroyUniverse(u);
	return 0;
}
