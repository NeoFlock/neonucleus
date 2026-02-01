#include "neonucleus.h"
#include "nn_utils.h"

#ifndef NN_BAREMETAL
#include <stdlib.h>
#include <time.h>

#if defined(__STDC_NO_THREADS__) || defined(NN_WINDOWS) // fuck you Windows
#include "tinycthread.h"
#else
#include <threads.h>
#endif
#endif

void *nn_alloc(nn_Context *ctx, size_t size) {
	if(size == 0) return ctx->alloc;
	return ctx->alloc(ctx->state, NULL, 0, size);
}

void *nn_realloc(nn_Context *ctx, void *memory, size_t oldSize, size_t newSize) {
	if(memory == ctx->alloc) return nn_alloc(memory, newSize);
	if(newSize == 0) {
		nn_free(ctx, memory, oldSize);
		return ctx->alloc;
	}
	return ctx->alloc(ctx->state, memory, oldSize, newSize);
}

void nn_free(nn_Context *ctx, void *memory, size_t size) {
	if(memory == ctx->alloc) return;
	ctx->alloc(ctx->state, memory, size, 0);
}

size_t nn_strlen(const char *s) {
	size_t l = 0;
	while(*(s++) != '\0') l++;
	return l;
}

char *nn_strdup(nn_Context *ctx, const char *s) {
	size_t l = nn_strlen(s);
	char *buf = nn_alloc(ctx, sizeof(char) * (l+1));
	if(buf == NULL) return NULL;
	nn_memcpy(buf, s, sizeof(char) * l);
	buf[l] = '\0';
	return buf;
}

void nn_strfree(nn_Context *ctx, char *s) {
	size_t l = nn_strlen(s);
	nn_free(ctx, s, sizeof(char) * (l+1));
}

void nn_memcpy(void *dest, const void *src, size_t len) {
	char *out = (char *)dest;
	const char *in = (const char *)src;
	for(size_t i = 0; i < len; i++) out[i] = in[i];
}

void nn_memset(void *dest, int x, size_t len) {
	char *out = (char *)dest;
	for(size_t i = 0; i < len; i++) out[i] = (char)x;
}

nn_Lock *nn_createLock(nn_Context *ctx) {
	void *mem = nn_alloc(ctx, ctx->lockSize);
	if(mem == NULL) return NULL;
	ctx->lock(ctx->state, mem, NN_LOCK_INIT);
	return mem;
}

void nn_destroyLock(nn_Context *ctx, nn_Lock *lock) {
	if(lock == NULL) return;
	ctx->lock(ctx->state, lock, NN_LOCK_DEINIT);
	nn_free(ctx, lock, ctx->lockSize);
}

void nn_lock(nn_Context *ctx, nn_Lock *lock) {
	ctx->lock(ctx->state, lock, NN_LOCK_LOCK);
}

void nn_unlock(nn_Context *ctx, nn_Lock *lock) {
	ctx->lock(ctx->state, lock, NN_LOCK_UNLOCK);
}

double nn_currentTime(nn_Context *ctx) {
	return ctx->time(ctx->state);
}

size_t nn_rand(nn_Context *ctx) {
	return ctx->rng(ctx->state);
}

double nn_randf(nn_Context *ctx) {
	double n = (double)nn_rand(ctx);
	return n / (double)(ctx->rngMaximum + 1);
}

double nn_randfi(nn_Context *ctx) {
	double n = (double)nn_rand(ctx);
	return n / (double)ctx->rngMaximum;
}

static void *nn_defaultAlloc(void *_, void *memory, size_t oldSize, size_t newSize) {
#ifndef NN_BAREMETAL
	if(newSize == 0) {
		free(memory);
		return NULL;
	}

	return realloc(memory, newSize);
#else
	// 0 memory available
	return NULL;
#endif
}

static double nn_defaultTime(void *_) {
#ifndef NN_BAREMETAL
	// time does not exist... yet!
	return 0;
#else
	// time does not exist
	return 0;
#endif
}

static size_t nn_defaultRng(void *_) {
#ifndef NN_BAREMETAL
	return rand();
#else
	// insane levels of RNG
	return 1;
#endif
}

static void nn_defaultLock(void *state, void *lock, nn_LockAction action) {
#ifndef NN_BAREMETAL
	switch(action) {
	case NN_LOCK_INIT:
		mtx_init(lock, mtx_plain);
		break;
	case NN_LOCK_DEINIT:
		mtx_destroy(lock);
		break;
	case NN_LOCK_LOCK:
		mtx_lock(lock);
		break;
	case NN_LOCK_UNLOCK:
		mtx_unlock(lock);
		break;
	}
#endif
}

void nn_initContext(nn_Context *ctx) {
	ctx->state = NULL;
	ctx->alloc = nn_defaultAlloc;
	ctx->time = nn_defaultTime;
#ifndef NN_BAREMETAL
	// someone pointed out that running this multiple times
	// in 1 second can cause the RNG to loop.
	// However, if you call this function multiple times at all,
	// that's on you.
	srand(time(NULL));
	ctx->rngMaximum = RAND_MAX;
	ctx->lockSize = sizeof(mtx_t);
#else
	ctx->rngMaximum = 1;
	ctx->lockSize = 0;
#endif
	ctx->rng = nn_defaultRng;
	ctx->lock = nn_defaultLock;
}
