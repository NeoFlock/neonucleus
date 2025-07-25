#ifndef NN_RESOURCE
#define NN_RESOURCE

#include "neonucleus.h"

typedef struct nn_resourceMethod_t {
	const char *name;
	const char *doc;
	void *userdata;
	nn_resourceMethodCallback_t *callback;
	nn_resourceMethodCondition_t *condition;
} nn_resourceMethod_t;

typedef struct nn_resourceTable_t {
	nn_Context ctx;
	nn_resourceDestructor_t *dtor;
	nn_size_t methodCount;
	nn_resourceMethod_t methods[NN_MAX_METHODS];
} nn_resourceTable_t;

#endif
