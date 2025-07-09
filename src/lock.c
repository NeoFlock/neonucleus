#include "neonucleus.h"

#ifndef NN_BAREMETAL
#include "tinycthread.h"

static nn_bool_t nni_libcLock(void *_, mtx_t *lock, int action, int flags) {
    if(action == NN_LOCK_INIT) {
        mtx_init(lock, mtx_recursive);
    } else if(action == NN_LOCK_DEINIT) {
        mtx_destroy(lock);
    } else if(action == NN_LOCK_RETAIN) {
        if(flags & NN_LOCK_IMMEDIATE) {
            return mtx_trylock(lock) == thrd_success;
        }
        return mtx_lock(lock);
    } else if(action == NN_LOCK_RELEASE) {
        mtx_unlock(lock);
    }
    return NN_TRUE;
}

nn_LockManager nn_libcMutex() {
    nn_LockManager mgr = {};
    mgr.lockSize = sizeof(mtx_t);
    mgr.userdata = NULL;
    mgr.proc = (nn_LockProc *)nni_libcLock;
    return mgr;
}

#endif

static nn_bool_t nni_noLock(void *_, void *__, int action, int flags) {
    return NN_TRUE;
}

nn_LockManager nn_noMutex() {
    return (nn_LockManager) {
        .userdata = NULL,
        .lockSize = 0,
        .proc = (nn_LockProc *)nni_noLock,
    };
}

nn_guard *nn_newGuard(nn_Context *ctx) {
    nn_guard *g = nn_alloc(&ctx->allocator, ctx->lockManager.lockSize);
    if(g == NULL) return NULL;
    ctx->lockManager.proc(ctx->lockManager.userdata, g, NN_LOCK_INIT, NN_LOCK_DEFAULT);
    return g;
}

void nn_lock(nn_Context *context, nn_guard *guard) {
    if(guard == NULL) return;
    context->lockManager.proc(context->lockManager.userdata, guard, NN_LOCK_RETAIN, NN_LOCK_DEFAULT);
}

nn_bool_t nn_tryLock(nn_Context *context, nn_guard *guard) {
    if(guard == NULL) return NN_TRUE;
    return context->lockManager.proc(context->lockManager.userdata, guard, NN_LOCK_RETAIN, NN_LOCK_IMMEDIATE);
}

void nn_unlock(nn_Context *context, nn_guard *guard) {
    if(guard == NULL) return;
    context->lockManager.proc(context->lockManager.userdata, guard, NN_LOCK_RELEASE, NN_LOCK_DEFAULT);
}

void nn_deleteGuard(nn_Context *context, nn_guard *guard) {
    if(guard == NULL) return;
    context->lockManager.proc(context->lockManager.userdata, guard, NN_LOCK_DEINIT, NN_LOCK_DEFAULT);
    nn_dealloc(&context->allocator, guard, context->lockManager.lockSize);
}

void nn_addRef(nn_refc *refc, size_t count) {
    (*refc) += count;
}

void nn_incRef(nn_refc *refc) {
    nn_addRef(refc, 1);
}

nn_bool_t nn_removeRef(nn_refc *refc, size_t count) {
    return ((*refc) -= count) == 0;
}

nn_bool_t nn_decRef(nn_refc *refc) {
    return nn_removeRef(refc, 1);
}
