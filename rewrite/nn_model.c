#include "neonucleus.h"
#include "nn_model.h"
#include "nn_utils.h"

nn_Universe *nn_createUniverse(nn_Context *ctx) {
	nn_Universe *u = nn_alloc(ctx, sizeof(nn_Universe));
	if(u == NULL) return NULL;
	u->ctx = *ctx;
	for(size_t i = 0; i < NN_BUILTIN_COUNT; i++) u->types[i] = NULL;
	return u;
}

void nn_destroyUniverse(nn_Universe *universe) {
	nn_Context ctx = universe->ctx;
	nn_free(&ctx, universe, sizeof(nn_Universe));
}
