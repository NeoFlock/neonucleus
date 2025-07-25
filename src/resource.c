#include "resource.h"

nn_resourceTable_t *nn_resource_newTable(nn_Context *ctx, nn_resourceDestructor_t *dtor) {
	nn_resourceTable_t *t = nn_alloc(&ctx->allocator, sizeof(nn_resourceTable_t));
	if(t == NULL) return NULL;
	t->dtor = dtor;
	t->methodCount = 0;
	return t;
}

nn_resourceMethod_t *nn_resource_addMethod(nn_resourceTable_t *table, const char *methodName, nn_resourceMethodCallback_t *method, const char *doc) {
	if(table->methodCount == NN_MAX_METHODS) return NULL;
	nn_resourceMethod_t *m = &table->methods[table->methodCount];
	table->methodCount++;
	nn_Alloc *a = &table->ctx.allocator;
	m->name = nn_strdup(a, methodName);
	m->doc = nn_strdup(a, doc);
	m->callback = method;
	m->condition = NULL;
	return m;
}

void nn_resource_setUserdata(nn_resourceMethod_t *method, void *methodUserdata) {
	method->userdata = methodUserdata;
}

void nn_resource_setCondition(nn_resourceMethod_t *method, nn_resourceMethodCondition_t *methodCondition) {
	method->condition = methodCondition;
}
