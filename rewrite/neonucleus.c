// all in 1 C file for convenience of distribution.
// as long as it can include the header, all is fine.
// this should be very easy to include in any modern build system, as it is a single C file.
// When compiled, you can define:
// - NN_BAREMETAL, to remove any runtime dependency on libc and use minimal headers
// - NN_NO_LOCKS, to not use mutexes AT ALL.
// - NN_NO_C11_LOCKS, to not use C11 mutexes, instead using pthread mutexes for POSIX systems or Windows locks.
// - NN_ATOMIC_NONE, to not use atomics.
// Most of the time, you only depend on libc.
// However, if pthread locks are used, you will need to link in -lpthread.

// we need the header.
#include "neonucleus.h"

// to use the numerical accuracy better
#define NN_COMPONENT_CALLBUDGET 10000

#ifdef NN_ATOMIC_NONE
typedef size_t nn_refc_t;

void nn_incRef(nn_refc_t *refc) {
	(*refc)++;
}

bool nn_decRef(nn_refc_t *refc) {
	(*refc)--;
	return (*refc) == 0;
}
#else
// we need atomics for thread-safe reference counting that will be used
// for managing the lifetimes of various resources
// TODO: evaluate if the context should contain a method for atomics.
#include <stdatomic.h>

typedef atomic_size_t nn_refc_t;

void nn_incRef(nn_refc_t *refc) {
	atomic_fetch_add(refc, 1);
}

bool nn_decRef(nn_refc_t *refc) {
	nn_refc_t old = atomic_fetch_sub(refc, 1);
	return old == 1;
}
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
#include <windows.h>
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

	while(arena->nextCap <= size) arena->nextCap *= 2;

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

// taken from https://wiki.osdev.org/CRC32
// OSDev wiki is really useful sometimes
// TODO: maybe allow one that uses compiler intrinsics
// because CPUs are really good at CRC32 nowadays

unsigned int nn_crc32_poly8_lookup[256] =
{
 0, 0x77073096, 0xEE0E612C, 0x990951BA,
 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
 0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
 0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
 0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
 0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
 0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
 0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
 0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
 0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
 0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
 0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
 0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
 0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
 0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
 0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
 0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
 0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
 0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
 0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
 0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
 0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

unsigned int nn_computeCRC32(const char *data, size_t datalen) {
	unsigned int crc = 0xFFFFFFFF;
	while(datalen-- > 0) {
		crc = nn_crc32_poly8_lookup[(crc ^ *(data++)) & 0xFF] ^ (crc >> 8);
	}
	return (crc ^ 0xFFFFFFFF);
}

void nn_crc32ChecksumBytes(unsigned int checksum, char out[8]) {
	char bytes[4];
	bytes[0] = (checksum >>  0) & 0xFF;
	bytes[1] = (checksum >>  8) & 0xFF;
	bytes[2] = (checksum >> 16) & 0xFF;
	bytes[3] = (checksum >> 24) & 0xFF;
	char alpha[16] = "0123456789abcdef";

	for(size_t i = 0; i < 4; i++) {
		char byte = bytes[i];
		out[i*2] = alpha[(byte>>4) & 0xF];
		out[i*2+1] = alpha[(byte>>0) & 0xF];
	}
}

bool nn_simplifyPath(const char original[NN_MAX_PATH], char simplified[NN_MAX_PATH]) {
	// pass 1: check for valid characters, and \ becomes /
	for(size_t i = 0; true; i++) {
		if(original[i] == '\\') simplified[i] = '/';
		else simplified[i] = original[i];
		if(original[i] == '\0') break;
	}
	// get rid of //, starting / and ending /
	{
		while(simplified[0] == '/') {
			for(size_t i = 1; true; i++) {
				simplified[i-1] = simplified[i];
				if(simplified[i] == '\0') break;
			}
		}

		size_t j = 0;
		for(size_t i = 0; simplified[i] != '\0'; i++) {
			if(simplified[i] == '/' && simplified[i+1] == '/') {
				// simply discard it
				continue;
			} else {
				simplified[j] = simplified[i];
				j++;
			}
		}
		simplified[j] = '\0';
		while(simplified[j-1] == '/') {
			j--;
			simplified[j] = '\0';
		}
	}
	// TODO: handle ..
	// valid
	return true;
}

int nn_memcmp(const char *a, const char *b, size_t len) {
	for(size_t i = 0; i < len; i++) {
		char c = a[i];
		char d = b[i];
		if(c != d) return (int)(unsigned char)c - (int)(unsigned char)d;
	}
	return 0;
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
#if defined(NN_NO_LOCKS)
	switch(req->action) {
	case NN_LOCK_CREATE:;
		req->lock = nn_defaultLock;
		return;
	case NN_LOCK_DESTROY:;
		return;
	case NN_LOCK_LOCK:;
		return;
	case NN_LOCK_UNLOCK:;
		return;
	}
	return;
#elif defined(NN_THREAD_C11)
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
	switch(req->action) {
	case NN_LOCK_CREATE:;
		req->lock = CreateMutex(NULL, false, NULL);
	case NN_LOCK_DESTROY:;
		CloseHandle(req->lock);
		return;
	case NN_LOCK_LOCK:;
		WaitForSingleObject(req->lock, INFINITE);
		return;
	case NN_LOCK_UNLOCK:;
		ReleaseMutex(req->lock);
		return;
	}
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

typedef struct nn_ComponentType {
	nn_Universe *universe;
	void *userdata;
	nn_Arena arena;
	const char *name;
	nn_ComponentHandler *handler;
	// NULL-terminated
	nn_Method *methods;
	size_t methodCount;
} nn_ComponentType;

// currently just a wrapper around a context
// but will be way more in the future
typedef struct nn_Universe {
	nn_Context ctx;
} nn_Universe;

typedef struct nn_Component {
	char *address;
	nn_ComponentType *ctype;
	int slot;
	float budgetUsed;
	void *userdata;
	void *state;
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

// we don't use the enum member as a name because then all the
// switch cases would have a useless branch for it.
const char *nn_typenames[6] = {
	[NN_VAL_NULL] = "null",
	[NN_VAL_BOOL] = "bool",
	[NN_VAL_NUM] = "number",
	[NN_VAL_STR] = "string",
	[NN_VAL_USERDATA] = "userdata",
	[NN_VAL_TABLE] = "table",
};

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

typedef struct nn_Signal {
	size_t len;
	nn_Value *values;
} nn_Signal;

typedef struct nn_Computer {
	nn_ComputerState state;
	nn_Universe *universe;
	void *userdata;
	char *address;
	char *tmpaddress;
	void *archState;
	nn_Architecture arch;
	nn_Architecture desiredArch;
	size_t callBudget;
	size_t totalCallBudget;
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
	size_t signalCount;
	size_t userCount;
	nn_Value callstack[NN_MAX_STACK];
	char errorBuffer[NN_MAX_ERROR_SIZE];
	nn_Architecture archs[NN_MAX_ARCHITECTURES];
	nn_Signal signals[NN_MAX_SIGNALS];
	char *users[NN_MAX_USERS];
} nn_Computer;

nn_Universe *nn_createUniverse(nn_Context *ctx) {
	nn_Universe *u = nn_alloc(ctx, sizeof(nn_Universe));
	if(u == NULL) return NULL;
	u->ctx = *ctx;
	return u;
}

void nn_destroyUniverse(nn_Universe *universe) {
	nn_Context ctx = universe->ctx;
	nn_free(&ctx, universe, sizeof(nn_Universe));
}

nn_ComponentType *nn_createComponentType(nn_Universe *universe, const char *name, void *userdata, const nn_Method methods[], nn_ComponentHandler *handler) {
	nn_Context *ctx = &universe->ctx;
	
	nn_ComponentType *ctype = nn_alloc(ctx, sizeof(nn_ComponentType));
	if(ctype == NULL) return NULL;
	ctype->universe = universe;
	ctype->userdata = userdata;
	ctype->handler = handler;

	nn_Arena *arena = &ctype->arena;
	nn_arinit(arena, ctx);

	const char *namecpy = nn_arstrdup(arena, name);
	if(namecpy == NULL) goto fail;
	ctype->name = namecpy;

	size_t methodCount = 0;
	while(methods[methodCount].name != NULL) methodCount++;

	nn_Method *methodscpy = nn_aralloc(arena, methodCount * sizeof(nn_Method));
	if(methodscpy == NULL) goto fail;
	ctype->methods = methodscpy;
	ctype->methodCount = methodCount;

	for(size_t i = 0; i < methodCount; i++) {
		nn_Method cpy;
		cpy.flags = methods[i].flags;
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

	nn_ComponentRequest req;
	req.typeUserdata = ctype->userdata;
	req.compUserdata = NULL;
	req.state = NULL;
	req.computer = NULL;
	req.compAddress = NULL;
	req.action = NN_COMP_FREETYPE;
	req.methodCalled = NULL;
	ctype->handler(&req);

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

	c->tmpaddress = NULL;

	c->arch.name = NULL;
	c->desiredArch.name = NULL;
	c->archState = NULL;

	c->totalCallBudget = 1000;
	c->callBudget = c->totalCallBudget;

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
	c->signalCount = 0;
	c->userCount = 0;
	// set to empty string
	c->errorBuffer[0] = '\0';
	return c;
}

static void nn_dropValue(nn_Value val);
static void nn_dropComponent(nn_Computer *computer, nn_Component c);

void nn_destroyComputer(nn_Computer *computer) {
	nn_Context *ctx = &computer->universe->ctx;

	if(computer->arch.name != NULL && computer->archState != NULL) {
		nn_ArchitectureRequest req;
		req.computer = computer;
		req.globalState = computer->arch.state;
		req.localState = computer->archState;
		req.action = NN_ARCH_DEINIT;
		computer->arch.handler(&req);
	}

	for(size_t i = 0; i < computer->stackSize; i++) {
		nn_dropValue(computer->callstack[i]);
	}
	for(size_t i = 0; i < computer->componentLen; i++) {
		nn_dropComponent(computer, computer->components[i]);
	}
	for(size_t i = 0; i < computer->signalCount; i++) {
		nn_Signal s = computer->signals[i];
		for(size_t j = 0; j < s.len; j++) nn_dropValue(s.values[j]);
		nn_free(ctx, s.values, sizeof(nn_Value) * s.len);
	}
	for(size_t i = 0; i < computer->userCount; i++) {
		nn_strfree(ctx, computer->users[i]);
	}

	nn_free(ctx, computer->components, sizeof(nn_Component) * computer->componentCap);
	nn_free(ctx, computer->deviceInfo, sizeof(nn_DeviceInfo) * computer->deviceInfoCap);
	if(computer->tmpaddress != NULL) nn_strfree(ctx, computer->tmpaddress);
	nn_strfree(ctx, computer->address);
	nn_free(ctx, computer, sizeof(nn_Computer));
}

void *nn_getComputerUserdata(nn_Computer *computer) {
	return computer->userdata;
}

const char *nn_getComputerAddress(nn_Computer *computer) {
	return computer->address;
}

nn_Universe *nn_getComputerUniverse(nn_Computer *computer) {
	return computer->universe;
}

nn_Context *nn_getUniverseContext(nn_Universe *universe) {
	return &universe->ctx;
}

nn_Context *nn_getComputerContext(nn_Computer *computer) {
	return &computer->universe->ctx;
}

nn_Exit nn_setTmpAddress(nn_Computer *computer, const char *address) {
	nn_Context ctx = computer->universe->ctx;
	if(address == NULL) {
		if(computer->tmpaddress != NULL) {
			nn_strfree(&ctx, computer->tmpaddress);
		}
		computer->tmpaddress = NULL;
		return NN_OK;
	}
	char *newTmp = nn_strdup(&ctx, address);
	if(newTmp == NULL) return NN_ENOMEM;
	if(computer->tmpaddress != NULL) {
		nn_strfree(&ctx, computer->tmpaddress);
	}
	computer->tmpaddress = newTmp;
	return NN_OK;
}

const char *nn_getTmpAddress(nn_Computer *computer) {
	return computer->tmpaddress;
}

nn_Exit nn_addUser(nn_Computer *computer, const char *user) {
	if(computer->userCount == NN_MAX_USERS) return NN_ELIMIT;
	size_t len = nn_strlen(user);
	if(len >= NN_MAX_USERNAME) return NN_ELIMIT;

	char *usercpy = nn_strdup(&computer->universe->ctx, user);
	if(usercpy == NULL) return NN_ENOMEM;
	computer->users[computer->userCount++] = usercpy;
	return NN_OK;
}

bool nn_removeUser(nn_Computer *computer, const char *user) {
	bool removed = false;
	size_t j = 0;
	nn_Context ctx = computer->universe->ctx;

	for(size_t i = 0; i < computer->userCount; i++) {
		char *u = computer->users[i];
		if(nn_strcmp(u, user) == 0) {
			nn_strfree(&ctx, u);
			removed = true;
		} else {
			computer->users[j] = computer->users[i];
			j++;
		}
	}
	computer->userCount = j;
	return removed;
}

const char *nn_getUser(nn_Computer *computer, size_t idx) {
	if(idx >= computer->userCount) return NULL;
	return computer->users[idx];
}

bool nn_hasUser(nn_Computer *computer, const char *user) {
	if(computer->userCount == 0) return true;
	for(size_t i = 0; i < computer->userCount; i++) {
		if(nn_strcmp(computer->users[i], user) == 0) return true;
	}
	return false;
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

const nn_Architecture *nn_getSupportedArchitectures(nn_Computer *computer, size_t *len) {
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
	if(computer->energy <= 0) {
		computer->state = NN_BLACKOUT;
		return true;
	}
	return false;
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

void nn_setErrorFromExit(nn_Computer *computer, nn_Exit exit) {
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
	nn_resetCallBudget(computer);
	nn_resetComponentBudgets(computer);
	nn_clearstack(computer);
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

nn_Exit nn_addComponent(nn_Computer *computer, nn_ComponentType *ctype, const char *address, int slot, void *userdata) {
	if(computer->componentLen == computer->componentCap) return NN_ELIMIT;

	nn_Component c;
	c.address = nn_strdup(&computer->universe->ctx, address);
	if(c.address == NULL) return NN_ENOMEM;
	c.ctype = ctype;
	c.slot = slot;
	c.userdata = userdata;
	c.state = NULL;
	c.budgetUsed = 0;

	nn_ComponentRequest req;
	req.typeUserdata = ctype->userdata;
	req.compUserdata = userdata;
	req.state = NULL;
	req.computer = computer;
	req.compAddress = address;
	req.action = NN_COMP_INIT;
	req.methodCalled = NULL;

	nn_Exit err = ctype->handler(&req);
	if(err != NN_OK) {
		nn_strfree(&computer->universe->ctx, c.address);
		return err;
	}
	// get the state back!
	c.state = req.state;

	computer->components[computer->componentLen++] = c;

	if(computer->state == NN_RUNNING) {
		err = nn_pushstring(computer, "component_added");
		if(err) return err;
		err = nn_pushstring(computer, address);
		if(err) return err;
		err = nn_pushstring(computer, ctype->name);
		if(err) return err;
		err = nn_pushSignal(computer, 3);
		if(err) return err;
	}
	return NN_OK;
}

bool nn_hasComponent(nn_Computer *computer, const char *address) {
	for(size_t i = 0; i < computer->componentLen; i++) {
		nn_Component *c = &computer->components[i];
		if(nn_strcmp(c->address, address) == 0) {
			return true;
		}
	}
	return false;
}

bool nn_hasMethod(nn_Computer *computer, const char *address, const char *method) {
	for(size_t i = 0; i < computer->componentLen; i++) {
		nn_Component *c = &computer->components[i];
		if(nn_strcmp(c->address, address) != 0) continue;

		bool found = false;
		for(size_t j = 0; j < c->ctype->methodCount; j++) {
			if(nn_strcmp(c->ctype->methods[j].name, method) != 0) continue;
			found = true;
			break;
		}
		if(!found) return false;
		
		nn_ComponentRequest req;
		req.typeUserdata = c->ctype->userdata;
		req.compUserdata = c->userdata;
		req.state = c->state;
		req.computer = computer;
		req.compAddress = address;
		req.action = NN_COMP_ENABLED;
		req.methodCalled = method;
		// default response in case it is not implemented
		req.methodEnabled = true;
		// should never error
		c->ctype->handler(&req);

		return req.methodEnabled;
	}
	return false;
}

static void nn_dropComponent(nn_Computer *computer, nn_Component c) {
	nn_ComponentRequest req;
	req.typeUserdata = c.ctype->userdata;
	req.compUserdata = c.userdata;
	req.state = c.state;
	req.computer = computer;
	req.compAddress = c.address;
	req.action = NN_COMP_DEINIT;
	req.methodCalled = NULL;

	c.ctype->handler(&req);
	
	nn_strfree(&computer->universe->ctx, c.address);
}

nn_Exit nn_removeComponent(nn_Computer *computer, const char *address) {
	size_t j = 0;
	nn_Component c;
	c.address = NULL;

	for(size_t i = 0; i < computer->componentLen; i++) {
		if(nn_strcmp(computer->components[i].address, address) == 0) {
			c = computer->components[i];
		} else {
			computer->components[j++] = computer->components[i];
		}
	}
	computer->componentLen = j;

	// already removed!
	if(c.address == NULL) return NN_EBADSTATE;
	nn_dropComponent(computer, c);

	if(computer->state == NN_RUNNING) {
		nn_Exit err = nn_pushstring(computer, "component_removed");
		if(err) return err;
		err = nn_pushstring(computer, address);
		if(err) return err;
		// not a UAF because c is on-stack
		err = nn_pushstring(computer, c.ctype->name);
		if(err) return err;
		err = nn_pushSignal(computer, 3);
		if(err) return err;
	}
	return NN_OK;
}

const char *nn_getComponentType(nn_Computer *computer, const char *address) {
	for(size_t i = 0; i < computer->componentLen; i++) {
		nn_Component *c = &computer->components[i];
		if(nn_strcmp(c->address, address) == 0) {
			return c->ctype->name;
		}
	}
	return NULL;
}

int nn_getComponentSlot(nn_Computer *computer, const char *address) {
	for(size_t i = 0; i < computer->componentLen; i++) {
		nn_Component *c = &computer->components[i];
		if(nn_strcmp(c->address, address) == 0) {
			return c->slot;
		}
	}
	return 0;
}

const nn_Method *nn_getComponentMethods(nn_Computer *computer, const char *address, size_t *len) {
	for(size_t i = 0; i < computer->componentLen; i++) {
		nn_Component *c = &computer->components[i];
		if(nn_strcmp(c->address, address) == 0) {
			if(len != NULL) *len = c->ctype->methodCount;
			return c->ctype->methods;
		}
	}
	if(len != NULL) *len = 0;
	return NULL;
}

const char *nn_getComponentAddress(nn_Computer *computer, size_t idx) {
	if(idx >= computer->componentLen) return NULL;
	return computer->components[idx].address;
}

const char *nn_getComponentDoc(nn_Computer *computer, const char *address, const char *method) {
	if(!nn_hasComponent(computer, address)) {
		return NULL;
	}
	if(!nn_hasMethod(computer, address, method)) {
		return NULL;
	}
	for(size_t i = 0; i < computer->componentLen; i++) {
		nn_Component c = computer->components[i];
		if(nn_strcmp(c.address, address) != 0) continue;

		for(size_t j = 0; j < c.ctype->methodCount; j++) {
			if(nn_strcmp(c.ctype->methods[j].name, method) == 0) return c.ctype->methods[j].docString;
		}
		return NULL;
	}
	return NULL;
}

void *nn_getComponentUserdata(nn_Computer *computer, const char *address) {
	for(size_t i = 0; i < computer->componentLen; i++) {
		nn_Component *c = &computer->components[i];
		if(nn_strcmp(c->address, address) == 0) {
			return c->userdata;
		}
	}
	return 0;
}

static void nn_retainValue(nn_Value val) {
	switch(val.type) {
	case NN_VAL_NULL:
	case NN_VAL_BOOL:
	case NN_VAL_NUM:
	case NN_VAL_USERDATA:
		return;
	case NN_VAL_STR:
		val.string->refc++;
		return;
	case NN_VAL_TABLE:
		val.table->refc++;
		return;
	}
}

static void nn_dropValue(nn_Value val) {
	nn_Context ctx;
	size_t size;
	switch(val.type) {
	case NN_VAL_NULL:
	case NN_VAL_BOOL:
	case NN_VAL_NUM:
	case NN_VAL_USERDATA:
		return;
	case NN_VAL_STR:
		val.string->refc--;
		if(val.string->refc != 0) return;
		ctx = val.string->ctx;
		size = val.string->len + 1;
		nn_free(&ctx, val.string, sizeof(nn_String) + sizeof(char) * size);
		return;
	case NN_VAL_TABLE:
		val.table->refc--;
		if(val.table->refc != 0) return;
		ctx = val.table->ctx;
		size = val.table->len;
		nn_free(&ctx, val.table, sizeof(nn_Table) + sizeof(nn_Value) * size * 2);
		return;
	}
}

nn_Exit nn_call(nn_Computer *computer, const char *address, const char *method) {
	if(!nn_hasComponent(computer, address)) {
		nn_setError(computer, "no such component");
		return NN_EBADCALL;
	}
	if(!nn_hasMethod(computer, address, method)) {
		nn_setError(computer, "no such method");
		return NN_EBADCALL;
	}
	for(size_t i = 0; i < computer->componentLen; i++) {
		nn_Component c = computer->components[i];
		if(nn_strcmp(c.address, address) != 0) continue;

		// minimum cost of a component call
		if(computer->callBudget > 0) computer->callBudget--;
		for(size_t j = 0; j < c.ctype->methodCount; j++) {
			nn_Method m = c.ctype->methods[j];
			if(nn_strcmp(m.name, method) != 0) continue;
			// indirect calls consume the entire call budget
			if((m.flags & NN_DIRECT) == NN_INDIRECT) computer->callBudget = 0;
		}
		
		nn_ComponentRequest req;
		req.typeUserdata = c.ctype->userdata;
		req.compUserdata = c.userdata;
		req.state = c.state;
		req.computer = computer;
		req.compAddress = address;
		req.action = NN_COMP_CALL;
		req.methodCalled = method;
		// default is to return nothing
		req.returnCount = 0;

		nn_Exit err = c.ctype->handler(&req);
		if(err) {
			if(err != NN_EBADCALL) nn_setErrorFromExit(computer, err);
			// clear junk
			nn_clearstack(computer);
			return err;
		}

		if(computer->stackSize < req.returnCount) {
			err = NN_EBELOWSTACK;
			nn_setErrorFromExit(computer, err);
			nn_clearstack(computer);
			return err;
		}

		size_t endOfTrim = computer->stackSize - req.returnCount;
		for(size_t i = 0; i < endOfTrim; i++) {
			nn_dropValue(computer->callstack[i]);
		}
		for(size_t i = 0; i < req.returnCount; i++) {
			computer->callstack[i] = computer->callstack[endOfTrim + i];
		}
		computer->stackSize = req.returnCount;
		return NN_OK;
	}
	return NN_EBADSTATE;
}

void nn_setCallBudget(nn_Computer *computer, size_t budget) {
	computer->totalCallBudget = budget;
}

size_t nn_getCallBudget(nn_Computer *computer) {
	return computer->totalCallBudget;
}

size_t nn_callBudgetRemaining(nn_Computer *computer) {
	return computer->callBudget;
}

void nn_resetCallBudget(nn_Computer *computer) {
	computer->callBudget = computer->totalCallBudget;
}

bool nn_componentsOverused(nn_Computer *computer) {
	for(size_t i = 0; i < computer->componentLen; i++) {
		if(computer->components[i].budgetUsed >= NN_COMPONENT_CALLBUDGET) return true;
	}
	if(computer->totalCallBudget == 0) return false;
	return computer->callBudget == 0;
}

void nn_resetComponentBudgets(nn_Computer *computer) {
	for(size_t i = 0; i < computer->componentLen; i++) {
		computer->components[i].budgetUsed = 0;
	}
}
bool nn_costComponent(nn_Computer *computer, const char *address, double perTick) {
	return nn_costComponentN(computer, address, 1, perTick);
}

bool nn_costComponentN(nn_Computer *computer, const char *address, double amount, double perTick) {
	for(size_t i = 0; i < computer->componentLen; i++) {
		nn_Component *c = &computer->components[i];
		if(nn_strcmp(c->address, address) != 0) continue;
		c->budgetUsed += (NN_COMPONENT_CALLBUDGET * amount) / perTick;
		return c->budgetUsed >= NN_COMPONENT_CALLBUDGET;
	}
	return false;
}

bool nn_checkstack(nn_Computer *computer, size_t amount) {
	return computer->stackSize + amount <= NN_MAX_STACK;
}

static nn_Exit nn_pushvalue(nn_Computer *computer, nn_Value val) {
	if(!nn_checkstack(computer, 1)) {
		nn_dropValue(val);
		return NN_ENOSTACK;
	}
	computer->callstack[computer->stackSize++] = val;
	return NN_OK;
}

nn_Exit nn_pushnull(nn_Computer *computer) {
	return nn_pushvalue(computer, (nn_Value) {.type = NN_VAL_NULL});
}

nn_Exit nn_pushbool(nn_Computer *computer, bool truthy) {
	return nn_pushvalue(computer, (nn_Value) {.type = NN_VAL_BOOL, .boolean = truthy});
}

nn_Exit nn_pushnumber(nn_Computer *computer, double num) {
	return nn_pushvalue(computer, (nn_Value) {.type = NN_VAL_NUM, .number = num});
}

nn_Exit nn_pushinteger(nn_Computer *computer, intptr_t num) {
	return nn_pushnumber(computer, num);
}

nn_Exit nn_pushstring(nn_Computer *computer, const char *str) {
	return nn_pushlstring(computer, str, nn_strlen(str));
}

nn_Exit nn_pushlstring(nn_Computer *computer, const char *str, size_t len) {
	nn_Context ctx = computer->universe->ctx;
	nn_String *s = nn_alloc(&ctx, sizeof(nn_String) + sizeof(char) * (len + 1));
	if(s == NULL) return NN_ENOMEM;
	s->ctx = ctx;
	s->refc = 1;
	s->len = len;
	nn_memcpy(s->data, str, sizeof(char) * len);
	s->data[len] = '\0';
	return nn_pushvalue(computer, (nn_Value) {.type = NN_VAL_STR, .string = s});
}

nn_Exit nn_pushuserdata(nn_Computer *computer, size_t userdataIdx) {
	return nn_pushvalue(computer, (nn_Value) {.type = NN_VAL_USERDATA, .userdataIdx = userdataIdx});
}

nn_Exit nn_pusharraytable(nn_Computer *computer, size_t len) {
	if(computer->stackSize < len) return NN_EBELOWSTACK;
	nn_Context ctx = computer->universe->ctx;
	nn_Table *t = nn_alloc(&ctx, sizeof(nn_Table) + sizeof(nn_Value) * len * 2);
	if(t == NULL) return NN_ENOMEM;
	t->ctx = ctx;
	t->refc = 1;
	t->len = len;
	for(size_t i = 0; i < len; i++) {
		t->vals[i*2].type = NN_VAL_NUM;
		t->vals[i*2].number = (double)i+1;
		t->vals[i*2+1] = computer->callstack[computer->stackSize - len + i];
	}
	computer->stackSize -= len;
	return nn_pushvalue(computer, (nn_Value) {.type = NN_VAL_TABLE, .table = t});
}

nn_Exit nn_pushtable(nn_Computer *computer, size_t len) {
	size_t size = len * 2;
	if(computer->stackSize < size) return NN_EBELOWSTACK;
	nn_Context ctx = computer->universe->ctx;
	nn_Table *t = nn_alloc(&ctx, sizeof(nn_Table) + sizeof(nn_Value) * size);
	if(t == NULL) return NN_ENOMEM;
	t->ctx = ctx;
	t->refc = 1;
	t->len = len;
	for(size_t i = 0; i < len*2; i++) {
		t->vals[i] = computer->callstack[computer->stackSize - size + i];
	}
	computer->stackSize -= size;
	return nn_pushvalue(computer, (nn_Value) {.type = NN_VAL_TABLE, .table = t});
}

nn_Exit nn_pop(nn_Computer *computer) {
	return nn_popn(computer, 1);
}

nn_Exit nn_popn(nn_Computer *computer, size_t n) {
	if(computer->stackSize < n) return NN_EBELOWSTACK;
	for(size_t i = computer->stackSize - n; i < computer->stackSize; i++) {
		nn_dropValue(computer->callstack[i]);
	}
	computer->stackSize -= n;
	return NN_OK;
}

nn_Exit nn_dupe(nn_Computer *computer) {
	return nn_dupen(computer, 1);
}
nn_Exit nn_dupen(nn_Computer *computer, size_t n) {
	if(computer->stackSize < n) return NN_EBELOWSTACK;
	if(!nn_checkstack(computer, n)) return NN_ENOSTACK;
	
	for(size_t i = 0; i < n; i++) {
		nn_Value v = computer->callstack[computer->stackSize - n + i];
		nn_retainValue(v);
		computer->callstack[computer->stackSize + i] = v;
	}
	computer->stackSize += n;
	return NN_OK;
}

nn_Exit nn_dupeat(nn_Computer *computer, size_t idx) {
	if(computer->stackSize <= idx) return NN_EBELOWSTACK;
	nn_Value v = computer->callstack[idx];
	nn_retainValue(v);
	return nn_pushvalue(computer, v);
}

size_t nn_getstacksize(nn_Computer *computer) {
	return computer->stackSize;
}

void nn_clearstack(nn_Computer *computer) {
	nn_popn(computer, nn_getstacksize(computer));
}

bool nn_isnull(nn_Computer *computer, size_t idx) {
	if(idx >= computer->stackSize) return false;
	return computer->callstack[idx].type == NN_VAL_NULL;
}

bool nn_isboolean(nn_Computer *computer, size_t idx) {
	if(idx >= computer->stackSize) return false;
	return computer->callstack[idx].type == NN_VAL_BOOL;
}

bool nn_isnumber(nn_Computer *computer, size_t idx) {
	if(idx >= computer->stackSize) return false;
	return computer->callstack[idx].type == NN_VAL_NUM;
}

bool nn_isinteger(nn_Computer *computer, size_t idx) {
	if(idx >= computer->stackSize) return false;
	if(computer->callstack[idx].type != NN_VAL_NUM) return false;
	double num = computer->callstack[idx].number;
	intptr_t castNum = (intptr_t)num;
	return (double)castNum == num;
}

bool nn_isstring(nn_Computer *computer, size_t idx) {
	if(idx >= computer->stackSize) return false;
	return computer->callstack[idx].type == NN_VAL_STR;
}

bool nn_isuserdata(nn_Computer *computer, size_t idx) {
	if(idx >= computer->stackSize) return false;
	return computer->callstack[idx].type == NN_VAL_USERDATA;
}

bool nn_istable(nn_Computer *computer, size_t idx) {
	if(idx >= computer->stackSize) return false;
	return computer->callstack[idx].type == NN_VAL_TABLE;
}

const char *nn_typenameof(nn_Computer *computer, size_t idx) {
	if(idx >= computer->stackSize) return "none";
	return nn_typenames[computer->callstack[idx].type];
}

bool nn_checknull(nn_Computer *computer, size_t idx, const char *errMsg) {
	if(nn_isnull(computer, idx)) return false;
	nn_setError(computer, errMsg);
	return true;
}

bool nn_checkboolean(nn_Computer *computer, size_t idx, const char *errMsg) {
	if(nn_isboolean(computer, idx)) return false;
	nn_setError(computer, errMsg);
	return true;
}

bool nn_checknumber(nn_Computer *computer, size_t idx, const char *errMsg) {
	if(nn_isnumber(computer, idx)) return false;
	nn_setError(computer, errMsg);
	return true;
}

bool nn_checkinteger(nn_Computer *computer, size_t idx, const char *errMsg) {
	if(nn_isinteger(computer, idx)) return false;
	nn_setError(computer, errMsg);
	return true;
}

bool nn_checkstring(nn_Computer *computer, size_t idx, const char *errMsg) {
	if(nn_isstring(computer, idx)) return false;
	nn_setError(computer, errMsg);
	return true;
}

bool nn_checkuserdata(nn_Computer *computer, size_t idx, const char *errMsg) {
	if(nn_isuserdata(computer, idx)) return false;
	nn_setError(computer, errMsg);
	return true;
}

bool nn_checktable(nn_Computer *computer, size_t idx, const char *errMsg) {
	if(nn_istable(computer, idx)) return false;
	nn_setError(computer, errMsg);
	return true;
}

nn_Exit nn_defaultnull(nn_Computer *computer, size_t idx) {
	if(computer->stackSize != idx) return NN_OK;
	return nn_pushnull(computer);
}

nn_Exit nn_defaultboolean(nn_Computer *computer, size_t idx, bool value) {
	if(computer->stackSize != idx) return NN_OK;
	return nn_pushbool(computer, value);
}

nn_Exit nn_defaultnumber(nn_Computer *computer, size_t idx, double num) {
	if(computer->stackSize != idx) return NN_OK;
	return nn_pushnumber(computer, num);
}

nn_Exit nn_defaultinteger(nn_Computer *computer, size_t idx, intptr_t num) {
	if(computer->stackSize != idx) return NN_OK;
	return nn_pushnumber(computer, num);
}

nn_Exit nn_defaultstring(nn_Computer *computer, size_t idx, const char *str) {
	return nn_defaultlstring(computer, idx, str, nn_strlen(str));
}

nn_Exit nn_defaultlstring(nn_Computer *computer, size_t idx, const char *str, size_t len) {
	if(computer->stackSize != idx) return NN_OK;
	return nn_pushlstring(computer, str, len);
}

nn_Exit nn_defaultuserdata(nn_Computer *computer, size_t idx, size_t userdataIdx) {
	if(computer->stackSize != idx) return NN_OK;
	return nn_pushuserdata(computer, userdataIdx);
}

nn_Exit nn_defaulttable(nn_Computer *computer, size_t idx) {
	if(computer->stackSize != idx) return NN_OK;
	return nn_pushtable(computer, 0);
}

bool nn_toboolean(nn_Computer *computer, size_t idx) {
	return computer->callstack[idx].boolean;
}

double nn_tonumber(nn_Computer *computer, size_t idx) {
	return computer->callstack[idx].number;
}

intptr_t nn_tointeger(nn_Computer *computer, size_t idx) {
	return computer->callstack[idx].number;
}

const char *nn_tostring(nn_Computer *computer, size_t idx) {
	return nn_tolstring(computer, idx, NULL);
}

const char *nn_tolstring(nn_Computer *computer, size_t idx, size_t *len) {
	nn_String *s = computer->callstack[idx].string;
	if(len != NULL) *len = s->len;
	return s->data;
}

size_t nn_touserdata(nn_Computer *computer, size_t idx) {
	return computer->callstack[idx].userdataIdx;
}

nn_Exit nn_dumptable(nn_Computer *computer, size_t idx, size_t *len) {
	nn_Table *t = computer->callstack[idx].table;
	if(!nn_checkstack(computer, t->len * 2)) return NN_ENOSTACK;

	if(len != NULL) *len = t->len;
	for(size_t i = 0; i < t->len * 2; i++) {
		computer->callstack[computer->stackSize + i] = t->vals[i];
	}
	computer->stackSize += t->len * 2;
	
	return NN_OK;
}

int nn_countValueCost(nn_Computer *computer, size_t values) {
	int total = 0;
	for(size_t i = 0; i < values; i++) {
		nn_Value val = computer->callstack[computer->stackSize - values + i];
		total += 2;
		switch(val.type) {
		case NN_VAL_NULL:
		case NN_VAL_BOOL:
			total += 4;
			continue;
		case NN_VAL_NUM:
			total += 8;
			continue;
		case NN_VAL_STR:
			total += val.string->len;
			if(val.string->len == 0) total++;
			continue;
		case NN_VAL_TABLE:
		case NN_VAL_USERDATA:
			return -1;
		}
	}
	return total;
}

size_t nn_countSignalCostValue(nn_Value value) {
	size_t total = 2;
	switch(value.type) {
	case NN_VAL_NULL:
	case NN_VAL_BOOL:
		total += 4;
		break;
	case NN_VAL_NUM:
	case NN_VAL_USERDATA:
		total += 8;
		break;
	case NN_VAL_STR:
		// 2+1
		if(value.string->len == 0) total++;
		else total += value.string->len;
		break;
	case NN_VAL_TABLE:
		total += 2;
		for(size_t i = 0; i < value.table->len * 2; i++) {
			total += nn_countSignalCostValue(value.table->vals[i]);
		}
		break;
	}
	return total;
}

size_t nn_countSignalCost(nn_Computer *computer, size_t values) {
	size_t total = 0;
	for(size_t i = 0; i < values; i++) {
		nn_Value val = computer->callstack[computer->stackSize - values + i];
		total += nn_countSignalCostValue(val);
	}
	return total;
}

size_t nn_countSignals(nn_Computer *computer) {
	return computer->signalCount;
}

nn_Exit nn_pushSignal(nn_Computer *computer, size_t valueCount) {
	if(computer->state != NN_RUNNING) return nn_popn(computer, valueCount);
	if(computer->signalCount == NN_MAX_SIGNALS) return NN_ELIMIT;
	if(computer->stackSize < valueCount) return NN_EBELOWSTACK;
	
	size_t cost = nn_countSignalCost(computer, valueCount);
	if(cost > NN_MAX_SIGNALSIZE) return NN_ELIMIT;

	nn_Context ctx = computer->universe->ctx;
	nn_Signal s;
	s.len = valueCount;
	s.values = nn_alloc(&ctx, sizeof(nn_Value) * valueCount);
	if(s.values == NULL) return NN_ENOMEM;
	for(size_t i = 0; i < valueCount; i++) {
		s.values[i] = computer->callstack[computer->stackSize - valueCount + i];
	}
	computer->stackSize -= valueCount;
	computer->signals[computer->signalCount++] = s;
	return NN_OK;
}

nn_Exit nn_popSignal(nn_Computer *computer, size_t *valueCount) {
	if(computer->signalCount == 0) return NN_EBADSTATE;

	nn_Context ctx = computer->universe->ctx;
	nn_Signal s = computer->signals[0];
	if(!nn_checkstack(computer, s.len)) return NN_ENOSTACK;

	if(valueCount != NULL) *valueCount = s.len;
	for(size_t i = 0; i < s.len; i++) {
		computer->callstack[computer->stackSize + i] = s.values[i];
	}
	computer->stackSize += s.len;
	for(size_t i = 1; i < computer->signalCount; i++) {
		computer->signals[i-1] = computer->signals[i];
	}
	computer->signalCount--;
	nn_free(&ctx, s.values, sizeof(nn_Value) * s.len);
	return NN_OK;
}

// todo: everything

typedef struct nn_EEPROM_state {
	nn_Universe *universe;
	nn_EEPROM eeprom;
	void *userdata;
	nn_EEPROMHandler *handler;
} nn_EEPROM_state;

typedef struct nn_VEEPROM_state {
	nn_Universe *universe;
	char *code;
	size_t codelen;
	char *data;
	size_t datalen;
	size_t archlen;
	size_t labellen;
	bool isReadonly;
	char arch[NN_MAX_ARCHNAME];
	char label[NN_MAX_LABEL];
} nn_VEEPROM_state;

nn_Exit nn_eeprom_handler(nn_ComponentRequest *req) {
	nn_EEPROM_state *state = req->typeUserdata;
	void *instance = req->compUserdata;
	// NULL for FREETYPE
	nn_Computer *computer = req->computer;
	nn_Context ctx = state->universe->ctx;

	nn_EEPROMRequest ereq;
	ereq.userdata = state->userdata;
	ereq.instance = instance;
	ereq.computer = computer;
	ereq.eepromConf = &state->eeprom;

	const char *method = req->methodCalled;

	switch(req->action) {
	case NN_COMP_FREETYPE:
		nn_free(&ctx, state, sizeof(*state));
		break;
	case NN_COMP_INIT:
		return NN_OK;
	case NN_COMP_DEINIT:
		ereq.action = NN_EEPROM_DROP;
		return state->handler(&ereq);
	case NN_COMP_ENABLED:
		req->methodEnabled = true;
		return NN_OK;
	case NN_COMP_CALL:
		if(nn_strcmp(method, "getSize") == 0) {
			return nn_pushnumber(computer, state->eeprom.size);
		}
		if(nn_strcmp(method, "getDataSize") == 0) {
			return nn_pushnumber(computer, state->eeprom.dataSize);
		}
		if(nn_strcmp(method, "isReadOnly") == 0) {
			ereq.action = NN_EEPROM_ISREADONLY;
			nn_Exit e = state->handler(&ereq);
			if(e) return e;
			req->returnCount = 1;
			return nn_pushbool(computer, ereq.buflen != 0);
		}
		if(nn_strcmp(method, "getChecksum") == 0) {
			nn_costComponent(computer, req->compAddress, 1);
			// yup, on-stack.
			// Perhaps in the future we'll make it heap-allocated.
			char buf[state->eeprom.size];
			ereq.action = NN_EEPROM_GET;
			ereq.buf = buf;
			ereq.buflen = state->eeprom.size;
			nn_Exit e = state->handler(&ereq);
			if(e) return e;
			unsigned int chksum = nn_computeCRC32(buf, ereq.buflen);
			char encoded[8];
			nn_crc32ChecksumBytes(chksum, encoded);
			req->returnCount = 1;
			return nn_pushlstring(computer, encoded, 8);
		}
		if(nn_strcmp(method, "makeReadonly") == 0) {
			nn_costComponent(computer, req->compAddress, 1);
			// 1st argument is a string, which is the checksum we're meant to have
			if(nn_checkstring(computer, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			size_t len;
			const char *desired = nn_tolstring(computer, 0, &len);
			if(len != 8) {
				nn_setError(computer, "invalid checksum");
				return NN_EBADCALL;
			}
			// yup, on-stack.
			// Perhaps in the future we'll make it heap-allocated.
			char buf[state->eeprom.size];
			ereq.action = NN_EEPROM_GET;
			ereq.buf = buf;
			ereq.buflen = state->eeprom.size;
			nn_Exit e = state->handler(&ereq);
			if(e) return e;
			unsigned int chksum = nn_computeCRC32(buf, ereq.buflen);
			char encoded[8];
			nn_crc32ChecksumBytes(chksum, encoded);
			if(nn_memcmp(encoded, desired, sizeof(char) * 8)) {
				nn_setError(computer, "incorrect checksum");
				return NN_EBADCALL;
			}
		
			ereq.action = NN_EEPROM_MAKEREADONLY;
			e = state->handler(&ereq);
			if(e) return e;

			req->returnCount = 1;
			return nn_pushbool(computer, true);
		}
		if(nn_strcmp(method, "getLabel") == 0) {
			nn_costComponent(computer, req->compAddress, 1);
			char buf[NN_MAX_LABEL];
			ereq.action = NN_EEPROM_GETLABEL;
			ereq.buf = buf;
			ereq.buflen = NN_MAX_LABEL;
			nn_Exit e = state->handler(&ereq);
			if(e) return e;
			req->returnCount = 1;
			if(ereq.buf == NULL) return nn_pushnull(computer);
			return nn_pushlstring(computer, buf, ereq.buflen);
		}
		if(nn_strcmp(method, "setLabel") == 0) {
			nn_costComponent(computer, req->compAddress, 1);
			if(nn_checkstring(computer, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			size_t len;
			const char *s = nn_tolstring(computer, 0, &len);
			if(len > NN_MAX_LABEL) len = NN_MAX_LABEL;
			char buf[NN_MAX_LABEL];
			nn_memcpy(buf, s, sizeof(char) * len);
			ereq.action = NN_EEPROM_SETLABEL;
			ereq.buf = buf;
			ereq.buflen = len;
			nn_Exit e = state->handler(&ereq);
			if(e) return e;
			req->returnCount = 1;
			return nn_pushlstring(computer, buf, ereq.buflen);
		}
		if(nn_strcmp(method, "get") == 0) {
			nn_costComponent(computer, req->compAddress, 1);
			// yup, on-stack.
			// Perhaps in the future we'll make it heap-allocated.
			char buf[state->eeprom.size];
			ereq.action = NN_EEPROM_GET;
			ereq.buf = buf;
			ereq.buflen = state->eeprom.size;
			nn_Exit e = state->handler(&ereq);
			if(e) return e;
			req->returnCount = 1;
			return nn_pushlstring(computer, buf, ereq.buflen);
		}
		if(nn_strcmp(method, "getData") == 0) {
			nn_costComponent(computer, req->compAddress, 1);
			// yup, on-stack.
			// Perhaps in the future we'll make it heap-allocated.
			char buf[state->eeprom.dataSize];
			ereq.action = NN_EEPROM_GETDATA;
			ereq.buf = buf;
			ereq.buflen = state->eeprom.dataSize;
			nn_Exit e = state->handler(&ereq);
			if(e) return e;
			req->returnCount = 1;
			return nn_pushlstring(computer, buf, ereq.buflen);
		}
		if(nn_strcmp(method, "getArchitecture") == 0) {
			nn_costComponent(computer, req->compAddress, 1);
			char buf[NN_MAX_ARCHNAME];
			ereq.action = NN_EEPROM_GETARCH;
			ereq.buf = buf;
			ereq.buflen = NN_MAX_ARCHNAME;
			nn_Exit e = state->handler(&ereq);
			if(e) return e;
			req->returnCount = 1;
			return nn_pushlstring(computer, buf, ereq.buflen);
		}
		if(nn_strcmp(method, "set") == 0) {
			nn_costComponent(computer, req->compAddress, 1);
			if(nn_getstacksize(computer) < 1) {
				nn_setError(computer, "bad argument #1 (string expected)");
				return NN_EBADCALL;
			}
			if(!nn_isstring(computer, 0)) {
				nn_setError(computer, "bad argument #1 (string expected)");
				return NN_EBADCALL;
			}
			size_t len;
			const char *s = nn_tolstring(computer, 0, &len);
			if(len > state->eeprom.size) {
				nn_setError(computer, "not enough space");
				return NN_EBADCALL;
			}
			ereq.action = NN_EEPROM_SET;
			// DO NOT MODIFY IT!!!!
			ereq.buf = (char*)s;
			ereq.buflen = len;
			return state->handler(&ereq);
		}
		if(nn_strcmp(method, "setData") == 0) {
			nn_costComponent(computer, req->compAddress, 1);
			if(nn_checkstring(computer, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			size_t len;
			const char *s = nn_tolstring(computer, 0, &len);
			if(len > state->eeprom.dataSize) {
				nn_setError(computer, "not enough space");
				return NN_EBADCALL;
			}
			ereq.action = NN_EEPROM_SETDATA;
			// DO NOT MODIFY IT!!!!
			ereq.buf = (char*)s;
			ereq.buflen = len;
			return state->handler(&ereq);
		}
		if(nn_strcmp(method, "setArchitecture") == 0) {
			nn_costComponent(computer, req->compAddress, 1);
			if(nn_checkstring(computer, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			size_t len;
			const char *s = nn_tolstring(computer, 0, &len);
			if(len > NN_MAX_ARCHNAME) {
				nn_setError(computer, "not enough space");
				return NN_EBADCALL;
			}
			ereq.action = NN_EEPROM_SETARCH;
			// DO NOT MODIFY IT!!!!
			ereq.buf = (char*)s;
			ereq.buflen = len;
			return state->handler(&ereq);
		}
		return NN_OK;
	}
	return NN_OK;
}

nn_EEPROM nn_defaultEEPROM = (nn_EEPROM) {
	.size = 4 * NN_KiB,
	.dataSize = 256,
	.readEnergyCost = 10,
	.writeEnergyCost = 100,
	.readDataEnergyCost = 10,
	.writeDataEnergyCost = 50,
};

nn_ComponentType *nn_createEEPROM(nn_Universe *universe, const nn_EEPROM *eeprom, nn_EEPROMHandler *handler, void *userdata) {
	nn_Context ctx = universe->ctx;
	nn_EEPROM_state *state = nn_alloc(&ctx, sizeof(*state));
	if(state == NULL) return NULL;
	state->universe = universe;
	state->eeprom = *eeprom;
	state->userdata = userdata;
	state->handler = handler;
	const nn_Method methods[] = {
		{"getSize", "function(): number - Get the storage capacity of the EEPROM.", NN_DIRECT},
		{"getDataSize", "function(): number - Get the storage capacity of the EEPROM data.", NN_DIRECT},
		{"getLabel", "function(): string - Get the EEPROM label", NN_INDIRECT},
		{"setLabel", "function(label: string): string - Set the EEPROM label and return what was actually set, which may be truncated.", NN_INDIRECT},
		{"get", "function(): string - Get the current EEPROM contents.", NN_INDIRECT},
		{"getData", "function(): string - Get the current EEPROM data contents.", NN_INDIRECT},
		{"set", "function(data: string) - Set the current EEPROM contents.", NN_INDIRECT},
		{"setData", "function(data: string) - Set the current EEPROM data contents.", NN_INDIRECT},
		{"getArchitecture", "function(): string - Get the current EEPROM architecture intended.", NN_INDIRECT},
		{"setArchitecture", "function(data: string) - Set the current EEPROM architecture intended.", NN_INDIRECT},
		{"isReadOnly", "function(): boolean - Returns whether the EEPROM is read-only.", NN_INDIRECT},
		{"makeReadonly", "function(checksum: string) - Makes the EEPROM read-only, this cannot be undone.", NN_INDIRECT},
		{"getChecksum", "function(): string - Returns a simple checksum of the EEPROM's contents and data.", NN_INDIRECT},
		{NULL, NULL, NN_INDIRECT},
	};
	nn_ComponentType *t = nn_createComponentType(universe, "eeprom", state, methods, nn_eeprom_handler);
	if(t == NULL) {
		nn_free(&ctx, state, sizeof(*state));
		return NULL;
	}
	return t;
}

static nn_Exit nn_veeprom_handler(nn_EEPROMRequest *req) {
	const nn_EEPROM *conf = req->eepromConf;
	nn_VEEPROM_state *state = req->userdata;
	nn_Context ctx = state->universe->ctx;
	switch(req->action) {
	case NN_EEPROM_DROP:
		nn_free(&ctx, state->code, sizeof(char) * conf->size);
		nn_free(&ctx, state->data, sizeof(char) * conf->dataSize);
		nn_free(&ctx, state, sizeof(*state));
		return NN_OK;
	case NN_EEPROM_ISREADONLY:
		req->buflen = state->isReadonly ? 1 : 0;
		return NN_OK;
	case NN_EEPROM_MAKEREADONLY:
		state->isReadonly = true;
		return NN_OK;
	case NN_EEPROM_GET:
		req->buflen = state->codelen;
		nn_memcpy(req->buf, state->code, sizeof(char) * state->codelen);
		return NN_OK;
	case NN_EEPROM_GETDATA:
		req->buflen = state->datalen;
		nn_memcpy(req->buf, state->data, sizeof(char) * state->datalen);
		return NN_OK;
	case NN_EEPROM_GETARCH:
		req->buflen = state->archlen;
		nn_memcpy(req->buf, state->arch, sizeof(char) * state->archlen);
		return NN_OK;
	case NN_EEPROM_GETLABEL:
		req->buflen = state->labellen;
		nn_memcpy(req->buf, state->label, sizeof(char) * state->labellen);
		return NN_OK;
	case NN_EEPROM_SET:
		state->codelen = req->buflen;
		nn_memcpy(state->code, req->buf, sizeof(char) * state->codelen);
		return NN_OK;
	case NN_EEPROM_SETDATA:
		state->datalen = req->buflen;
		nn_memcpy(state->data, req->buf, sizeof(char) * state->datalen);
		return NN_OK;
	case NN_EEPROM_SETARCH:
		state->archlen = req->buflen;
		nn_memcpy(state->arch, req->buf, sizeof(char) * state->archlen);
		return NN_OK;
	case NN_EEPROM_SETLABEL:
		state->labellen = req->buflen;
		nn_memcpy(state->label, req->buf, sizeof(char) * state->labellen);
		return NN_OK;
	}
	return NN_OK;
}

nn_ComponentType *nn_createVEEPROM(nn_Universe *universe, const nn_EEPROM *eeprom, const nn_VEEPROM *vmem) {
	nn_Context ctx = universe->ctx;
	char *code = NULL;
	char *data = NULL;
	size_t archlen = 0;
	nn_VEEPROM_state *state = nn_alloc(&ctx, sizeof(*state));
	if(state == NULL) goto fail;
	state->universe = universe;
	state->isReadonly = vmem->isReadonly;
	
	code = nn_alloc(&ctx, sizeof(char) * eeprom->size);
	if(code == NULL) goto fail;

	data = nn_alloc(&ctx, sizeof(char) * eeprom->dataSize);
	if(data == NULL) goto fail;

	state->code = code;
	state->data = data;

	state->codelen = vmem->codelen;
	state->datalen = vmem->datalen;
	state->labellen = vmem->labellen;
	nn_memcpy(state->code, vmem->code, sizeof(char) * state->codelen);
	nn_memcpy(state->data, vmem->data, sizeof(char) * state->datalen);
	nn_memcpy(state->label, vmem->label, sizeof(char) * state->labellen);

	if(vmem->arch != NULL) {
		archlen = nn_strlen(vmem->arch);
	}
	state->archlen = archlen;
	nn_memcpy(state->arch, vmem->arch, sizeof(char) * archlen);

	nn_ComponentType *ty = nn_createEEPROM(universe, eeprom, nn_veeprom_handler, state);
	if(ty == NULL) goto fail;
	return ty;
fail:;
	 // remember, freeing NULL is fine!
	 nn_free(&ctx, code, sizeof(char) * eeprom->size);
	 nn_free(&ctx, data, sizeof(char) * eeprom->dataSize);
	 nn_free(&ctx, state, sizeof(*state));
	 return NULL;
}

typedef struct nn_Filesystem_state {
	nn_Universe *universe;
	void *userdata;
	nn_FilesystemHandler *handler;
	nn_Filesystem fs;
} nn_Filesystem_state;

nn_Filesystem nn_defaultFilesystems[4] = {
	(nn_Filesystem) {
		.spaceTotal = 1 * NN_MiB,
		.readsPerTick = 4,
		.writesPerTick = 2,
		.dataEnergyCost = 256.0 / NN_MiB,
	},
	(nn_Filesystem) {
		.spaceTotal = 2 * NN_MiB,
		.readsPerTick = 4,
		.writesPerTick = 2,
		.dataEnergyCost = 512.0 / NN_MiB,
	},
	(nn_Filesystem) {
		.spaceTotal = 4 * NN_MiB,
		.readsPerTick = 7,
		.writesPerTick = 3,
		.dataEnergyCost = 1024.0 / NN_MiB,
	},
	(nn_Filesystem) {
		.spaceTotal = 8 * NN_MiB,
		.readsPerTick = 13,
		.writesPerTick = 5,
		.dataEnergyCost = 2048.0 / NN_MiB,
	},
};


nn_Filesystem nn_defaultFloppy = (nn_Filesystem) {
	.spaceTotal = 512 * NN_KiB,
	.readsPerTick = 1,
	.writesPerTick = 1,
	.dataEnergyCost = 8.0 / NN_MiB,
};

nn_Exit nn_filesystem_handler(nn_ComponentRequest *req) {
	nn_Filesystem_state *state = req->typeUserdata;
	void *instance = req->compUserdata;
	// NULL for FREETYPE
	nn_Computer *computer = req->computer;
	nn_Context ctx = state->universe->ctx;

	nn_FilesystemRequest fsreq;
	fsreq.userdata = state->userdata;
	fsreq.instance = instance;
	fsreq.computer = computer;
	fsreq.fsConf = &state->fs;

	const char *method = req->methodCalled;
	nn_Exit err;

	switch(req->action) {
	case NN_COMP_FREETYPE:
		nn_free(&ctx, state, sizeof(*state));
		break;
	case NN_COMP_INIT:
		return NN_OK;
	case NN_COMP_DEINIT:
		fsreq.action = NN_FS_DROP;
		return state->handler(&fsreq);
	case NN_COMP_ENABLED:
		req->methodEnabled = true;
		return NN_OK;
	case NN_COMP_CALL:
		if(nn_strcmp(method, "spaceTotal") == 0) {
			req->returnCount = 1;
			return nn_pushnumber(computer, state->fs.spaceTotal);
		}
		if(nn_strcmp(method, "spaceUsed") == 0) {
			fsreq.action = NN_FS_SPACEUSED;
			err = state->handler(&fsreq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushnumber(computer, fsreq.size);
		}
		if(nn_strcmp(method, "isReadOnly") == 0) {
			fsreq.action = NN_FS_ISREADONLY;
			err = state->handler(&fsreq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushbool(computer, fsreq.size != 0);
		}
		if(nn_strcmp(method, "getLabel") == 0) {
			char buf[NN_MAX_LABEL];
			fsreq.action = NN_FS_GETLABEL;
			fsreq.strarg1 = buf;
			fsreq.strarg1len = NN_MAX_LABEL;
			err = state->handler(&fsreq);
			if(err) return err;
			req->returnCount = 1;
			if(fsreq.strarg1 == NULL) return nn_pushnull(computer);
			return nn_pushlstring(computer, buf, fsreq.strarg1len);
		}
		if(nn_strcmp(method, "setLabel") == 0) {
			if(nn_checkstring(computer, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			fsreq.action = NN_FS_SETLABEL;
			// DO NOT MODIFY THE BUFFER!!!
			fsreq.strarg1 = (char *)nn_tolstring(computer, 0, &fsreq.strarg1len);
			err = state->handler(&fsreq);
			if(err) return err;
			req->returnCount = 1;
			if(fsreq.strarg1 == NULL) return nn_pushnull(computer);
			return nn_pushlstring(computer, fsreq.strarg1, fsreq.strarg1len);
		}
		if(nn_strcmp(method, "open") == 0) {
			if(nn_checkstring(computer, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			err = nn_defaultstring(computer, 1, "r");
			if(err) return err;
			if(nn_checkstring(computer, 1, "bad argument #2 (string expected)")) return NN_EBADCALL;
			size_t pathlen;
			const char *path = nn_tolstring(computer, 0, &pathlen);
			if(pathlen >= NN_MAX_PATH) {
				nn_setError(computer, "path too long");
				return NN_EBADCALL;
			}
			char truepath[NN_MAX_PATH];
			nn_simplifyPath(path, truepath);
			size_t modelen;
			const char *mode = nn_tolstring(computer, 1, &modelen);
			fsreq.action = NN_FS_OPEN;
			fsreq.strarg1 = truepath;
			fsreq.strarg1len = nn_strlen(truepath);
			fsreq.strarg2 = (char *)mode;
			fsreq.strarg2len = modelen;

			err = state->handler(&fsreq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushnumber(computer, fsreq.fd);
		}
		if(nn_strcmp(method, "close") == 0) {
			if(nn_checkinteger(computer, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
			fsreq.fd = nn_tointeger(computer, 0);
			fsreq.action = NN_FS_CLOSE;
			return state->handler(&fsreq);
		}
		if(nn_strcmp(method, "read") == 0) {
			if(nn_checkinteger(computer, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
			err = nn_defaultinteger(computer, 1, NN_MAX_READ);
			if(err) return err;
			if(nn_checknumber(computer, 1, "bad argument #2 (number expected)")) return NN_EBADCALL;
			fsreq.action = NN_FS_READ;
			fsreq.fd = nn_tointeger(computer, 0);
			double requested = nn_tonumber(computer, 1);
			// handles infinity consistently
			if(requested > NN_MAX_READ) requested = NN_MAX_READ;
			if(requested < 0) requested = 0;
			char buf[(size_t)requested];
			fsreq.strarg1 = buf;
			fsreq.strarg1len = requested;
			err = state->handler(&fsreq);
			if(err) return err;
			req->returnCount = 1;
			if(fsreq.strarg1 == NULL) return nn_pushnull(computer);
			return nn_pushlstring(computer, fsreq.strarg1, fsreq.strarg1len);
		}
		if(nn_strcmp(method, "write") == 0) {
			if(nn_checkinteger(computer, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
			if(nn_checkstring(computer, 1, "bad argument #2 (string expected)")) return NN_EBADCALL;
			fsreq.action = NN_FS_WRITE;
			fsreq.fd = nn_tointeger(computer, 0);
			fsreq.strarg1 = (char *)nn_tolstring(computer, 1, &fsreq.strarg1len);
			err = state->handler(&fsreq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushbool(computer, true);
		}
		if(nn_strcmp(method, "seek") == 0) {
			if(nn_checkinteger(computer, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
			err = nn_defaultstring(computer, 1, "cur");
			if(err) return err;
			if(nn_checkstring(computer, 1, "bad argument #2 (string expected)")) return NN_EBADCALL;
			err = nn_defaultinteger(computer, 2, 0);
			if(err) return err;
			if(nn_checkinteger(computer, 2, "bad argument #3 (integer expected)")) return NN_EBADCALL;
			fsreq.action = NN_FS_SEEK;
			fsreq.fd = nn_tointeger(computer, 0);
			const char *whence = nn_tostring(computer, 1);
			fsreq.off = nn_tointeger(computer, 2);

			if(nn_strcmp(whence, "set") == 0) {
				fsreq.whence = NN_SEEK_SET;
			} else if(nn_strcmp(whence, "cur") == 0) {
				fsreq.whence = NN_SEEK_CUR;
			} else if(nn_strcmp(whence, "end") == 0) {
				fsreq.whence = NN_SEEK_END;
			} else {
				nn_setError(computer, "bad seek whence");
				return NN_EBADCALL;
			}
			err = state->handler(&fsreq);
			return nn_pushnumber(computer, fsreq.off);
		}
		if(nn_strcmp(method, "list") == 0) {
			if(nn_checkstring(computer, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			char truepath[NN_MAX_PATH];
			size_t pathlen;
			const char *path = nn_tolstring(computer, 0, &pathlen);
			if(pathlen >= NN_MAX_PATH) {
				nn_setError(computer, "path too long");
				return NN_EBADCALL;
			}
			nn_simplifyPath(path, truepath);
			int dirfd;
			fsreq.action = NN_FS_OPENDIR;
			fsreq.strarg1 = truepath;
			fsreq.strarg1len = nn_strlen(truepath);
			err = state->handler(&fsreq);
			if(err) return err;
			dirfd = fsreq.fd;

			// this sucks hard
			size_t entryCount = 0;
			while(1) {
				char entry[NN_MAX_PATH];
				fsreq.action = NN_FS_READDIR;
				fsreq.fd = dirfd;
				fsreq.strarg1 = entry;
				fsreq.strarg1len = NN_MAX_PATH;

				err = state->handler(&fsreq);
				if(err) goto list_fail;
				if(fsreq.strarg1 == NULL) break;

				if(fsreq.strarg1len == 1 && entry[0] == '.') continue;
				if(fsreq.strarg1len == 2 && entry[0] == '.' && entry[1] == '.') continue;

				err = nn_pushlstring(computer, entry, fsreq.strarg1len);
				if(err) goto list_fail;
				entryCount++;
			}
			err = nn_pusharraytable(computer, entryCount);
			if(err) goto list_fail;
			req->returnCount = 1;
			fsreq.action = NN_FS_CLOSEDIR;
			fsreq.fd = dirfd;
			state->handler(&fsreq);
			return NN_OK;
		list_fail:
			fsreq.action = NN_FS_CLOSEDIR;
			fsreq.fd = dirfd;
			state->handler(&fsreq);
			return err;
		}
		if(nn_strcmp(method, "exists") == 0) {
			if(nn_checkstring(computer, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			char truepath[NN_MAX_PATH];
			size_t pathlen;
			const char *path = nn_tolstring(computer, 0, &pathlen);
			if(pathlen >= NN_MAX_PATH) {
				nn_setError(computer, "path too long");
				return NN_EBADCALL;
			}
			nn_simplifyPath(path, truepath);

			fsreq.action = NN_FS_EXISTS;
			fsreq.strarg1 = truepath;
			fsreq.strarg1len = nn_strlen(truepath);
			err = state->handler(&fsreq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushbool(computer, fsreq.size != 0);
		}
		if(nn_strcmp(method, "size") == 0) {
			if(nn_checkstring(computer, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			char truepath[NN_MAX_PATH];
			size_t pathlen;
			const char *path = nn_tolstring(computer, 0, &pathlen);
			if(pathlen >= NN_MAX_PATH) {
				nn_setError(computer, "path too long");
				return NN_EBADCALL;
			}
			nn_simplifyPath(path, truepath);

			fsreq.action = NN_FS_SIZE;
			fsreq.strarg1 = truepath;
			fsreq.strarg1len = nn_strlen(truepath);
			err = state->handler(&fsreq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushnumber(computer, fsreq.size);
		}
		if(nn_strcmp(method, "lastModified") == 0) {
			if(nn_checkstring(computer, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			char truepath[NN_MAX_PATH];
			size_t pathlen;
			const char *path = nn_tolstring(computer, 0, &pathlen);
			if(pathlen >= NN_MAX_PATH) {
				nn_setError(computer, "path too long");
				return NN_EBADCALL;
			}
			nn_simplifyPath(path, truepath);

			fsreq.action = NN_FS_SIZE;
			fsreq.strarg1 = truepath;
			fsreq.strarg1len = nn_strlen(truepath);
			err = state->handler(&fsreq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushnumber(computer, fsreq.size * 1000);
		}
		if(nn_strcmp(method, "isDirectory") == 0) {
			if(nn_checkstring(computer, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			char truepath[NN_MAX_PATH];
			size_t pathlen;
			const char *path = nn_tolstring(computer, 0, &pathlen);
			if(pathlen >= NN_MAX_PATH) {
				nn_setError(computer, "path too long");
				return NN_EBADCALL;
			}
			nn_simplifyPath(path, truepath);

			fsreq.action = NN_FS_ISDIRECTORY;
			fsreq.strarg1 = truepath;
			fsreq.strarg1len = nn_strlen(truepath);
			err = state->handler(&fsreq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushbool(computer, fsreq.size != 0);
		}
	}
	return NN_OK;
}

nn_ComponentType *nn_createFilesystem(nn_Universe *universe, const nn_Filesystem *filesystem, nn_FilesystemHandler *handler, void *userdata) {
	nn_Context ctx = universe->ctx;
	nn_Filesystem_state *state = nn_alloc(&ctx, sizeof(*state));
	if(state == NULL) return NULL;
	state->universe = universe;
	state->userdata = userdata;
	state->handler = handler;
	state->fs = *filesystem;
	const nn_Method methods[] = {
		{"spaceTotal", "function(): number - Get the storage capacity of the filesystem.", NN_DIRECT},
		{"spaceUsed", "function(): number - Get the space used by the files on the drive.", NN_DIRECT},
		{"isReadOnly", "function(): boolean - Returns whether the drive is read-only.", NN_DIRECT},
		{"getLabel", "function(): string - Get the filesystem label.", NN_INDIRECT},
		{"setLabel", "function(label: string): string - Set the filesystem label and return what was actually set, which may be truncated.", NN_INDIRECT},
		{"open", "function(path: string, mode?: string = 'r'): number - Open a file. Valid modes are 'r' (read-only), 'w' (write-only) and 'a' (append-only).", NN_DIRECT},
		{"close", "function(fd: number) - Closes a file.", NN_DIRECT},
		{"read", "function(fd: number, count?: number): string? - Reads part of a file. If there is no more data, nothing is returned.", NN_DIRECT},
		{"write", "function(fd: number, data: string): boolean - Writes the data to a file. Returns true on success.", NN_DIRECT},
		{"seek", "function(fd: number, whence: string, offset: number): number - Seeks a file. Valid whences are 'set' (relative to start), 'cur' (relative to current), 'end' (relative to EoF, backwards). Returns the new position.", NN_DIRECT},
		{"list", "function(path: string): string[] - Returns the names of the entries inside of the directory. Directories get a / appended to their names.", NN_DIRECT},
		{"exists", "function(path: string): boolean - Checks if there exists an entry at the specified path.", NN_DIRECT},
		{"size", "function(path: string) - Gets the size of the entry at the specified path.", NN_DIRECT},
		{"remove", "function(path: string): boolean - Removes the entry at the specified path.", NN_DIRECT},
		{"rename", "function(from: string, to: string): boolean - Renames or moves an entry to a new location.", NN_DIRECT},
		{"isDirectory", "function(path: string): boolean - Checks if the entry at the specified path is a directory.", NN_DIRECT},
		{"lastModified", "function(path: string): number - Returns the UNIX timestamp of the time the entry was last modified. This is stored in milliseconds, but is always a multiple of 1000.", NN_DIRECT},
		{"makeDirectory", "function(path: string): boolean - Creates a directory. Creates parent directories if necessary.", NN_DIRECT},
		{NULL, NULL, NN_INDIRECT},
	};
	nn_ComponentType *t = nn_createComponentType(universe, "filesystem", state, methods, nn_filesystem_handler);
	if(t == NULL) {
		nn_free(&ctx, state, sizeof(*state));
		return NULL;
	}
	return t;
}

nn_ScreenConfig nn_defaultScreens[4] = {
	(nn_ScreenConfig) {
		.maxWidth = 50,
		.maxHeight = 16,
		.maxDepth = 1,
		.paletteColors = 0,
		.features = NN_SCRF_NONE,
	},
	(nn_ScreenConfig) {
		.maxWidth = 80,
		.maxHeight = 25,
		.maxDepth = 4,
		.paletteColors = 16,
		.features = NN_SCRF_MOUSE | NN_SCRF_TOUCHINVERTED,
	},
	(nn_ScreenConfig) {
		.maxWidth = 160,
		.maxHeight = 50,
		.maxDepth = 8,
		.paletteColors = 256,
		.features = NN_SCRF_MOUSE | NN_SCRF_TOUCHINVERTED | NN_SCRF_PRECISE | NN_SCRF_EDITABLECOLORS,
	},
	(nn_ScreenConfig) {
		.maxWidth = 240,
		.maxHeight = 80,
		.maxDepth = 16,
		.paletteColors = 256,
		.features = NN_SCRF_NONE | NN_SCRF_EDITABLECOLORS,
	},
};

typedef struct nn_Screen_state {
	nn_Universe *universe;
	void *userdata;
	nn_ScreenHandler *handler;
} nn_Screen_state;

static nn_Exit nn_screen_handler(nn_ComponentRequest *req) {
	nn_Screen_state *state = req->typeUserdata;
	nn_Context ctx = state->universe->ctx;

	nn_Computer *C = req->computer;

	nn_ScreenRequest scrreq;
	scrreq.computer = C;
	scrreq.userdata = state->userdata;
	scrreq.instance = req->compUserdata;

	const char *method = req->methodCalled;
	
	switch(req->action) {
	case NN_COMP_FREETYPE:
		nn_free(&ctx, state, sizeof(*state));
		return NN_OK;
	case NN_COMP_DEINIT:
		scrreq.action = NN_SCR_DROP;
		return state->handler(&scrreq);
	case NN_COMP_INIT:
		return NN_OK;
	case NN_COMP_ENABLED:
		req->methodEnabled = true;
		return NN_OK;
	case NN_COMP_CALL:
		nn_setError(C, "method not implemented yet");
		return NN_EBADCALL;
	}
	return NN_OK;
}

nn_ComponentType *nn_createScreen(nn_Universe *universe, nn_ScreenHandler *handler, void *userdata) {
	nn_Context ctx = universe->ctx;
	nn_Screen_state *state = nn_alloc(&ctx, sizeof(*state));
	if(state == NULL) return NULL;
	state->handler = handler;
	state->universe = universe;
	state->userdata = userdata;

	const nn_Method methods[] = {
		{"isOn", "function(): boolean - Returns whether the screen is on", NN_DIRECT},
		{"turnOn", "function(): boolean, boolean - Turns the screen on. Returns whether the screen is was off and the new power state.", NN_DIRECT},
		{"turnOff", "function(): boolean, boolean - Turns the screen off. Returns whether the screen is was on and the new power state.", NN_DIRECT},
		{"getAspectRatio", "function(): number, number - Returns how large the screen is, typically in blocks.", NN_DIRECT},
		{"getKeyboards", "function(): string[] - Returns a list of keyboards attached to this screen.", NN_DIRECT},
		{"setPrecise", "function(enabled: boolean): boolean - Enable or disable precise mode (sub-pixel precision in touch events).", NN_DIRECT},
		{"isPrecise", "function(): boolean - Checks if precise mode is enabled.", NN_DIRECT},
		{"setTouchModeInverted", "function(enabled: boolean): boolean - Enable or disable inverted touch mode (alters player interactions)", NN_DIRECT},
		{"isTouchModeInverted", "function(): boolean - Checks if inverted touch mode is enabled.", NN_DIRECT},
		{NULL, NULL, NN_INDIRECT},
	};
	nn_ComponentType *t = nn_createComponentType(universe, "screen", state, methods, nn_screen_handler);
	if(t == NULL) {
		nn_free(&ctx, state, sizeof(*state));
		return NULL;
	}
	return t;
}

static nn_Exit nn_keyboard_handler(nn_ComponentRequest *req) {
	(void)req;
	return NN_OK;
}

nn_ComponentType *nn_createKeyboard(nn_Universe *universe) {
	const nn_Method methods[] = {
		{NULL, NULL, NN_INDIRECT},
	};
	return nn_createComponentType(universe, "keyboard", NULL, methods, nn_keyboard_handler);
}

nn_GPU nn_defaultGPUs[4] = {
	(nn_GPU) {
		.maxWidth = 50,
		.maxHeight = 16,
		.maxDepth = 1,
		.totalVRAM = 5000,
		.copyPerTick = 1,
		.fillPerTick = 1,
		.setPerTick = 4,
		.setForegroundPerTick = 2,
		.setBackgroundPerTick = 2,
		.energyPerWrite = 0.02,
		.energyPerClear = 0.01,
	},
	(nn_GPU) {
		.maxWidth = 80,
		.maxHeight = 25,
		.maxDepth = 4,
		.totalVRAM = 10000,
		.copyPerTick = 2,
		.fillPerTick = 4,
		.setPerTick = 8,
		.setForegroundPerTick = 4,
		.setBackgroundPerTick = 4,
		.energyPerWrite = 0.1,
		.energyPerClear = 0.05,
	},
	(nn_GPU) {
		.maxWidth = 160,
		.maxHeight = 50,
		.maxDepth = 8,
		.totalVRAM = 20000,
		.copyPerTick = 4,
		.fillPerTick = 8,
		.setPerTick = 16,
		.setForegroundPerTick = 8,
		.setBackgroundPerTick = 8,
		.energyPerWrite = 0.2,
		.energyPerClear = 0.1,
	},
	(nn_GPU) {
		.maxWidth = 240,
		.maxHeight = 80,
		.maxDepth = 16,
		.totalVRAM = 65536,
		.copyPerTick = 8,
		.fillPerTick = 12,
		.setPerTick = 32,
		.setForegroundPerTick = 16,
		.setBackgroundPerTick = 16,
		.energyPerWrite = 0.25,
		.energyPerClear = 0.12,
	},
};

typedef struct nn_GPU_state {
	void *userdata;
	nn_GPUHandler *handler;
	nn_Universe *universe;
	nn_GPU gpu;
} nn_GPU_state;

nn_Exit nn_gpu_handler(nn_ComponentRequest *req) {
	nn_Computer *C = req->computer;
	nn_GPU_state *state = req->typeUserdata;
	nn_Context ctx = state->universe->ctx;
	nn_GPU conf = state->gpu;

	nn_GPURequest greq;
	greq.computer = C;
	greq.userdata = state->userdata;
	greq.instance = req->compUserdata;
	greq.gpuConf = &state->gpu;

	const char *method = req->methodCalled;
	nn_Exit err;

	switch(req->action) {
	case NN_COMP_FREETYPE:
		nn_free(&ctx, state, sizeof(*state));
		return NN_OK;
	case NN_COMP_INIT:
		return NN_OK;
	case NN_COMP_DEINIT:
		greq.action = NN_GPU_DROP;
		return state->handler(&greq);
	case NN_COMP_ENABLED:
		req->methodEnabled = true;
		return NN_OK;
	case NN_COMP_CALL:
		if(nn_strcmp(method, "bind") == 0) {
			if(nn_checkstring(C, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
			err = nn_defaultboolean(C, 1, false);
			if(err) return err;
			if(nn_checkboolean(C, 1, "bad argument #2 (bool expected)")) return NN_EBADCALL;
			greq.action = NN_GPU_BIND;
			size_t len;
			greq.text = (char *)nn_tolstring(C, 0, &len);
			greq.width = len;
			greq.x = nn_toboolean(C, 1) ? 1 : 0;
			err = state->handler(&greq);
			if(err) return err;
			return nn_pushbool(C, true);
		}
		if(nn_strcmp(method, "unbind") == 0) {
			greq.action = NN_GPU_UNBIND;
			err = state->handler(&greq);
			if(err) return err;
			return nn_pushbool(C, true);
		}
		if(nn_strcmp(method, "getScreen") == 0) {
			char buf[NN_MAX_ADDRESS];
			greq.action = NN_GPU_GETSCREEN;
			greq.text = buf;
			greq.width = NN_MAX_ADDRESS;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 1;
			if(greq.text == NULL) return nn_pushnull(C);
			return nn_pushlstring(C, greq.text, greq.width);
		}
		if(nn_strcmp(method, "getResolution") == 0) {
			greq.action = NN_GPU_GETRESOLUTION;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 2;
			err = nn_pushinteger(C, greq.width);
			if(err) return err;
			return nn_pushinteger(C, greq.height);
		}
		if(nn_strcmp(method, "set") == 0) {
			nn_costComponent(C, req->compAddress, conf.setPerTick);
			if(nn_checkinteger(C, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
			if(nn_checkinteger(C, 1, "bad argument #2 (integer expected)")) return NN_EBADCALL;
			if(nn_checkstring(C, 2, "bad argument #3 (string expected)")) return NN_EBADCALL;
			err = nn_defaultboolean(C, 3, false);
			if(err) return err;
			if(nn_checkboolean(C, 3, "bad argument #4 (boolean expected)")) return NN_EBADCALL;
			greq.action = nn_toboolean(C, 3) ? NN_GPU_SETVERTICAL : NN_GPU_SET;
			size_t len;
			greq.text = (char *)nn_tolstring(C, 2, &len);
			if(len > conf.maxWidth) len = conf.maxWidth;
			greq.width = len;
			greq.x = nn_tointeger(C, 0);
			greq.y = nn_tointeger(C, 1);
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushbool(C, true);
		}
		if(nn_strcmp(method, "get") == 0) {
			nn_costComponent(C, req->compAddress, conf.setPerTick);
			if(nn_checkinteger(C, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
			if(nn_checkinteger(C, 1, "bad argument #2 (integer expected)")) return NN_EBADCALL;
			greq.action = NN_GPU_GET;
			greq.x = nn_tointeger(C, 0);
			greq.y = nn_tointeger(C, 1);
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 5;
			char buf[NN_MAX_UNICODE_BUFFER];
			size_t len = nn_unicode_codepointToChar(buf, greq.codepoint);
			err = nn_pushlstring(C, buf, len);
			if(err) return err;
			err = nn_pushinteger(C, greq.width);
			if(err) return err;
			err = nn_pushinteger(C, greq.height);
			if(err) return err;
			if(greq.dest == -1) err = nn_pushnull(C);
			else err = nn_pushinteger(C, greq.dest);
			if(err) return err;
			if(greq.src == -1) err = nn_pushnull(C);
			else err = nn_pushinteger(C, greq.src);
			if(err) return err;
			return NN_OK;
		}
		if(nn_strcmp(method, "fill") == 0) {
			nn_costComponent(C, req->compAddress, conf.fillPerTick);
			if(nn_checkinteger(C, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
			if(nn_checkinteger(C, 1, "bad argument #2 (integer expected)")) return NN_EBADCALL;
			if(nn_checkinteger(C, 2, "bad argument #3 (integer expected)")) return NN_EBADCALL;
			if(nn_checkinteger(C, 3, "bad argument #4 (integer expected)")) return NN_EBADCALL;
			if(nn_checkstring(C, 4, "bad argument #5 (string expected)")) return NN_EBADCALL;
			greq.action = NN_GPU_FILL;
			size_t len;
			const char *text = nn_tolstring(C, 4, &len);
			if(nn_unicode_validateFirstChar(text, len) == 0) {
				nn_setError(C, "invalid UTF-8 character");
				return NN_EBADCALL;
			}
			greq.codepoint = nn_unicode_firstCodepoint(text);
			greq.x = nn_tointeger(C, 0);
			greq.y = nn_tointeger(C, 1);
			greq.width = nn_tointeger(C, 2);
			greq.height = nn_tointeger(C, 3);
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushbool(C, true);
		}
		if(nn_strcmp(method, "copy") == 0) {
			nn_costComponent(C, req->compAddress, conf.copyPerTick);
			if(nn_checkinteger(C, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
			if(nn_checkinteger(C, 1, "bad argument #2 (integer expected)")) return NN_EBADCALL;
			if(nn_checkinteger(C, 2, "bad argument #3 (integer expected)")) return NN_EBADCALL;
			if(nn_checkinteger(C, 3, "bad argument #4 (integer expected)")) return NN_EBADCALL;
			if(nn_checkinteger(C, 4, "bad argument #5 (integer expected)")) return NN_EBADCALL;
			if(nn_checkinteger(C, 5, "bad argument #6 (integer expected)")) return NN_EBADCALL;
			greq.action = NN_GPU_COPY;
			greq.x = nn_tointeger(C, 0);
			greq.y = nn_tointeger(C, 1);
			greq.width = nn_tointeger(C, 2);
			greq.height = nn_tointeger(C, 3);
			greq.tx = nn_tointeger(C, 4);
			greq.ty = nn_tointeger(C, 5);
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushbool(C, true);
		}
		if(nn_strcmp(method, "getDepth") == 0) {
			greq.action = NN_GPU_GETDEPTH;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushinteger(C, greq.x);
		}
		if(nn_strcmp(method, "maxDepth") == 0) {
			greq.action = NN_GPU_MAXDEPTH;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 1;
			return nn_pushinteger(C, greq.x);
		}
		if(nn_strcmp(method, "getViewport") == 0) {
			greq.action = NN_GPU_GETVIEWPORT;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 2;
			err = nn_pushinteger(C, greq.width);
			if(err) return err;
			return nn_pushinteger(C, greq.height);
		}
		if(nn_strcmp(method, "getResolution") == 0) {
			greq.action = NN_GPU_GETRESOLUTION;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 2;
			err = nn_pushinteger(C, greq.width);
			if(err) return err;
			return nn_pushinteger(C, greq.height);
		}
		if(nn_strcmp(method, "maxResolution") == 0) {
			greq.action = NN_GPU_MAXRESOLUTION;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 2;
			err = nn_pushinteger(C, greq.width);
			if(err) return err;
			return nn_pushinteger(C, greq.height);
		}
		if(nn_strcmp(method, "setForeground") == 0) {
			if(nn_checkinteger(C, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
			err = nn_defaultboolean(C, 1, false);
			if(err) return err;
			if(nn_checkboolean(C, 1, "bad argument #2 (boolean expected)")) return NN_EBADCALL;
			greq.action = NN_GPU_SETFOREGROUND;
			greq.x = nn_tointeger(C, 0);
			greq.y = nn_toboolean(C, 1) ? 1 : 0;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 2;
			err = nn_pushinteger(C, greq.x);
			if(err) return err;
			err = nn_pushbool(C, greq.y != 0);
			if(err) return err;
			return NN_OK;
		}
		if(nn_strcmp(method, "getForeground") == 0) {
			greq.action = NN_GPU_GETFOREGROUND;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 2;
			err = nn_pushinteger(C, greq.x);
			if(err) return err;
			err = nn_pushbool(C, greq.y != 0);
			if(err) return err;
			return NN_OK;
		}
		if(nn_strcmp(method, "setBackground") == 0) {
			if(nn_checkinteger(C, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
			err = nn_defaultboolean(C, 1, false);
			if(err) return err;
			if(nn_checkboolean(C, 1, "bad argument #2 (boolean expected)")) return NN_EBADCALL;
			greq.action = NN_GPU_SETBACKGROUND;
			greq.x = nn_tointeger(C, 0);
			greq.y = nn_toboolean(C, 1) ? 1 : 0;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 2;
			err = nn_pushinteger(C, greq.x);
			if(err) return err;
			err = nn_pushbool(C, greq.y != 0);
			if(err) return err;
			return NN_OK;
		}
		if(nn_strcmp(method, "getBackground") == 0) {
			greq.action = NN_GPU_GETBACKGROUND;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 2;
			err = nn_pushinteger(C, greq.x);
			if(err) return err;
			err = nn_pushbool(C, greq.y != 0);
			if(err) return err;
			return NN_OK;
		}
		if(nn_strcmp(method, "getBackground") == 0) {
			greq.action = NN_GPU_GETBACKGROUND;
			err = state->handler(&greq);
			if(err) return err;
			req->returnCount = 2;
			err = nn_pushinteger(C, greq.x);
			if(err) return err;
			err = nn_pushbool(C, greq.y != 0);
			if(err) return err;
			return NN_OK;
		}
		nn_setError(C, "method not yet implemented");
		return NN_EBADCALL;
	}
	return NN_OK;
}

nn_ComponentType *nn_createGPU(nn_Universe *universe, const nn_GPU *gpu, nn_GPUHandler *handler, void *userdata) {
	nn_Context ctx = universe->ctx;
	nn_GPU_state *state = nn_alloc(&ctx, sizeof(*state));
	if(state == NULL) return NULL;
	state->handler = handler;
	state->universe = universe;
	state->userdata = userdata;
	state->gpu = *gpu;

	const nn_Method methods[] = {
		{"bind", "function(address: string, reset?: boolean) - Bind the GPU to a screen.", NN_INDIRECT},
		{"unbind", "function() - Unbind the GPU, if bound.", NN_INDIRECT},
		{"getScreen", "function(): string? - Get the screen address, if any.", NN_DIRECT},
		{"getBackground", "function(): integer, boolean - Get the current background color, and whether it is a palette index.", NN_DIRECT},
		{"getForeground", "function(): integer, boolean - Get the current foreground color, and whether it is a palette index.", NN_DIRECT},
		{"setBackground", "function(color: integer, isPalette?: boolean): integer, boolean - Set the current background color. Returns the old background color.", NN_DIRECT},
		{"setForeground", "function(color: integer, isPalette?: boolean): integer, boolean - Set the current foreground color. Returns the old foreground color.", NN_DIRECT},
		{"getPaletteColor", "function(index: integer): integer - Get a color from the palette.", NN_DIRECT},
		{"setPaletteColor", "function(index: integer, color: integer): integer - Change a color from the palette. Returns the old one.", NN_DIRECT},
		{"maxDepth", "function(): integer - Returns the maximum depth supported.", NN_DIRECT},
		{"getDepth", "function(): integer - Returns the current depth used.", NN_DIRECT},
		{"setDepth", "function(depth: integer): string - Change the current depth. Returns the name of the old one.", NN_DIRECT},
		{"maxResolution", "function(): integer, integer - Returns the maximum working resolution.", NN_DIRECT},
		{"getResolution", "function(): integer, integer - Returns the current resolution.", NN_DIRECT},
		{"setResolution", "function(w: integer, h: integer): boolean - Changes the current resolution.", NN_DIRECT},
		{"getViewport", "function(): integer, integer - Returns the current viewport resolution.", NN_DIRECT},
		{"setViewport", "function(w: integer, h: integer): boolean - Changes the current viewport resolution.", NN_DIRECT},
		{"get", "function(x: integer, y: integer): string, integer, integer, integer?, integer? - Gets information about a character. Returns the character, foreground color, background color, foreground palette index (if applicable), background palette index (if applicable).", NN_DIRECT},
		{"set", "function(x: integer, y: integer, value: string, vertical?: boolean): boolean - Writes a string to the screen. The string is simply copied to the buffer, escapes and special characters are not given special treatment.", NN_DIRECT},
		{"copy", "function(x: integer, y: integer, width: integer, height: integer, dx: integer, dy: integer) - Copies a rectangle on the screen buffer to a new position. The new position is x + dx, y + dy, thus dx and dy determine the translation of the copy.", NN_DIRECT},
		{"fill", "function(x: integer, y: integer, width: integer, height: integer, char: string): boolean - Fills a rectangle on the screen buffer. Returns true on success, false otherwise.", NN_DIRECT},
		// TODO: vram buffers
		{NULL, NULL, NN_INDIRECT},
	};
	nn_ComponentType *t = nn_createComponentType(universe, "gpu", state, methods, nn_gpu_handler);
	if(t == NULL) {
		nn_free(&ctx, state, sizeof(*state));
		return NULL;
	}
	return t;
}

int nn_palette2[4] = {
	0x000000,
	0x444444,
	0x999999,
	0xFFFFFF,
};

// The NeoNucleus 3-bit palette
int nn_palette3[8] = {
	0x000000,
	0xFF0000,
	0x00FF00,
	0xFFFF00,
	0x0000FF,
	0xFF00FF,
	0x00FFFF,
	0xFFFFFF,
};

// The OC 4-bit palette.
int nn_ocpalette4[16] = {
	0xFFFFFF, // white
	0xFFCC33, // orange
	0xCC66CC, // magenta
	0x6699FF, // lightblue
	0xFFFF33, // yellow
	0x33CC33, // lime
	0xFF6699, // pink
	0x333333, // gray
	0xCCCCCC, // silver
	0x336699, // cyan
	0x9933CC, // purple
	0x333399, // blue
	0x663300, // brown
	0x336600, // green
	0xFF3333, // red
	0x000000, // black
};

// The Minecraft 4-bit palette, using dye colors.
int nn_mcpalette4[16] = {
	0xFFFFFF, // white
	0xF9801D, // orange
	0xC74EBD, // magenta
	0x3AB3DA, // lightblue
	0xFED83D, // yellow
	0x80C71F, // lime
	0xF38BAA, // pink
	0x474F52, // gray
	0x9D9D97, // silver
	0x169C9C, // cyan
	0x8932B8, // purple
	0x3C44AA, // blue
	0x835432, // brown
	0x5E7C16, // green
	0xB02E26, // red
	0x000000, // black
};

// The OC 8-bit palette.
int nn_ocpalette8[256];

void nn_initPalettes() {
	// generate the 8-bit palette
    // source: https://ocdoc.cil.li/component:gpu
    int reds[6] = {0x00, 0x33, 0x66, 0x99, 0xCC, 0xFF};
    int greens[8] = {0x00, 0x24, 0x49, 0x6D, 0x92, 0xB6, 0xDB, 0xFF};
    int blues[5] = {0x00, 0x40, 0x80, 0xC0, 0xFF};

    for(int r = 0; r < 6; r++) {
        for(int g = 0; g < 8; g++) {
            for(int b = 0; b < 5; b++) {
                int i = r * 8 * 5 + g * 5 + b;
                nn_ocpalette8[i+16] = (reds[r] << 16) | (greens[g] << 8) | (blues[b]);
            }
        }
    }

    // TODO: turn into an algorithm
    nn_ocpalette8[0] = 0x0F0F0F;
    nn_ocpalette8[1] = 0x1E1E1E;
    nn_ocpalette8[2] = 0x2D2D2D;
    nn_ocpalette8[3] = 0x3C3C3C;
    nn_ocpalette8[4] = 0x4B4B4B;
    nn_ocpalette8[5] = 0x5A5A5A;
    nn_ocpalette8[6] = 0x696969;
    nn_ocpalette8[7] = 0x787878;
    nn_ocpalette8[8] = 0x878787;
    nn_ocpalette8[9] = 0x969696;
    nn_ocpalette8[10] = 0xA5A5A5;
    nn_ocpalette8[11] = 0xB4B4B4;
    nn_ocpalette8[12] = 0xC3C3C3;
    nn_ocpalette8[13] = 0xD2D2D2;
    nn_ocpalette8[14] = 0xE1E1E1;
    nn_ocpalette8[15] = 0xF0F0F0;
}

int nn_mapColor(int color, int *palette, size_t len) {
	// TODO: color mapping
	(void)palette;
	(void)len;
	return color;
}

int nn_mapDepth(int color, int depth, bool ocCompatible) {
	if(depth == 1) return color == 0 ? 0 : 0xFFFFFF;
	// TODO: map the other depths
	return color;
}

const char *nn_depthName(int depth) {
	if(depth == 1) return "OneBit";
	if(depth == 2) return "TwoBit";
	if(depth == 3) return "ThreeBit";
	if(depth == 4) return "FourBit";
	if(depth == 8) return "EightBit";
	if(depth == 16) return "SixteenBit";
	if(depth == 24) return "TwentyfourBit";
	return NULL;
}

// Unicode

// both tables copied from: https://github.com/MightyPirates/OpenComputers/blob/52da41b5e171b43fea80342dc75d808f97a0f797/src/main/scala/li/cil/oc/util/FontUtils.scala
static const unsigned char nn_unicode_charWidth_table[] = {
    16, 16, 16, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 16, 33, 16, 16, 16, 34, 35, 36,
    37, 38, 39, 40, 16, 16, 41, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 42, 43, 16, 16, 44, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 45, 16, 46, 47, 48, 49, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 50, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 51, 16, 16, 52,
    53, 16, 54, 55, 56, 16, 16, 16, 16, 16, 16, 57, 16, 16, 58, 16, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68,
    69, 70, 16, 71, 72, 73, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 74, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 75, 76, 16, 16, 16, 77, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 78, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 79, 80, 16, 16, 16, 16, 16, 16, 16, 81, 16, 16, 16, 16, 16, 82, 83, 84, 16, 16, 16, 16, 16, 85,
    86, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 248, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 254, 255, 255, 255, 255, 191, 182, 0, 0, 0, 0, 0, 0, 0, 63, 0, 255, 23, 0, 0, 0, 0, 0, 248, 255,
    255, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 191, 159, 61, 0, 0, 0, 128, 2, 0, 0, 0, 255, 255, 255,
    7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 255, 1, 0, 0, 0, 0, 0, 0, 248, 15, 32, 0, 0, 192, 251, 239, 62, 0, 0,
    0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 248, 255, 255, 255, 255,
    255, 7, 0, 0, 0, 0, 0, 0, 20, 254, 33, 254, 0, 12, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 16, 30, 32, 0, 0, 12, 0, 0,
    64, 6, 0, 0, 0, 0, 0, 0, 16, 134, 57, 2, 0, 0, 0, 35, 0, 6, 0, 0, 0, 0, 0, 0, 16, 190, 33, 0, 0, 12, 0, 0,
    252, 2, 0, 0, 0, 0, 0, 0, 144, 30, 32, 64, 0, 12, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 1, 32, 0, 0, 0, 0, 0, 0, 17,
    0, 0, 0, 0, 0, 0, 192, 193, 61, 96, 0, 12, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 144, 64, 48, 0, 0, 12, 0, 0, 0, 3, 0,
    0, 0, 0, 0, 0, 24, 30, 32, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 92, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    242, 7, 128, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 242, 31, 0, 63, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 160,
    2, 0, 0, 0, 0, 0, 0, 254, 127, 223, 224, 255, 254, 255, 255, 255, 31, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 224, 253, 102, 0, 0, 0, 195, 1, 0, 30, 0, 100, 32, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 224, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 28, 0, 0, 0, 28, 0, 0, 0, 12, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 176, 63, 64, 254,
    15, 32, 0, 0, 0, 0, 0, 120, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 96, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 135, 1, 4, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    128, 9, 0, 0, 0, 0, 0, 0, 64, 127, 229, 31, 248, 159, 0, 0, 0, 0, 0, 0, 255, 127, 0, 0, 0, 0, 0, 0, 0, 0,
    15, 0, 0, 0, 0, 0, 208, 23, 4, 0, 0, 0, 0, 248, 15, 0, 3, 0, 0, 0, 60, 59, 0, 0, 0, 0, 0, 0, 64, 163, 3, 0, 0,
    0, 0, 0, 0, 240, 207, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 247, 255, 253, 33, 16,
    3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255,
    251, 0, 248, 0, 0, 0, 124, 0, 0, 0, 0, 0, 0, 223, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255,
    255, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 3, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 0, 0, 0, 0,
    0, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 128, 247, 63, 0, 0, 0, 192, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 68, 8, 0, 0, 96, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 48, 0, 0, 0, 255, 255, 3, 128, 0, 0, 0, 0, 192, 63, 0, 0, 128, 255, 3, 0,
    0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 200, 51, 0, 0, 0, 0, 32, 0, 0,
    0, 0, 0, 0, 0, 0, 126, 102, 0, 8, 16, 0, 0, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 157, 193, 2, 0, 0, 0, 0, 48, 64, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 33, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0,
    64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 0, 0, 255,
    255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 110, 240, 0,
    0, 0, 0, 0, 135, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 96, 0, 0, 0, 0, 0, 0, 0, 240, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 255, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 255, 127, 0, 0, 0, 0, 0, 0, 128,
    3, 0, 0, 0, 0, 0, 120, 38, 0, 32, 0, 0, 0, 0, 0, 0, 7, 0, 0, 0, 128, 239, 31, 0, 0, 0, 0, 0, 0, 0, 8, 0, 3, 0,
    0, 0, 0, 0, 192, 127, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 211, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 128, 248, 7, 0, 0, 3, 0, 0, 0, 0, 0, 0, 24, 1, 0, 0, 0, 192, 31, 31, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 255, 92, 0, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 248, 133, 13, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 60, 176, 1, 0, 0, 48, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    248, 167, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 40, 191, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 224, 188, 15, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 255, 6, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 240, 12, 1, 0, 0, 0, 254, 7, 0, 0, 0, 0, 248, 121, 128, 0, 126, 14, 0, 0, 0, 0, 0, 252,
    127, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 191, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 252, 255,
    255, 252, 109, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 126, 180, 191, 0, 0, 0, 0, 0, 0, 0, 0, 0, 163, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 0, 0, 0, 0, 0, 0, 0, 255,
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 31, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0, 0, 128, 7, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 96, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 3, 248, 255, 231, 15, 0, 0, 0, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255,
    255, 255, 255, 255, 127, 248, 255, 255, 255, 255, 255, 31, 32, 0, 16, 0, 0, 248, 254, 255, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 127, 255, 255, 249, 219, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 240, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 240, 7, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const unsigned char nn_unicode_charWidth_wide_table[] = {
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 18, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 19, 16, 20, 21, 22, 16, 16, 16, 23, 16, 16, 24, 25, 26, 27, 28, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 29,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 30, 16, 16, 16, 16, 31, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
    17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
    17, 17, 17, 17, 17, 17, 17, 32, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 16, 16, 16, 33,
    34, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 35, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
    17, 17, 17, 17, 17, 17, 36, 17, 17, 37, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 38, 39, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 16, 16, 16, 16, 16, 16, 40, 41, 42, 43, 44, 45, 46, 47, 16, 48, 49, 16, 16, 16, 16,
    16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 1, 0, 0, 0, 80, 184, 0, 0, 0, 0, 0, 0, 0, 224,
    0, 0, 0, 1, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24, 0, 0, 0, 0, 0, 0, 33, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 251, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 15, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 63, 0, 0, 0, 255, 15, 255, 255, 255, 255,
    255, 255, 255, 127, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 127, 254, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 224, 255, 255, 255, 255, 255, 254, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 127, 255, 255, 255, 255, 255, 7, 255, 255, 255, 255, 15, 0,
    255, 255, 255, 255, 255, 127, 255, 255, 255, 255, 255, 0, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0,
    0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 31, 255, 255, 255, 255, 255, 255, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255,
    255, 255, 31, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 3, 0, 0, 255, 255, 255, 255, 247, 255, 127, 15, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 7, 0, 255, 255, 255, 127, 0, 0, 0, 0, 0,
    0, 7, 0, 240, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    15, 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 254, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 255, 255, 255,
    255, 255, 15, 255, 1, 3, 0, 63, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 255, 255,
    1, 224, 191, 255, 255, 255, 255, 255, 255, 255, 255, 223, 255, 255, 15, 0, 255, 255, 255, 255,
    255, 135, 15, 0, 255, 255, 17, 255, 255, 255, 255, 255, 255, 255, 255, 127, 253, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    159, 255, 255, 255, 255, 255, 255, 255, 63, 0, 120, 255, 255, 255, 0, 0, 4, 0, 0, 96, 0, 16, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 248, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 255, 255,
    255, 255, 255, 255, 255, 255, 63, 16, 39, 0, 0, 24, 240, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 15, 0,
    0, 0, 224, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 123, 252, 255, 255, 255,
    255, 231, 199, 255, 255, 255, 231, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 15, 7, 7, 0, 63, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static bool nn_unicode_is_continuation(unsigned char byte) {
    return (byte >> 6) == 0b10;
}

bool nn_unicode_validate(const char *s, size_t len) {
	for(size_t i = 0; i < len;) {
		size_t w = nn_unicode_validateFirstChar(s + i, len - i);
		if(w == 0) return false;
		i += w;
	}
	return true;
}

size_t nn_unicode_validateFirstChar(const char *b, size_t len) {
	if(len < 1) return 0;
	const unsigned char *s = (const unsigned char *)b;
    if(s[0] <= 0x7F) {
        return 1;
    } else if((s[0] >> 5) == 0b110) {
		if(len < 2) return 0;
        if (!nn_unicode_is_continuation(s[1])) {
            return 0;
        }
		return 2;
    } else if((s[0] >> 4) == 0b1110) {
		if(len < 3) return 0;
        if (!nn_unicode_is_continuation(s[1])) {
            return 0;
        }
        if (!nn_unicode_is_continuation(s[2])) {
            return 0;
        }
		return 3;
    } else if((s[0] >> 3) == 0b11110) {
		if(len < 4) return 0;
        if (!nn_unicode_is_continuation(s[1])) {
            return 0;
        }
        if (!nn_unicode_is_continuation(s[2])) {
            return 0;
        }
        if (!nn_unicode_is_continuation(s[3])) {
            return 0;
        }
		return 4;
	}
    return 0;
}

size_t nn_unicode_len(const char *s, size_t len) {
	size_t ulen = 0;
	for(size_t i = 0; i < len;) {
		size_t cw = nn_unicode_validateFirstChar(s + i, len - i);
		i += cw;
		ulen++;
	}
	return ulen;
}

size_t nn_unicode_lenPermissive(const char *s, size_t len) {
	size_t ulen = 0;
	for(size_t i = 0; i < len;) {
		size_t cw = nn_unicode_validateFirstChar(s + i, len - i);
		if(cw == 0) cw = 1;
		i += cw;
		ulen++;
	}
	return ulen;
}

void nn_unicode_codepoints(const char *s, size_t len, nn_codepoint *codepoints) {
	size_t i = 0;
	for(size_t j = 0; j < len;) {
		codepoints[i++] = nn_unicode_firstCodepoint(s + j);
		size_t cw = nn_unicode_validateFirstChar(s + j, len - j);
		j += cw;
	}
}

void nn_unicode_codepointsPermissive(const char *s, size_t len, nn_codepoint *codepoints) {
	size_t i = 0;
	for(size_t j = 0; j < len;) {
		size_t cw = nn_unicode_validateFirstChar(s + j, len - j);
		if(cw == 0) {
			codepoints[i++] = (unsigned char)s[j];
			j++;
		} else {
			codepoints[i++] = nn_unicode_firstCodepoint(s + j);
		}
		j += cw;
	}
}

nn_codepoint nn_unicode_firstCodepoint(const char *s) {
	nn_codepoint point = 0;
	const unsigned char *b = (const unsigned char *)s;
    const unsigned char subpartMask = 0b111111;
    if(b[0] <= 0x7F) {
        return b[0];
    } else if((b[0] >> 5) == 0b110) {
        point += ((unsigned int)(b[0] & 0b11111)) << 6;
        point += ((unsigned int)(b[1] & subpartMask));
    } else if((b[0] >> 4) == 0b1110) {
        point += ((unsigned int)(b[0] & 0b1111)) << 12;
        point += ((unsigned int)(b[1] & subpartMask)) << 6;
        point += ((unsigned int)(b[2] & subpartMask));
    } else if((b[0] >> 3) == 0b11110) {
        point += ((unsigned int)(b[0] & 0b111)) << 18;
        point += ((unsigned int)(b[1] & subpartMask)) << 12;
        point += ((unsigned int)(b[2] & subpartMask)) << 6;
        point += ((unsigned int)(b[3] & subpartMask));
    }
    return point;
}

size_t nn_unicode_codepointSize(nn_codepoint codepoint) {
    if (codepoint <= 0x007f) {
        return 1;
    } else if (codepoint <= 0x07ff) {
        return 2;
    } else if (codepoint <= 0xffff) {
        return 3;
    } else if (codepoint <= 0x10ffff) {
        return 4;
    }

    return 1;
}

size_t nn_unicode_codepointToChar(char buffer[NN_MAX_UNICODE_BUFFER], nn_codepoint codepoint) {
    size_t codepointSize = nn_unicode_codepointSize(codepoint);

    if (codepointSize == 1) {
        buffer[0] = (char)codepoint;
    } else if (codepointSize == 2) {
        buffer[0] = 0b11000000 + ((codepoint >> 6) & 0b11111);
        buffer[1] = 0b10000000 + (codepoint & 0b111111);
    } else if (codepointSize == 3) {
        buffer[0] = 0b11100000 + ((codepoint >> 12) & 0b1111);
        buffer[1] = 0b10000000 + ((codepoint >> 6) & 0b111111);
        buffer[2] = 0b10000000 + (codepoint & 0b111111);
    } else if (codepointSize == 4) {
        buffer[0] = 0b11110000 + ((codepoint >> 18) & 0b111);
        buffer[1] = 0b10000000 + ((codepoint >> 12) & 0b111111);
        buffer[2] = 0b10000000 + ((codepoint >> 6) & 0b111111);
        buffer[3] = 0b10000000 + (codepoint & 0b111111);
    }
	return codepointSize;
}

// copied straight from opencomputers and musl's libc
// https://github.com/MightyPirates/OpenComputers/blob/52da41b5e171b43fea80342dc75d808f97a0f797/src/main/scala/li/cil/oc/util/FontUtils.scala#L205
// https://git.musl-libc.org/cgit/musl/tree/src/ctype/wcwidth.c
size_t nn_unicode_charWidth(nn_codepoint codepoint) {
    if (codepoint < 0xff) {
        if (((codepoint + 1) & 0x7f) >= 0x21) {
            return 1;
        } else {
            return 0;
        }
    } else if ((codepoint & 0xfffeffff) < 0xfffe) {
        if ((nn_unicode_charWidth_table[nn_unicode_charWidth_table[codepoint>>8]*32+((codepoint&255)>>3)]>>(codepoint&7))&1)
			return 0;
		if ((nn_unicode_charWidth_wide_table[nn_unicode_charWidth_wide_table[codepoint>>8]*32+((codepoint&255)>>3)]>>(codepoint&7))&1)
			return 2;
        return 1;
    } else if (codepoint-0x20000 < 0x20000) {
        return 2;
    } else if (codepoint == 0xe0001 || codepoint-0xe0020 < 0x5f || codepoint-0xe0100 < 0xef) {
        return 0;
    }
    return 1;
}

size_t nn_unicode_wlen(const char *s, size_t len) {
	size_t wlen = 0;
	for(size_t i = 0; i < len;) {
		nn_codepoint codepoint = nn_unicode_firstCodepoint(s + i);
		size_t size = nn_unicode_codepointSize(codepoint);
		size_t width = nn_unicode_charWidth(codepoint);
		if(width == 0) width = 1;
		wlen += width;
		i += size;
	}
	return wlen;
}

size_t nn_unicode_wlenPermissive(const char *s, size_t len) {
	size_t wlen = 0;
	for(size_t i = 0; i < len;) {
		if(nn_unicode_validateFirstChar(s + i, len - i) == 0) {
			size_t width = nn_unicode_charWidth((unsigned char)s[i]);
			if(width == 0) width = 1;
			wlen += width;
			i++;
		} else {
			nn_codepoint codepoint = nn_unicode_firstCodepoint(s + i);
			size_t size = nn_unicode_codepointSize(codepoint);
			size_t width = nn_unicode_charWidth(codepoint);
			if(width == 0) width = 1;
			wlen += width;
			i += size;
		}
	}
	return wlen;
}

size_t nn_unicode_countBytes(nn_codepoint *codepoints, size_t len) {
	size_t count = 0;
	for(size_t i = 0; i < len; i++) count += nn_unicode_codepointSize(codepoints[i]);
	return count;
}

void nn_unicode_writeBytes(char *s, nn_codepoint *codepoints, size_t len) {
	for(size_t i = 0; i < len; i++) {
		nn_codepoint cp = codepoints[i];
		size_t size = nn_unicode_codepointSize(cp);
		nn_unicode_codepointToChar(s, cp);
		s += size;
	}
}

// TODO: impl ts
nn_codepoint nn_unicode_upper(nn_codepoint codepoint) {
	return codepoint;
}

nn_codepoint nn_unicode_lower(nn_codepoint codepoint) {
	return codepoint;
}

// signal helper funcs

nn_Exit nn_pushScreenResized(nn_Computer *computer, const char *screenAddress, int newWidth, int newHeight) {
	nn_Exit err = nn_pushstring(computer, "screen_resized");
	if(err) return err;
	err = nn_pushstring(computer, screenAddress);
	if(err) return err;
	err = nn_pushinteger(computer, newWidth);
	if(err) return err;
	err = nn_pushinteger(computer, newHeight);
	if(err) return err;
	return nn_pushSignal(computer, 4);
}

nn_Exit nn_pushTouch(nn_Computer *computer, const char *screenAddress, double x, double y, int button, const char *player) {
	if(!nn_hasUser(computer, player)) return NN_OK;
	nn_Exit err = nn_pushstring(computer, "touch");
	if(err) return err;
	err = nn_pushstring(computer, screenAddress);
	if(err) return err;
	err = nn_pushnumber(computer, x);
	if(err) return err;
	err = nn_pushnumber(computer, y);
	if(err) return err;
	err = nn_pushinteger(computer, button);
	if(err) return err;
	err = nn_pushstring(computer, player);
	if(err) return err;
	return nn_pushSignal(computer, 6);
}

nn_Exit nn_pushDrag(nn_Computer *computer, const char *screenAddress, double x, double y, int button, const char *player) {
	if(!nn_hasUser(computer, player)) return NN_OK;
	nn_Exit err = nn_pushstring(computer, "drag");
	if(err) return err;
	err = nn_pushstring(computer, screenAddress);
	if(err) return err;
	err = nn_pushnumber(computer, x);
	if(err) return err;
	err = nn_pushnumber(computer, y);
	if(err) return err;
	err = nn_pushinteger(computer, button);
	if(err) return err;
	err = nn_pushstring(computer, player);
	if(err) return err;
	return nn_pushSignal(computer, 6);
}

nn_Exit nn_pushDrop(nn_Computer *computer, const char *screenAddress, double x, double y, int button, const char *player) {
	if(!nn_hasUser(computer, player)) return NN_OK;
	nn_Exit err = nn_pushstring(computer, "drag");
	if(err) return err;
	err = nn_pushstring(computer, screenAddress);
	if(err) return err;
	err = nn_pushnumber(computer, x);
	if(err) return err;
	err = nn_pushnumber(computer, y);
	if(err) return err;
	err = nn_pushinteger(computer, button);
	if(err) return err;
	err = nn_pushstring(computer, player);
	if(err) return err;
	return nn_pushSignal(computer, 6);
}

nn_Exit nn_pushScroll(nn_Computer *computer, const char *screenAddress, double x, double y, double direction, const char *player) {
	if(!nn_hasUser(computer, player)) return NN_OK;
}

nn_Exit nn_pushWalk(nn_Computer *computer, const char *screenAddress, double x, double y, const char *player) {
	if(!nn_hasUser(computer, player)) return NN_OK;
}

nn_Exit nn_pushKeyDown(nn_Computer *computer, const char *keyboardAddress, nn_codepoint charcode, int keycode, const char *player) {
	if(!nn_hasUser(computer, player)) return NN_OK;
	nn_Exit err = nn_pushstring(computer, "key_down");
	if(err) return err;
	err = nn_pushstring(computer, keyboardAddress);
	if(err) return err;
	err = nn_pushinteger(computer, charcode);
	if(err) return err;
	err = nn_pushinteger(computer, keycode);
	if(err) return err;
	return nn_pushSignal(computer, 4);
}

nn_Exit nn_pushKeyUp(nn_Computer *computer, const char *keyboardAddress, nn_codepoint charcode, int keycode, const char *player) {
	if(!nn_hasUser(computer, player)) return NN_OK;
	nn_Exit err = nn_pushstring(computer, "key_down");
	if(err) return err;
	err = nn_pushstring(computer, keyboardAddress);
	if(err) return err;
	err = nn_pushinteger(computer, charcode);
	if(err) return err;
	err = nn_pushinteger(computer, keycode);
	if(err) return err;
	return nn_pushSignal(computer, 4);
}

nn_Exit nn_pushClipboard(nn_Computer *computer, const char *keyboardAddress, const char *clipboard, const char *player) {
	if(!nn_hasUser(computer, player)) return NN_OK;
	return nn_pushLClipboard(computer, keyboardAddress, clipboard, nn_strlen(clipboard), player);
}

nn_Exit nn_pushLClipboard(nn_Computer *computer, const char *keyboardAddress, const char *clipboard, size_t len, const char *player) {
	if(!nn_hasUser(computer, player)) return NN_OK;
	nn_Exit err = nn_pushstring(computer, "clipboard");
	if(err) return err;
	err = nn_pushstring(computer, keyboardAddress);
	if(err) return err;
	err = nn_pushlstring(computer, clipboard, len);
	if(err) return err;
	return nn_pushSignal(computer, 3);
}
