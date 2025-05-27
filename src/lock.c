#include "neonucleus.h"
#include "tinycthread.h"

typedef struct nn_guard {
    mtx_t m;
} nn_guard;

nn_guard *nn_newGuard() {
    nn_guard *g = nn_malloc(sizeof(nn_guard));
    if(g == NULL) return NULL;
    mtx_init(&g->m, mtx_recursive);
    return g;
}

void nn_lock(nn_guard *guard) {
    if(guard == NULL) return;
    mtx_lock(&guard->m);
}

void nn_unlock(nn_guard *guard) {
    if(guard == NULL) return;
    mtx_unlock(&guard->m);
}

void nn_deleteGuard(nn_guard *guard) {
    if(guard == NULL) return;
    mtx_destroy(&guard->m);
}

void nn_addRef(nn_refc *refc, size_t count) {
    atomic_fetch_add(refc, count);
}

void nn_incRef(nn_refc *refc) {
    nn_addRef(refc, 1);
}

bool nn_removeRef(nn_refc *refc, size_t count) {
    size_t old = atomic_fetch_sub(refc, count);
    return old == count;
}

bool nn_decRef(nn_refc *refc) {
    return nn_removeRef(refc, 1);
}
