// all in 1 C file for convenience of distribution.
// as long as it can include the header, all is fine.
// this should be very easy to include in any modern build system, as it is a single C file.
// When compiled, you can define:
// - NN_BAREMETAL, to remove any runtime dependency on libc and use minimal headers
// - NN_NO_C11_LOCKS, to not use C11 mutexes, instead using pthread mutexes for POSIX systems or Windows locks.
// Most of the time, you only depend on libc.
// However, if pthread locks are used, you will need to link in -lpthread.

// we need the header.
#include "neonucleus.h"

// Based off https://stackoverflow.com/questions/5919996/how-to-detect-reliably-mac-os-x-ios-linux-windows-in-c-preprocessor
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
   //define something for Windows (32-bit and 64-bit, this part is common)
   #ifdef _WIN64
        #define NN_WINDOWS
   #else
        #error "Windows 32-bit is not supported"
   #endif
#elif __APPLE__
    #include <TargetConditionals.h>
    #if TARGET_IPHONE_SIMULATOR
        #error "iPhone Emulators are not supported"
    #elif TARGET_OS_MACCATALYST
        // I guess?
        #define NN_MACOS
    #elif TARGET_OS_IPHONE
        #error "iPhone are not supported"
    #elif TARGET_OS_MAC
        #define NN_MACOS
    #else
        #error "Unknown Apple platform"
    #endif
#elif __ANDROID__
    #error "Android is not supported"
#elif __linux__
    #define NN_LINUX
#endif

#if __unix__ // all unices not caught above
    // Unix
    #define NN_UNIX
    #define NN_POSIX
#elif defined(_POSIX_VERSION)
    // POSIX
    #define NN_POSIX
#endif

typedef struct nn_Lock nn_Lock;

// the special includes
#ifndef NN_BAREMETAL
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#if defined(__STDC_NO_THREADS__) || defined(NN_NO_C11_LOCKS) || defined(NN_WINDOWS) // fuck you Windows
#ifdef NN_POSIX
#define NN_THREAD_PTHREAD
#endif
#ifdef NN_WINDOWS
#define NN_THREAD_WINDOWS
#endif
#else
#include <threads.h>
#define NN_THREAD_C11
#endif

#ifdef NN_POSIX
#include <sys/time.h>
#include <pthread.h>
#endif

#ifdef NN_WINDOWS
#include <Windows.h>
#endif

#endif

void *nn_alloc(nn_Context *ctx, size_t size) {
	if(size == 0) return ctx->alloc;
	return ctx->alloc(ctx->state, NULL, 0, size);
}

void nn_free(nn_Context *ctx, void *memory, size_t size) {
	if(memory == NULL) return;
	if(memory == ctx->alloc) return;
	ctx->alloc(ctx->state, memory, size, 0);
}

void *nn_realloc(nn_Context *ctx, void *memory, size_t oldSize, size_t newSize) {
	if(memory == NULL) return nn_alloc(memory, newSize);
	if(memory == ctx->alloc) return nn_alloc(memory, newSize);
	if(newSize == 0) {
		nn_free(ctx, memory, oldSize);
		return ctx->alloc;
	}
	return ctx->alloc(ctx->state, memory, oldSize, newSize);
}

typedef struct nn_ArenaBlock {
	// we should make each block be 1 allocation instead of 2
	// TODO: make it 1 alloc instead of 2
	void *memory;
	size_t used;
	size_t cap;
	struct nn_ArenaBlock *next;
} nn_ArenaBlock;

typedef struct nn_Arena {
	nn_Context ctx;
	nn_ArenaBlock *block;
	size_t nextCap;
} nn_Arena;

void nn_arinit(nn_Arena *arena, nn_Context *ctx) {
	arena->ctx = *ctx;
	arena->block = NULL;
	arena->nextCap = 1024;
}

void nn_ardestroy(nn_Arena *arena) {
	nn_ArenaBlock *b = arena->block;
	while(b != NULL) {
		nn_ArenaBlock *cur = b;
		b = b->next;

		nn_free(&arena->ctx, cur->memory, cur->cap);
		nn_free(&arena->ctx, cur, sizeof(*cur));
	}
}

nn_ArenaBlock *nn_arallocblock(nn_Context *ctx, size_t cap) {
	void *memory = nn_alloc(ctx, cap);
	if(memory == NULL) return NULL;
	nn_ArenaBlock *block = nn_alloc(ctx, sizeof(*block));
	if(block == NULL) {
		nn_free(ctx, memory, cap);
		return NULL;
	}
	block->memory = memory;
	block->cap = cap;
	block->used = 0;
	block->next = NULL;
	return block;
}

void *nn_aralloc(nn_Arena *arena, size_t size) {
	if((size % NN_ALLOC_ALIGN) != 0) {
		size_t over = size % NN_ALLOC_ALIGN;
		size += NN_ALLOC_ALIGN - over;
	}

	if(arena->block == NULL) {
	}

	nn_ArenaBlock *block = arena->block;
	while(block != NULL) {
		nn_ArenaBlock *cur = block;
		block = block->next;

		size_t free = cur->cap - cur->used;
		if(free >= size) {
			void *mem = (void *)((size_t)cur->memory + cur->used);
			cur->used += size;
			return mem;
		}
	}

	while(arena->nextCap < size) arena->nextCap *= 2;

	nn_ArenaBlock *newBlock = nn_arallocblock(&arena->ctx, arena->nextCap);
	if(newBlock == NULL) {
		return NULL;
	}
	newBlock->next = arena->block;
	newBlock->used = size;
	arena->block = newBlock;
	return newBlock->memory;
}

size_t nn_strlen(const char *s) {
	size_t l = 0;
	while(*(s++) != '\0') l++;
	return l;
}

void nn_memcpy(void *dest, const void *src, size_t len) {
	char *out = (char *)dest;
	const char *in = (const char *)src;
	for(size_t i = 0; i < len; i++) out[i] = in[i];
}

char *nn_strdup(nn_Context *ctx, const char *s) {
	size_t l = nn_strlen(s);
	char *buf = nn_alloc(ctx, sizeof(char) * (l+1));
	if(buf == NULL) return NULL;
	nn_memcpy(buf, s, sizeof(char) * l);
	buf[l] = '\0';
	return buf;
}

const char *nn_arstrdup(nn_Arena *arena, const char *s) {
	size_t len = nn_strlen(s);
	char *buf = nn_aralloc(arena, sizeof(char) * (len+1));
	nn_memcpy(buf, s, sizeof(char) * len);
	buf[len] = '\0';
	return buf;
}

void nn_strfree(nn_Context *ctx, char *s) {
	size_t l = nn_strlen(s);
	nn_free(ctx, s, sizeof(char) * (l+1));
}

void nn_memset(void *dest, int x, size_t len) {
	char *out = (char *)dest;
	for(size_t i = 0; i < len; i++) out[i] = (char)x;
}

int nn_strcmp(const char *a, const char *b) {
	size_t i = 0;
	while(1) {
		char c = a[i];
		char d = b[i];
		if(c == '\0' && d == '\0') return 0;
		if(c != d) return (int)(unsigned char)c - (int)(unsigned char)d;
		i++;
	}
}

nn_Lock *nn_createLock(nn_Context *ctx) {
	nn_LockRequest req;
	req.lock = NULL;
	req.action = NN_LOCK_CREATE;
	ctx->lock(ctx->state, &req);
	return req.lock;
}

void nn_destroyLock(nn_Context *ctx, nn_Lock *lock) {
	if(lock == NULL) return;
	nn_LockRequest req;
	req.lock = lock;
	req.action = NN_LOCK_DESTROY;
	ctx->lock(ctx->state, &req);
}

void nn_lock(nn_Context *ctx, nn_Lock *lock) {
	nn_LockRequest req;
	req.lock = lock;
	req.action = NN_LOCK_LOCK;
	ctx->lock(ctx->state, &req);
}

void nn_unlock(nn_Context *ctx, nn_Lock *lock) {
	nn_LockRequest req;
	req.lock = lock;
	req.action = NN_LOCK_UNLOCK;
	ctx->lock(ctx->state, &req);
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

static void nn_defaultLock(void *state, nn_LockRequest *req) {
	(void)state;
#ifndef NN_BAREMETAL
#if defined(NN_THREAD_C11)
	switch(req->action) {
	case NN_LOCK_CREATE:;
		mtx_t *mem = malloc(sizeof(mtx_t));
		req->lock = mem;
		if(mem == NULL) return;
		if(mtx_init(mem, mtx_plain) != thrd_success) {
			free(mem);
			req->lock = NULL;
			return;
		}
		return;
	case NN_LOCK_DESTROY:;
		mtx_destroy(req->lock);
		free(req->lock);
		return;
	case NN_LOCK_LOCK:;
		mtx_lock(req->lock);
		return;
	case NN_LOCK_UNLOCK:;
		mtx_unlock(req->lock);
		return;
	}
#elif defined(NN_THREAD_PTHREAD)
	switch(req->action) {
	case NN_LOCK_CREATE:;
		pthread_mutex_t *mem = malloc(sizeof(pthread_mutex_t));
		req->lock = mem;
		if(mem == NULL) return;
		if(pthread_mutex_init(mem, NULL) != 0) {
			free(mem);
			req->lock = NULL;
			return;
		}
		return;
	case NN_LOCK_DESTROY:;
		pthread_mutex_destroy(req->lock);
		free(req->lock);
		return;
	case NN_LOCK_LOCK:;
		pthread_mutex_lock(req->lock);
		return;
	case NN_LOCK_UNLOCK:;
		pthread_mutex_unlock(req->lock);
		return;
	}
#elif defined(NN_THREAD_WINDOWS)
#error "Windows locks are not supported yet"
#endif

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
#else
	ctx->rngMaximum = 1;
#endif
	ctx->rng = nn_defaultRng;
	ctx->lock = nn_defaultLock;
}

typedef enum nn_BuiltinComponent {
	NN_BUILTIN_GPU = 0,
	NN_BUILTIN_SCREEN,
	NN_BUILTIN_KEYBOARD,
	NN_BUILTIN_FILESYSTEM,

	// to determine array size
	NN_BUILTIN_COUNT,
} nn_BuiltinComponent;

typedef struct nn_ComponentType {
	nn_Universe *universe;
	nn_Arena arena;
	const char *name;
	// NULL-terminated
	nn_ComponentMethod *methods;
	size_t methodCount;
} nn_ComponentType;

typedef struct nn_Universe {
	nn_Context ctx;
	nn_ComponentType *types[NN_BUILTIN_COUNT];
} nn_Universe;

typedef struct nn_Component {
	char *address;
	nn_ComponentType *ctype;
	size_t slot;
	void *userdata;
} nn_Component;

// the values
typedef enum nn_ValueType {
	NN_VAL_NULL,
	NN_VAL_BOOL,
	NN_VAL_NUM,
	NN_VAL_STR,
	NN_VAL_USERDATA,
	NN_VAL_TABLE,
} nn_ValueType;

typedef struct nn_String {
	nn_Context ctx;
	size_t refc;
	size_t len;
	char data[];
} nn_String;

typedef struct nn_Value {
	nn_ValueType type;
	union {
		bool boolean;
		double number;
		nn_String *string;
		size_t userdataIdx;
		struct nn_Table *table;
	};
} nn_Value;

typedef struct nn_Table {
	nn_Context ctx;
	size_t refc;
	size_t len;
	nn_Value vals[];
} nn_Table;

typedef struct nn_Computer {
	nn_ComputerState state;
	nn_Universe *universe;
	void *userdata;
	char *address;
	void *archState;
	nn_Architecture arch;
	nn_Architecture desiredArch;
	size_t componentCap;
	size_t componentLen;
	nn_Component *components;
	size_t deviceInfoCap;
	size_t deviceInfoLen;
	nn_DeviceInfo *deviceInfo;
	double totalEnergy;
	double energy;
	size_t totalMemory;
	double creationTimestamp;
	size_t stackSize;
	size_t archCount;
	nn_Value callstack[NN_MAX_STACK];
	char errorBuffer[NN_MAX_ERROR_SIZE];
	nn_Architecture archs[NN_MAX_ARCHITECTURES];
} nn_Computer;

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
	
	nn_ComponentType *ctype = nn_alloc(ctx, sizeof(nn_ComponentType));
	if(ctype == NULL) return NULL;
	ctype->universe = universe;

	nn_Arena *arena = &ctype->arena;
	nn_arinit(arena, ctx);

	const char *namecpy = nn_arstrdup(arena, name);
	if(namecpy == NULL) goto fail;

	size_t methodCount = 0;
	while(methods[methodCount].name != NULL) methodCount++;

	nn_ComponentMethod *methodscpy = nn_aralloc(arena, methodCount * sizeof(nn_ComponentMethod));
	if(methodscpy == NULL) goto fail;
	ctype->methods = methodscpy;
	ctype->methodCount = methodCount;

	for(size_t i = 0; i < methodCount; i++) {
		nn_ComponentMethod cpy;
		cpy.direct = methods[i].direct;
		cpy.name = nn_arstrdup(arena, methods[i].name);
		if(cpy.name == NULL) goto fail;
		cpy.docString = nn_arstrdup(arena, methods[i].docString);
		if(cpy.docString == NULL) goto fail;

		ctype->methods[i] = cpy;
	}

	return ctype;
fail:;
	 // yes, because of arenas, we support freeing a "partially initialized state"
	 nn_destroyComponentType(ctype);
	 return NULL;
}

void nn_destroyComponentType(nn_ComponentType *ctype) {
	if(ctype == NULL) return;
	nn_Context *ctx = &ctype->universe->ctx;

	nn_ardestroy(&ctype->arena);
	nn_free(ctx, ctype, sizeof(nn_ComponentType));
}

nn_Computer *nn_createComputer(nn_Universe *universe, void *userdata, const char *address, size_t totalMemory, size_t maxComponents, size_t maxDevices) {
	nn_Context *ctx = &universe->ctx;

	nn_Computer *c = nn_alloc(ctx, sizeof(nn_Computer));
	if(c == NULL) return NULL;

	c->state = NN_BOOTUP;
	c->universe = universe;
	c->userdata = userdata;

	c->address = nn_strdup(ctx, address);
	if(c->address == NULL) {
		nn_free(ctx, c, sizeof(nn_Computer));
		return NULL;
	}

	c->arch.name = NULL;
	c->desiredArch.name = NULL;
	c->archState = NULL;

	c->componentCap = maxComponents;
	c->componentLen = 0;
	c->components = nn_alloc(ctx, sizeof(nn_Component) * maxComponents);
	if(c->components == NULL) {
		nn_strfree(ctx, c->address);
		nn_free(ctx, c, sizeof(nn_Computer));
		return NULL;
	}

	c->deviceInfoCap = maxDevices;
	c->deviceInfoLen = 0;
	c->deviceInfo = nn_alloc(ctx, sizeof(nn_DeviceInfo) * maxDevices);
	if(c->deviceInfo == NULL) {
		nn_free(ctx, c->components, sizeof(nn_Component) * maxComponents);
		nn_strfree(ctx, c->address);
		nn_free(ctx, c, sizeof(nn_Computer));
		return NULL;
	}
	c->totalEnergy = 500;
	c->energy = 500;
	c->totalMemory = totalMemory;
	c->creationTimestamp = nn_currentTime(ctx);
	c->stackSize = 0;
	c->archCount = 0;
	// set to empty string
	c->errorBuffer[0] = '\0';
	return c;
}

void nn_destroyComputer(nn_Computer *computer) {
	nn_Context *ctx = &computer->universe->ctx;

	if(computer->arch.name != NULL) {
		nn_ArchitectureRequest req;
		req.computer = computer;
		req.globalState = computer->arch.state;
		req.localState = computer->archState;
		req.action = NN_ARCH_DEINIT;
		computer->arch.handler(&req);
	}

	nn_free(ctx, computer->components, sizeof(nn_Component) * computer->componentCap);
	nn_free(ctx, computer->deviceInfo, sizeof(nn_DeviceInfo) * computer->deviceInfoCap);
	nn_strfree(ctx, computer->address);
	nn_free(ctx, computer, sizeof(nn_Computer));
}

void *nn_getComputerUserdata(nn_Computer *computer) {
	return computer->userdata;
}

const char *nn_getComputerAddress(nn_Computer *computer) {
	return computer->address;
}

void nn_setArchitecture(nn_Computer *computer, const nn_Architecture *arch) {
	computer->arch = *arch;
}

nn_Architecture nn_getArchitecture(nn_Computer *computer) {
	return computer->arch;
}

void nn_setDesiredArchitecture(nn_Computer *computer, const nn_Architecture *arch) {
	computer->desiredArch = *arch;
}

nn_Architecture nn_getDesiredArchitecture(nn_Computer *computer) {
	return computer->desiredArch;
}

nn_Exit nn_addSupportedArchitecture(nn_Computer *computer, const nn_Architecture *arch) {
	if(computer->archCount == NN_MAX_ARCHITECTURES) return NN_ELIMIT;
	computer->archs[computer->archCount++] = *arch;
	return NN_OK;
}

const nn_Architecture *nn_getSupportedArchitecture(nn_Computer *computer, size_t *len) {
	*len = computer->archCount;
	return computer->archs;
}

nn_Architecture nn_findSupportedArchitecture(nn_Computer *computer, const char *name) {
	for(size_t i = 0; i < computer->archCount; i++) {
		if(nn_strcmp(computer->archs[i].name, name) == 0) return computer->archs[i];
	}

	return (nn_Architecture) {
		.name = NULL,
		.state = NULL,
		.handler = NULL,
	};	
}

void nn_setTotalEnergy(nn_Computer *computer, double maxEnergy) {
	computer->totalEnergy = maxEnergy;
}

double nn_getTotalEnergy(nn_Computer *computer) {
	return computer->totalEnergy;
}

void nn_setEnergy(nn_Computer *computer, double energy) {
	computer->energy = energy;
}

double nn_getEnergy(nn_Computer *computer) {
	return computer->energy;
}

bool nn_removeEnergy(nn_Computer *computer, double energy) {
	computer->energy -= energy;
	if(computer->energy < 0) computer->energy = 0;
	return computer->energy <= 0;
}

size_t nn_getTotalMemory(nn_Computer *computer) {
	return computer->totalMemory;
}

size_t nn_getFreeMemory(nn_Computer *computer) {
	if(computer->state == NN_BOOTUP) return 0;
	nn_ArchitectureRequest req;
	req.computer = computer;
	req.action = NN_ARCH_FREEMEM;
	req.globalState = computer->arch.state;
	req.localState = computer->archState;
	
	computer->arch.handler(&req);
	return req.freeMemory;
}

double nn_getUptime(nn_Computer *computer) {
	return nn_currentTime(&computer->universe->ctx) - computer->creationTimestamp;
}


void nn_setError(nn_Computer *computer, const char *s) {
	nn_setLError(computer, s, nn_strlen(s));
}

void nn_setLError(nn_Computer *computer, const char *s, size_t len) {
	if(len >= NN_MAX_ERROR_SIZE) len = NN_MAX_ERROR_SIZE - 1;
	nn_memcpy(computer->errorBuffer, s, sizeof(char) * len);
	computer->errorBuffer[len] = '\0';
}

const char *nn_getError(nn_Computer *computer) {
	return computer->errorBuffer;
}

void nn_clearError(nn_Computer *computer) {
	// make it empty
	computer->errorBuffer[0] = '\0';
}

void nn_setComputerState(nn_Computer *computer, nn_ComputerState state) {
	computer->state = state;
}

nn_ComputerState nn_getComputerState(nn_Computer *computer) {
	return computer->state;
}

static void nn_setErrorFromExit(nn_Computer *computer, nn_Exit exit) {
	switch(exit) {
	case NN_OK:
		return; // no error
	case NN_EBADCALL:
		return; // stored by component
	case NN_ENOMEM:
		nn_setError(computer, "out of memory");
		return;
	case NN_ELIMIT:
		nn_setError(computer, "buffer overflow");
		return;
	case NN_ENOSTACK:
		nn_setError(computer, "stack overflow");
		return;
	case NN_EBELOWSTACK:
		nn_setError(computer, "stack underflow");
		return;
	case NN_EBADSTATE:
		nn_setError(computer, "bad state");
		return;
	}
}

nn_Exit nn_tick(nn_Computer *computer) {
	nn_Exit err;
	if(computer->state == NN_BOOTUP) {
		// init state
		nn_ArchitectureRequest req;
		req.computer = computer;
		req.globalState = computer->arch.state;
		req.localState = NULL;
		req.action = NN_ARCH_INIT;
		err = computer->arch.handler(&req);
		if(err) {
			computer->state = NN_CRASHED;
			nn_setErrorFromExit(computer, err);
			return err;
		}
		computer->archState = req.localState;
	} else if(computer->state != NN_RUNNING) {
		nn_setErrorFromExit(computer, NN_EBADSTATE);
		return NN_EBADSTATE;
	}
	computer->state = NN_RUNNING;
	nn_ArchitectureRequest req;
	req.computer = computer;
	req.globalState = computer->arch.state;
	req.localState = computer->archState;
	req.action = NN_ARCH_TICK;
	err = computer->arch.handler(&req);
	if(err) {
		computer->state = NN_CRASHED;
		nn_setErrorFromExit(computer, err);
		return err;
	}
	return NN_OK;
}
