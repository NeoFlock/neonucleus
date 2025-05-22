#include "neonucleus.h"
#include "tinycthread.h"

typedef struct nn_guard {
    mtx_t m;
} nn_guard;

nn_guard *nn_newGuard() {
    nn_guard *g = nn_malloc(sizeof(nn_guard));
    mtx_init(&g->m, mtx_recursive);
    return g;
}

void nn_lock(nn_guard *guard) {
    mtx_lock(&guard->m);
}

void nn_unlock(nn_guard *guard) {
    mtx_unlock(&guard->m);
}

void nn_deleteGuard(nn_guard *guard) {
    mtx_destroy(&guard->m);
}
