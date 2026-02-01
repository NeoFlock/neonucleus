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
	for(size_t i = 0; i < NN_BUILTIN_COUNT; i++) nn_destroyComponentType(universe->types[i]);
	nn_free(&ctx, universe, sizeof(nn_Universe));
}

nn_ComponentType *nn_createComponentType(nn_Universe *universe, const char *name, void *userdata, const nn_ComponentMethod methods[], nn_ComponentHandler *handler) {
	nn_Context *ctx = &universe->ctx;
	char *namecpy = nn_strdup(ctx, name);
	if(namecpy == NULL) return NULL;

	size_t methodCount = 0;
	while(methods[methodCount].name != NULL) methodCount++;
	// include the terminator!
	methodCount++;
	size_t methodSize = sizeof(nn_ComponentMethod) * methodCount;

	nn_ComponentMethod *methodscpy = nn_alloc(ctx, methodSize);
	if(methodscpy == NULL) {
		nn_strfree(ctx, namecpy);
		return NULL;
	}

	{
		// in an ideal world, I'd just implement arenas.
		// I should just implement arenas ngl
		// TODO: just use arenas so memory management is ultra free
		size_t methodIdx = 0;
		while(methodIdx < methodCount) {
			if(methods[methodIdx].name == NULL) {
				methodscpy[methodIdx].name = NULL;
				methodscpy[methodIdx].docString = NULL;
				methodscpy[methodIdx].direct = false;
				continue;
			}
			char *namecpy = nn_strdup(ctx, methods[methodIdx].name);
			if(namecpy == NULL) goto methodOom;
			char *doc = nn_strdup(ctx, methods[methodIdx].docString);
			if(doc == NULL) {
				nn_strfree(ctx, namecpy);
				goto methodOom;
			}
			methodscpy[methodIdx].name = namecpy;
			methodscpy[methodIdx].docString = doc;
			methodscpy[methodIdx].direct = methods[methodIdx].direct;
			methodIdx++;
		}
		goto normalExec;
	methodOom:
		for(size_t i = 0; i < methodIdx; i++) {
			nn_strfree(ctx, (char *)methodscpy[i].name);
			nn_strfree(ctx, (char *)methodscpy[i].docString);
		}
		return NULL;
	}
normalExec:;

	nn_ComponentType *ctype = nn_alloc(ctx, sizeof(nn_ComponentType));
	if(ctype == NULL) {
		nn_strfree(ctx, namecpy);
		nn_free(ctx, methodscpy, methodSize);
		return NULL;
	}

	ctype->name = namecpy;
	ctype->methods = methodscpy;
	ctype->universe = universe;
	return ctype;
}

void nn_destroyComponentType(nn_ComponentType *ctype) {
	nn_Context *ctx = &ctype->universe->ctx;

	nn_strfree(ctx, ctype->name);
	
	size_t methodCount = 0;
	while(ctype->methods[methodCount].name != NULL) {
		nn_strfree(ctx, (char *)ctype->methods[methodCount].name);
		nn_strfree(ctx, (char *)ctype->methods[methodCount].docString);
		methodCount++;
	}
	// include the terminator!
	methodCount++;
	size_t methodSize = sizeof(nn_ComponentMethod) * methodCount;
	nn_free(ctx, ctype->methods, methodSize);

	nn_free(ctx, ctype, sizeof(nn_ComponentType));
}
