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

void nn_incRef(nn_refc_t *refc, size_t n) {
	atomic_fetch_add(refc, n);
}

bool nn_decRef(nn_refc_t *refc, size_t n) {
	nn_refc_t old = atomic_fetch_sub(refc, n);
	return old == n;
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

size_t nn_strlenUntil(const char *s, char sep) {
	size_t l = 0;
	while(1) {
		char c = s[l];
		if(c == '\0') break;
		if(c == sep) break;
		l++;
	}
	return l;
}

void nn_memcpy(void *dest, const void *src, size_t len) {
	char *out = (char *)dest;
	const char *in = (const char *)src;
	for(size_t i = 0; i < len; i++) out[i] = in[i];
}

void nn_strcpy(char *dest, const char *src) {
	while(1) {
		*dest = *src;
		if(*src == '\0') break;
		dest++;
		src++;
	}
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
	if(s == NULL) return;
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

static bool nn_isLiterallyJust(const char *s, size_t len, char c) {
	for(size_t i = 0; i < len; i++) if(s[i] != c) return false;
	return true;
}

void nn_simplifyPath(const char original[NN_MAX_PATH], char simplified[NN_MAX_PATH]) {
	// pass 1: check for valid characters, and \ becomes /
	for(size_t i = 0; true; i++) {
		if(original[i] == '\\') simplified[i] = '/';
		else simplified[i] = original[i];
		if(original[i] == '\0') break;
	}
	// this is similar to KOCOS pathfixing
	// in https://github.com/NeoFlock/onyx-os/blob/main/usr/src/kocos/fs.lua#L237
	{
		char resolved[NN_MAX_PATH];

		struct {const char *mem; size_t len;} slices[NN_MAX_PATH];
		size_t slicecount = 0;

		size_t i = 0;
		while(1) {
			if(simplified[i] == '\0') break;
			char *mem = simplified + i;
			size_t sublen = nn_strlenUntil(mem, '/');

			if(sublen == 0) {
				i++;
				continue;
			}
			slices[slicecount].mem = mem;
			slices[slicecount].len = sublen;
			slicecount++;
			if(nn_isLiterallyJust(mem, sublen, '.')) {
				// no underflow for u
				if(slicecount < sublen) slicecount = sublen;
				slicecount -= sublen;
			}
			if(mem[sublen] == '\0') break;
			i += sublen + 1;
		}

		// concat into resolved
		size_t resolvedLen = 0;
		for(size_t i = 0; i < slicecount; i++) {
			bool isLast = (i == (slicecount - 1));
			char *dest = resolved + resolvedLen;
			nn_memcpy(dest, slices[i].mem, slices[i].len);
			dest[slices[i].len] = isLast ? '\0' : '/';
			resolvedLen += slices[i].len + 1;
		}
		resolved[resolvedLen] = '\0';

		// copy over
		nn_strcpy(simplified, resolved);
	}
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

void nn_randomUUID(nn_Context *ctx, nn_uuid uuid) {
	// inaccurate
	// TODO: make it correct, based off how uuid.lua generates them

	const char *alpha = "0123456789abcdef";
	for(int i = 0; i < 36; i++) {
		uuid[i] = alpha[nn_rand(ctx) & 0xF];
	}
	uuid[36] = '\0';
	uuid[8] = '-';
	uuid[13] = '-';
	uuid[18] = '-';
	uuid[23] = '-';
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

// some util data structures

#define NN_PTROFF(p, i, size) (void *)(((size_t)(p)) + ((i) * (size)))

typedef enum nn_HashEntryState {
	// equal
	NN_HASH_EQUAL,
	// different, ignore
	NN_HASH_DIFFERENT,
	// The slot was removed.
	NN_HASH_REMOVED,
	// Free but not equal.
	NN_HASH_FREE,
} nn_HashEntryState;

typedef enum nn_HashAction {
	// init to free
	NN_HASH_INIT,
	NN_HASH_HASH,
	NN_HASH_REMOVE,
	// checks if slot is equal to entry
	NN_HASH_CMP,
} nn_HashAction;

// slot is the memory in the hashmap.
// for NN_HASH_CMP, entry is the key, and may be NULL if we are only trying to get the state of a slot for iteration.
// if entry is NULL, NN_HASH_EQUAL is invalid and NN_HASH_DIFFERENT means it is used up.
// for NN_HASH_CMP, a HashEntryState should be returned.
typedef size_t (nn_HashHandler)(nn_HashAction action, void *slot, void *entry);

typedef struct nn_HashContext {
	size_t entSize;
	nn_HashHandler *handler;
} nn_HashContext;

// not a dynamic hashmap, has a fixed capacity.
// This is because every hashmap we care about has a known maximum capacity that is equal to the length
// and thus we literally do not care
typedef struct nn_HashMap {
	void *buf;
	size_t bufsize;
	nn_Context *ctx;
	const nn_HashContext *hash;
} nn_HashMap;

nn_Exit nn_hashInit(nn_HashMap *map, size_t capacity, nn_Context *ctx, const nn_HashContext *hash) {
	void *buf = nn_alloc(ctx, hash->entSize * capacity);
	if(buf == NULL) return NN_ENOMEM;
	map->buf = buf;
	map->bufsize = capacity;
	map->ctx = ctx;
	map->hash = hash;
	for(size_t i = 0; i < map->bufsize; i++) {
		hash->handler(NN_HASH_INIT, NN_PTROFF(map->buf, i, hash->entSize), NULL);
	}
	return NN_OK;
}

// note: does not free entries
void nn_hashDeinit(nn_HashMap *map) {
	nn_free(map->ctx, map->buf, map->hash->entSize * map->bufsize);
}

size_t nn_hashGetHash(nn_HashMap *map, void *entry) {
	return map->hash->handler(NN_HASH_HASH, entry, NULL);
}
void *nn_hashGetAt(nn_HashMap *map, size_t idx) {
	return NN_PTROFF(map->buf, idx, map->hash->entSize);
}

// get by entry by key. It is assumed that the entry is NULL.
void *nn_hashGet(nn_HashMap *map, void *entry) {
	if(entry == NULL) return NULL;
	size_t len = map->bufsize;
	if(len == 0) return NULL;
	size_t base = nn_hashGetHash(map, entry) % len;
	size_t entSize = map->hash->entSize;
	for(size_t i = 0; i < len; i++) {
		size_t j = (base + i) % len;
		void *slot = NN_PTROFF(map->buf, j, entSize);
		nn_HashEntryState state = map->hash->handler(NN_HASH_CMP, slot, entry);
		switch(state) {
		case NN_HASH_EQUAL:
			return slot;
		case NN_HASH_DIFFERENT:
		case NN_HASH_REMOVED:
			continue;
		case NN_HASH_FREE:
			break;
		}
	}
	return NULL;
}

// should put the entire entry over there.
// False on ENOSPC.
bool nn_hashPut(nn_HashMap *map, void *entry) {
	if(entry == NULL) return false;
	size_t len = map->bufsize;
	if(len == 0) return false;
	size_t base = nn_hashGetHash(map, entry);
	size_t entSize = map->hash->entSize;
	for(size_t i = 0; i < len; i++) {
		size_t j = (base + i) % len;
		void *slot = NN_PTROFF(map->buf, j, entSize);
		nn_HashEntryState state = map->hash->handler(NN_HASH_CMP, slot, entry);
		switch(state) {
		case NN_HASH_EQUAL:
		case NN_HASH_REMOVED:
		case NN_HASH_FREE:
			nn_memcpy(slot, entry, entSize);
			return true;
		case NN_HASH_DIFFERENT:
			break;
		}
	}
	return false;
}

// remove an entry
void nn_hashRemove(nn_HashMap *map, void *entry) {
	void *mem = nn_hashGet(map, entry);
	if(mem == NULL) return;
	map->hash->handler(NN_HASH_REMOVE, mem, NULL);
}

// takes in an entry and returns the next one. If entry is NULL, it will return the first one.
// Returns NULL on empty.
// entry must be either NULL or a pointer to the map's buffer.
void *nn_hashIterate(nn_HashMap *map, void *entry) {
	size_t entSize = map->hash->entSize;
	void *bufEnd = NN_PTROFF(map->buf, map->bufsize, entSize);
	if(entry == NULL) {
		if(map->bufsize == 0) return NULL;
		entry = map->buf;
	} else {
		entry = NN_PTROFF(entry, 1, entSize);
	}
	while(true) {
		if(entry == bufEnd) return NULL;
		nn_HashEntryState state = map->hash->handler(NN_HASH_CMP, entry, NULL);
		if(state == NN_HASH_DIFFERENT) break;
		entry = NN_PTROFF(entry, 1, entSize);
	}
	return entry;
}

// from https://gist.github.com/MohamedTaha98/ccdf734f13299efb73ff0b12f7ce429f
// TODO: experiment with better ones
size_t nn_strhash(const char *s) {
	size_t hash = 5381;
	int c;
	while ((c = *s++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	return hash;
}

// real stuff

typedef struct nn_MethodEntry {
	const char *name;
	const char *doc;
	nn_MethodFlags flags;
	unsigned int idx;
} nn_MethodEntry;

typedef struct nn_Component {
	nn_refc_t refc;
	nn_Universe *universe;
	char *address;
	char *type;
	char *internalID;
	void *state;
	void *classState;
	nn_ComponentHandler *handler;
	nn_Arena methodArena;
	nn_HashMap methodsMap;
	size_t methodCount;
} nn_Component;

static size_t nn_methodHash(nn_HashAction act, nn_MethodEntry *slot, nn_MethodEntry *ent) {
	switch(act) {
	case NN_HASH_INIT:
		slot->name = NULL;
		slot->flags = -1;
		break;
	case NN_HASH_HASH:
		return nn_strhash(slot->name);
	case NN_HASH_REMOVE:
		slot->flags = -1;
		break;
	case NN_HASH_CMP:
		if(slot->name == NULL) return NN_HASH_FREE;
		if(slot->flags == -1) return NN_HASH_REMOVED;
		if(ent == NULL) {
			return NN_HASH_DIFFERENT;
		}
		return nn_strcmp(slot->name, ent->name) == 0 ? NN_HASH_EQUAL : NN_HASH_DIFFERENT;
	}
	return 0;
}

static const nn_HashContext nn_methodHasher = {
	.entSize = sizeof(nn_MethodEntry),
	.handler = (nn_HashHandler *)nn_methodHash,
};

// currently just a wrapper around a context
// but will be way more in the future
typedef struct nn_Universe {
	nn_Context ctx;
} nn_Universe;

typedef struct nn_ComponentEntry {
	const char *address;
	nn_Component *comp;
	double budgetUsed;
	int slot;
} nn_ComponentEntry;

static size_t nn_componentHash(nn_HashAction act, nn_ComponentEntry *slot, nn_ComponentEntry *ent) {
	switch(act) {
	case NN_HASH_INIT:
		slot->address = NULL;
		slot->comp = NULL;
		break;
	case NN_HASH_HASH:
		return nn_strhash(slot->address);
	case NN_HASH_REMOVE:
		slot->comp = NULL;
		break;
	case NN_HASH_CMP:
		if(slot->address == NULL) return NN_HASH_FREE;
		if(slot->comp == NULL) return NN_HASH_REMOVED;
		if(ent == NULL) return NN_HASH_DIFFERENT;
		return nn_strcmp(slot->address, ent->address) == 0 ? NN_HASH_EQUAL : NN_HASH_DIFFERENT;
	}
	return 0;
}

static const nn_HashContext nn_componentHasher = {
	.entSize = sizeof(nn_ComponentEntry),
	.handler = (nn_HashHandler *)nn_componentHash,
};

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
	nn_HashMap components;
	double totalEnergy;
	void *energyState;
	nn_EnergyHandler *energyHandler;
	size_t totalMemory;
	double creationTimestamp;
	size_t stackSize;
	size_t archCount;
	size_t signalCount;
	size_t userCount;
	double idleTimestamp;
	double memoryScale;
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

double nn_default_energyHandler(void *state, nn_Computer *computer, double amount) {
	(void)state;
	(void)amount;
	return nn_getTotalEnergy(computer);
}

size_t nn_ramSizes[8] = {
	192 * NN_KiB,
	256 * NN_KiB,
	384 * NN_KiB,
	512 * NN_KiB,
	768 * NN_KiB,
	NN_MiB,
	NN_MiB + 512 * NN_KiB,
	2 * NN_MiB,
};

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

	c->totalCallBudget = 10000;
	c->callBudget = c->totalCallBudget;

	if(nn_hashInit(&c->components, maxComponents, ctx, &nn_componentHasher)) {
		nn_strfree(ctx, c->address);
		nn_free(ctx, c, sizeof(nn_Computer));
		return NULL;
	}

	c->totalEnergy = 500;
	c->energyState = NULL;
	c->energyHandler = nn_default_energyHandler;
	c->totalMemory = totalMemory;
	c->creationTimestamp = nn_currentTime(ctx);
	c->stackSize = 0;
	c->archCount = 0;
	c->signalCount = 0;
	c->userCount = 0;
	c->idleTimestamp = 0;
	c->memoryScale = 1;
	// set to empty string
	c->errorBuffer[0] = '\0';
	return c;
}

static void nn_dropValue(nn_Value val);

static nn_ComponentEntry *nn_getInternalComponent(nn_Computer *computer, const char *address) {
	nn_ComponentEntry lookingFor = {
		.address = address,
	};
	return nn_hashGet(&computer->components, &lookingFor);
}

static const nn_MethodEntry *nn_getInternalMethod(nn_Component *c, const char *method) {
	nn_MethodEntry lookingFor = {
		.name = method,
	};
	return nn_hashGet(&c->methodsMap, &lookingFor);
}

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
	for(nn_ComponentEntry *c = nn_hashIterate(&computer->components, NULL); c != NULL; c = nn_hashIterate(&computer->components, c)) {
		if(c->slot >= 0 || (computer->tmpaddress != NULL && nn_strcmp(computer->tmpaddress, c->address))) {
			nn_signalComponent(c->comp, computer, NN_CSIGRESET);
		}
		nn_dropComponent(c->comp);
	}
	for(size_t i = 0; i < computer->signalCount; i++) {
		nn_Signal s = computer->signals[i];
		for(size_t j = 0; j < s.len; j++) nn_dropValue(s.values[j]);
		nn_free(ctx, s.values, sizeof(nn_Value) * s.len);
	}
	for(size_t i = 0; i < computer->userCount; i++) {
		nn_strfree(ctx, computer->users[i]);
	}

	nn_hashDeinit(&computer->components);
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

double nn_getEnergy(nn_Computer *computer) {
	double newEnergy = computer->energyHandler(computer->energyState, computer, 0);
	if(newEnergy <= 0) {
		newEnergy = 0;
		computer->state = NN_BLACKOUT;
	}
	return newEnergy;
}

bool nn_removeEnergy(nn_Computer *computer, double energy) {
	double newEnergy = computer->energyHandler(computer->energyState, computer, energy);
	if(newEnergy <= 0) {
		newEnergy = 0;
		computer->state = NN_BLACKOUT;
		return true;
	}
	return false;
}

void nn_setEnergyHandler(nn_Computer *computer, void *energyState, nn_EnergyHandler *handler) {
	computer->energyState = energyState;
	computer->energyHandler = handler;
}

void nn_setMemoryScale(nn_Computer *computer, double scale) {
	computer->memoryScale = scale;
}

double nn_getMemoryScale(nn_Computer *computer) {
	return computer->memoryScale;
}

size_t nn_getTotalMemory(nn_Computer *computer) {
	return computer->totalMemory;
}

size_t nn_getFreeMemory(nn_Computer *computer) {
	if(computer->state == NN_BOOTUP) return computer->totalMemory;
	nn_ArchitectureRequest req;
	req.computer = computer;
	req.action = NN_ARCH_FREEMEM;
	req.globalState = computer->arch.state;
	req.localState = computer->archState;
	
	computer->arch.handler(&req);
	return req.freeMemory;
}

size_t nn_getUsedMemory(nn_Computer *computer) {
	return nn_getTotalMemory(computer) - nn_getFreeMemory(computer);
}

double nn_getUptime(nn_Computer *computer) {
	return nn_currentTime(&computer->universe->ctx) - computer->creationTimestamp;
}

nn_Exit nn_deserializeComputer(nn_Computer *computer, const char *buf, size_t buflen) {
	nn_ArchitectureRequest req;
	req.computer = computer;
	req.action = NN_ARCH_DESERIALIZE;
	req.globalState = computer->arch.state;
	req.localState = computer->archState;
	req.memIn = buf;
	req.memLen = buflen;
	
	return computer->arch.handler(&req);
}

nn_Exit nn_serializeComputer(nn_Computer *computer, char **buf, size_t *buflen) {
	nn_ArchitectureRequest req;
	req.computer = computer;
	req.action = NN_ARCH_SERIALIZE;
	req.globalState = computer->arch.state;
	req.localState = computer->archState;
	
	nn_Exit e = computer->arch.handler(&req);
	if(e) return e;

	*buf = req.memOut;
	*buflen = req.memLen;

	return NN_OK;
}

nn_Exit nn_freeSerializedComputer(nn_Computer *computer, char *buf, size_t buflen) {
	nn_ArchitectureRequest req;
	req.computer = computer;
	req.action = NN_ARCH_DROPSERIALIZED;
	req.globalState = computer->arch.state;
	req.localState = computer->archState;
	req.memOut = buf;
	req.memLen = buflen;
	
	return computer->arch.handler(&req);
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

bool nn_isComputerIdle(nn_Computer *computer) {
	return nn_getUptime(computer) < computer->idleTimestamp;
}

void nn_addIdleTime(nn_Computer *computer, double time) {
	computer->idleTimestamp += time;
}

void nn_resetIdleTime(nn_Computer *computer) {
	computer->idleTimestamp = -1;
}

nn_Exit nn_tick(nn_Computer *computer) {
	nn_resetCallBudget(computer);
	nn_resetComponentBudgets(computer);
	nn_clearstack(computer);
	nn_Exit err;
	// idling pootr
	if(nn_isComputerIdle(computer)) return NN_OK;
	computer->idleTimestamp = nn_getUptime(computer);
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

// TODO: every component method src/neonucleus.h:530

static nn_Exit nn_defaultComponent(nn_ComponentRequest *request) {
	return NN_OK;
}

nn_Component *nn_createComponent(nn_Universe *universe, const char *address, const char *type) {
	nn_Context *ctx = &universe->ctx;
	nn_Component *c = nn_alloc(ctx, sizeof(*c));
	if(c == NULL) return NULL;
	nn_arinit(&c->methodArena, ctx);
	c->universe = universe;
	c->state = NULL;
	c->address = NULL;
	c->internalID = NULL;
	c->type = NULL;
	c->refc = 1;
	c->handler = nn_defaultComponent;
	c->methodCount = 0;
	c->methodsMap.ctx = NULL;

	if(address == NULL) {
		c->address = nn_alloc(ctx, sizeof(nn_uuid));
		if(c->address == NULL) goto fail;
		nn_randomUUID(ctx, c->address);
	} else {
		c->address = nn_strdup(ctx, address);
		if(c->address == NULL) goto fail;
	}

	c->type = nn_strdup(ctx, type);
	if(c->type == NULL) goto fail;

	c->internalID = nn_strdup(ctx, type);
	if(c->internalID == NULL) goto fail;

	// cannot fail, as does not actually allocate
	nn_hashInit(&c->methodsMap, 0, ctx, &nn_methodHasher);

	return c;
fail:;
	 nn_ardestroy(&c->methodArena);
	 if(c->methodsMap.ctx != NULL) nn_hashDeinit(&c->methodsMap);
	 nn_strfree(ctx, c->address);
	 nn_strfree(ctx, c->internalID);
	 nn_strfree(ctx, c->type);
	 nn_free(ctx, c, sizeof(*c));
	 return NULL;
}

void nn_retainComponent(nn_Component *c) {
	nn_retainComponentN(c, 1);
}

void nn_retainComponentN(nn_Component *c, size_t n) {
	nn_incRef(&c->refc, n);
}

void nn_dropComponent(nn_Component *c) {
	nn_dropComponentN(c, 1);
}

void nn_dropComponentN(nn_Component *c, size_t n) {
	if(!nn_decRef(&c->refc, n)) return;
	nn_Context *ctx = &c->universe->ctx;

	nn_ComponentRequest req;
	req.state = c->state;
	req.classState = c->classState;
	req.ctx = ctx;
	req.computer = NULL;
	req.action = NN_COMP_DROP;
	c->handler(&req);

	nn_ardestroy(&c->methodArena);
	nn_strfree(ctx, c->address);
	nn_strfree(ctx, c->type);
	nn_strfree(ctx, c->internalID);
	nn_hashDeinit(&c->methodsMap);
	nn_free(ctx, c, sizeof(*c));
}

void nn_setComponentHandler(nn_Component *c, nn_ComponentHandler *handler) {
	c->handler = handler;
}

void nn_setComponentState(nn_Component *c, void *state) {
	c->state = state;
}

void nn_setComponentClassState(nn_Component *c, void *state) {
	c->classState = state;
}

nn_Exit nn_setComponentMethods(nn_Component *c, const nn_Method *methods) {
	size_t len = 0;
	while(methods[len].name != NULL) len++;
	return nn_setComponentMethodsArray(c, methods, len);
}

nn_Exit nn_setComponentMethodsArray(nn_Component *c, const nn_Method *methods, size_t count) {
	nn_Context *ctx = &c->universe->ctx;
	nn_Exit e = nn_hashInit(&c->methodsMap, count, ctx, &nn_methodHasher);
	if(e) return e;
	nn_ardestroy(&c->methodArena);
	nn_arinit(&c->methodArena, ctx);
	for(size_t i = 0; i < count; i++) {
		const char *name = nn_arstrdup(&c->methodArena, methods[i].name);
		if(name == NULL) goto fail;
		const char *doc = nn_arstrdup(&c->methodArena, methods[i].doc);
		if(doc == NULL) goto fail;

		nn_MethodEntry method = {
			.name = name,
			.doc = doc,
			.flags = methods[i].flags,
			.idx = i,
		};
		if(!nn_hashPut(&c->methodsMap, &method)) goto fail;
	}
	c->methodCount = count;
	return NN_OK;
fail:
	nn_hashDeinit(&c->methodsMap);
	nn_hashInit(&c->methodsMap, 0, ctx, &nn_methodHasher);
	nn_ardestroy(&c->methodArena);
	nn_arinit(&c->methodArena, ctx);
	c->methodCount = 0;
	return NN_ENOMEM;
}

// Sets an internal type ID, which is meant to be a more precise typename.
// For example, ncomplib would set ncl-screen for the screen component,
// so the GPU can confirm it is being bound to a screen it knows how to use.
nn_Exit nn_setComponentTypeID(nn_Component *c, const char *internalTypeID) {
	char *newType = nn_strdup(&c->universe->ctx, internalTypeID);
	if(newType == NULL) return NN_ENOMEM;
	nn_strfree(&c->universe->ctx, c->internalID);
	c->internalID = newType;
	return NN_OK;
}

void *nn_getComponentState(nn_Component *c) {
	return c->state;
}

void *nn_getComponentClassState(nn_Component *c) {
	return c->classState;
}

// counts how many methods are registered. May return too many if some of them are not enabled.
size_t nn_countComponentMethods(nn_Component *c) {
	return c->methodCount;
}

static nn_MethodEntry *nn_getComponentMethodEntry(nn_Component *c, const char *method) {
	nn_MethodEntry ent = {
		.name = method,
	};
	return nn_hashGet(&c->methodsMap, &ent);
}

// will fill the methodnames array with the names of the *enabled* methods.
// Will set *len to the amount of methods.
void nn_getComponentMethods(nn_Component *c, const char **methodnames, size_t *len) {
	size_t enabled = 0;
	nn_HashMap *m = &c->methodsMap;

	for(nn_MethodEntry *ent = nn_hashIterate(m, NULL); ent != NULL; ent = nn_hashIterate(m, ent)) {
		if(!nn_hasComponentMethod(c, ent->name)) continue;
		methodnames[enabled] = ent->name;
		enabled++;
	}

	*len = enabled;
}

bool nn_hasComponentMethod(nn_Component *c, const char *method) {
	nn_MethodEntry *ent = nn_getComponentMethodEntry(c, method);
	if(ent == NULL) return false;

	nn_ComponentRequest req;
	req.ctx = &c->universe->ctx;
	req.computer = NULL;
	req.state = c->state;
	req.action = NN_COMP_CHECKMETHOD;
	req.methodIdx = ent->idx;
	// by default, yes
	req.methodEnabled = true;
	c->handler(&req);
	return req.methodEnabled;
}

const char *nn_getComponentDoc(nn_Component *c, const char *method) {
	nn_MethodEntry *ent = nn_getComponentMethodEntry(c, method);
	if(ent == NULL) return NULL;
	return ent->doc;
}

nn_MethodFlags nn_getComponentMethodFlags(nn_Component *c, const char *method) {
	nn_MethodEntry *ent = nn_getComponentMethodEntry(c, method);
	if(ent == NULL) return -1;
	return ent->flags;
}

const char *nn_getComponentType(nn_Component *c) {
	return c->type;
}

const char *nn_getComponentTypeID(nn_Component *c) {
	return c->internalID;
}

static nn_Exit nn_pushComponentAdded(nn_Computer *c, const char *address, const char *type) {
	nn_Exit e = nn_pushstring(c, "component_added");
	if(e) return e;
	e = nn_pushstring(c, address);
	if(e) return e;
	e = nn_pushstring(c, type);
	if(e) return e;
	return nn_pushSignal(c, 3);
}

static nn_Exit nn_pushComponentRemoved(nn_Computer *c, const char *address, const char *type) {
	nn_Exit e = nn_pushstring(c, "component_removed");
	if(e) return e;
	e = nn_pushstring(c, address);
	if(e) return e;
	e = nn_pushstring(c, type);
	if(e) return e;
	return nn_pushSignal(c, 3);
}

nn_Exit nn_mountComponent(nn_Computer *c, nn_Component *comp, int slot) {
	if(nn_getComponent(c, comp->address) != NULL) return NN_EBADSTATE;

	nn_ComponentEntry ent = {
		.address = comp->address,
		.comp = comp,
		.slot = slot,
		.budgetUsed = 0,
	};
	if(!nn_hashPut(&c->components, &ent)) return NN_ELIMIT;
	nn_retainComponent(comp);
	if(c->state == NN_RUNNING) {
		return nn_pushComponentAdded(c, comp->address, comp->type);
	}
	return NN_OK;
}

nn_Exit nn_unmountComponent(nn_Computer *c, const char *address) {
	nn_Component *comp = nn_getComponent(c, address);
	if(comp == NULL) return NN_OK;
	nn_ComponentEntry lookingFor = {.address = address};
	nn_hashRemove(&c->components, &lookingFor);

	nn_Exit e = NN_OK;
	if(c->state == NN_RUNNING) {
		e = nn_pushComponentRemoved(c, address, comp->type);
	}
	nn_dropComponent(comp);
	return e;
}

static nn_ComponentEntry *nn_getComponentEntry(nn_Computer *c, const char *address) {
	nn_ComponentEntry ent = {
		.address = address,
	};
	return nn_hashGet(&c->components, &ent);
}

nn_Component *nn_getComponent(nn_Computer *c, const char *address) {
	nn_ComponentEntry *ent = nn_getComponentEntry(c, address);
	if(ent == NULL) return NULL;
	return ent->comp;
}

int nn_getComponentSlot(nn_Computer *c, const char *address) {
	nn_ComponentEntry *ent = nn_getComponentEntry(c, address);
	if(ent == NULL) return -1;
	return ent->slot;
}

size_t nn_countComponents(nn_Computer *c) {
	size_t len = 0;
	for(nn_ComponentEntry *ent = nn_hashIterate(&c->components, NULL); ent != NULL; ent = nn_hashIterate(&c->components, ent)) len++;
	return len;
}

void nn_getComponents(nn_Computer *c, const char **components) {
	size_t i = 0;
	for(nn_ComponentEntry *ent = nn_hashIterate(&c->components, NULL); ent != NULL; ent = nn_hashIterate(&c->components, ent)) {
		components[i] = ent->address;
		i++;
	}
}

nn_Exit nn_invokeComponent(nn_Computer *computer, const char *compAddress, const char *method) {
	nn_Component *c = nn_getComponent(computer, compAddress);
	if(c == NULL) {
		nn_setError(computer, "no such component");
		return NN_EBADCALL;
	}
	if(!nn_hasComponentMethod(c, method)) {
		nn_setError(computer, "no such method");
		return NN_EBADCALL;
	}
	nn_MethodEntry *m = nn_getComponentMethodEntry(c, method);

	while(nn_getstacksize(computer) > 0) {
		if(!nn_isnull(computer, nn_getstacksize(computer) - 1)) break;
		nn_pop(computer);
	}

	nn_ComponentRequest req;
	req.ctx = &c->universe->ctx;
	req.computer = computer;
	req.state = c->state;
	req.classState = c->classState;
	req.action = NN_COMP_INVOKE;
	req.methodIdx = m->idx;
	req.returnCount = 0;
	nn_Exit e = c->handler(&req);
	if(e) {
		if(e != NN_EBADCALL) nn_setErrorFromExit(computer, e);
		nn_clearstack(computer);
		return e;
	}

	size_t endOfTrim = computer->stackSize - req.returnCount;
	for(size_t i = 0; i < endOfTrim; i++) {
		nn_dropValue(computer->callstack[i]);
	}
	for(size_t i = endOfTrim; i < computer->stackSize; i++) {
		computer->callstack[i - endOfTrim] = computer->callstack[i];
	}
	computer->stackSize = req.returnCount;

	return NN_OK;
}

nn_Exit nn_signalComponent(nn_Component *component, nn_Computer *computer, const char *signal) {
	nn_ComponentRequest req;
	req.ctx = &component->universe->ctx;
	req.computer = computer;
	req.state = component->state;
	req.classState = component->classState;
	req.action = NN_COMP_SIGNAL;
	req.signal = signal;
	return component->handler(&req);
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

// TODO: call

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
	for(nn_ComponentEntry *c = nn_hashIterate(&computer->components, NULL); c != NULL; c = nn_hashIterate(&computer->components, c)) {
		if(c->budgetUsed >= NN_COMPONENT_CALLBUDGET) return true;
	}
	if(computer->totalCallBudget == 0) return false;
	return computer->callBudget == 0;
}

void nn_resetComponentBudgets(nn_Computer *computer) {
	for(nn_ComponentEntry *c = nn_hashIterate(&computer->components, NULL); c != NULL; c = nn_hashIterate(&computer->components, c)) {
		c->budgetUsed = 0;
	}
	computer->callBudget = computer->totalCallBudget;
}
bool nn_costComponent(nn_Computer *computer, const char *address, double perTick) {
	return nn_costComponentN(computer, address, 1, perTick);
}

bool nn_costComponentN(nn_Computer *computer, const char *address, double amount, double perTick) {
	// this means 0 per tick means free
	if(perTick == 0) return false;
	nn_ComponentEntry *c = nn_getInternalComponent(computer, address);
	if(c == NULL) return false;
	c->budgetUsed += (NN_COMPONENT_CALLBUDGET * amount) / perTick;
	return c->budgetUsed >= NN_COMPONENT_CALLBUDGET;
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

const nn_EEPROM nn_defaultEEPROMs[4] = {
	(nn_EEPROM) {
		.size = 4 * NN_KiB,
		.dataSize = 256,
		.readEnergyCost = 1,
		.writeEnergyCost = 100,
		.readDataEnergyCost = 0.1,
		.writeDataEnergyCost = 5,
		.writeDelay = 2,
		.writeDataDelay = 1,
	},
	(nn_EEPROM) {
		.size = 8 * NN_KiB,
		.dataSize = 1 * NN_KiB,
		.readEnergyCost = 2,
		.writeEnergyCost = 200,
		.readDataEnergyCost = 0.2,
		.writeDataEnergyCost = 10,
		.writeDelay = 2,
		.writeDataDelay = 1,
	},
	(nn_EEPROM) {
		.size = 16 * NN_KiB,
		.dataSize = 2 * NN_KiB,
		.readEnergyCost = 4,
		.writeEnergyCost = 400,
		.readDataEnergyCost = 0.4,
		.writeDataEnergyCost = 20,
		.writeDelay = 1,
		.writeDataDelay = 0.5,
	},
	(nn_EEPROM) {
		.size = 32 * NN_KiB,
		.dataSize = 4 * NN_KiB,
		.readEnergyCost = 8,
		.writeEnergyCost = 800,
		.readDataEnergyCost = 0.8,
		.writeDataEnergyCost = 40,
		.writeDelay = 1,
		.writeDataDelay = 0.5,
	},
};

const nn_Filesystem nn_defaultFilesystems[4] = {
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


const nn_Filesystem nn_defaultFloppy = (nn_Filesystem) {
	.spaceTotal = 512 * NN_KiB,
	.readsPerTick = 1,
	.writesPerTick = 1,
	.dataEnergyCost = 8.0 / NN_MiB,
};

const nn_Filesystem nn_defaultTmpFS = (nn_Filesystem) {
	.spaceTotal = 64 * NN_KiB,
	.readsPerTick = 1024,
	.writesPerTick = 512,
	.dataEnergyCost = 512.0 / NN_MiB,
};

const nn_Drive nn_defaultDrives[4] = {
	(nn_Drive) {
		.capacity = 1 * NN_MiB,
		.sectorSize = 512,
		.platterCount = 2,
		.readsPerTick = 10,
		.writesPerTick = 5,
		.rpm = 1800,
		.onlySpinForwards = false,
		.dataEnergyCost = 256.0 / NN_MiB,
	},
	(nn_Drive) {
		.capacity = 2 * NN_MiB,
		.sectorSize = 512,
		.platterCount = 4,
		.readsPerTick = 20,
		.writesPerTick = 10,
		.rpm = 1800,
		.onlySpinForwards = false,
		.dataEnergyCost = 512.0 / NN_MiB,
	},
	(nn_Drive) {
		.capacity = 4 * NN_MiB,
		.sectorSize = 512,
		.platterCount = 8,
		.readsPerTick = 30,
		.writesPerTick = 15,
		.rpm = 1800,
		.onlySpinForwards = false,
		.dataEnergyCost = 1024.0 / NN_MiB,
	},
	(nn_Drive) {
		.capacity = 8 * NN_MiB,
		.sectorSize = 512,
		.platterCount = 16,
		.readsPerTick = 40,
		.writesPerTick = 20,
		.rpm = 1800,
		.onlySpinForwards = false,
		.dataEnergyCost = 2048.0 / NN_MiB,
	},
};

const nn_Drive nn_floppyDrive = {
	.capacity = 512 * NN_KiB,
	.sectorSize = 512,
	.platterCount = 1,
	.readsPerTick = 5,
	.writesPerTick = 2,
	.rpm = 1800,
	.onlySpinForwards = true,
	.dataEnergyCost = 128.0 / NN_MiB,
};


const nn_ScreenConfig nn_defaultScreens[4] = {
	(nn_ScreenConfig) {
		.maxWidth = 50,
		.maxHeight = 16,
		.maxDepth = 1,
		.defaultPalette = NULL,
		.paletteColors = 0,
		.editableColors = 0,
		.features = NN_SCRF_NONE,
	},
	(nn_ScreenConfig) {
		.maxWidth = 80,
		.maxHeight = 25,
		.maxDepth = 4,
		.defaultPalette = nn_ocpalette4,
		.paletteColors = 16,
		.editableColors = 0,
		.features = NN_SCRF_MOUSE | NN_SCRF_TOUCHINVERTED,
	},
	(nn_ScreenConfig) {
		.maxWidth = 160,
		.maxHeight = 50,
		.maxDepth = 8,
		.defaultPalette = nn_ocpalette8,
		.paletteColors = 256,
		.editableColors = 16,
		.features = NN_SCRF_MOUSE | NN_SCRF_TOUCHINVERTED | NN_SCRF_PRECISE | NN_SCRF_EDITABLECOLORS,
	},
	(nn_ScreenConfig) {
		.maxWidth = 240,
		.maxHeight = 80,
		.maxDepth = 16,
		.defaultPalette = nn_ocpalette8,
		.paletteColors = 256,
		.editableColors = 256,
		.features = NN_SCRF_NONE | NN_SCRF_EDITABLECOLORS,
	},
};

const nn_GPU nn_defaultGPUs[4] = {
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
		.energyPerWrite = 0.0002,
		.energyPerClear = 0.0001,
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
		.energyPerWrite = 0.001,
		.energyPerClear = 0.0005,
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
		.energyPerWrite = 0.002,
		.energyPerClear = 0.001,
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
		.energyPerWrite = 0.0025,
		.energyPerClear = 0.0012,
	},
};

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

static void nn_splitColor(int color, double *r, double *g, double *b) {
	int _r = (color >> 16) & 0xFF;
	int _g = (color >> 8) & 0xFF;
	int _b = (color >> 0) & 0xFF;

	*r = (double)_r / 255;
	*g = (double)_g / 255;
	*b = (double)_b / 255;
}

static double nn_colorLuminance(int color) {
	double r, g, b;
	nn_splitColor(color, &r, &g, &b);
	// taken from https://stackoverflow.com/questions/687261/converting-rgb-to-grayscale-intensity
	return r * 0.2126 + g * 0.7152 + b * 0.0722;
}

static double nn_colorDistance(int a, int b) {
	double n = nn_colorLuminance(a) - nn_colorLuminance(b);
	if(n < 0) n = -n;
	return n;
}

int nn_mapColor(int color, int *palette, size_t len) {
	int bestColor = color;
	// maximum distance, the one between white and black, is ~1.0 so this is way higher
	double bestDist = 100000;
	for(size_t i = 0; i < len; i++) {
		int entry = palette[i];
		double dist = nn_colorDistance(color, entry);
		if(dist < bestDist) {
			bestDist = dist;
			bestColor = entry;
		}
	}
	return bestColor;
}

int nn_mapDepth(int color, int depth) {
	if(depth == 1) return color == 0 ? 0 : 0xFFFFFF;
	// TODO: map the other depths
	if(depth == 4) return nn_mapColor(color, nn_ocpalette4, 16);
	if(depth == 8) return nn_mapColor(color, nn_ocpalette8, 256);
	if(depth == 16) return color & 0xF0FFF0;
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
	nn_Exit err = nn_pushstring(computer, "key_up");
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

typedef enum nn_EENum {
	NN_EENUM_GETSIZE,
	NN_EENUM_GETDATASIZE,
	NN_EENUM_GET,
	NN_EENUM_GETDATA,
	NN_EENUM_GETLABEL,
	NN_EENUM_GETARCH,
	NN_EENUM_SET,
	NN_EENUM_SETDATA,
	NN_EENUM_SETLABEL,
	NN_EENUM_SETARCH,
	
	NN_EENUM_COUNT,
} nn_EENum;

typedef struct nn_EEState {
	nn_Context *ctx;
	nn_EEPROM eeprom;
	nn_EEPROMHandler *handler;
} nn_EEState;

static nn_Exit nn_eepromHandler(nn_ComponentRequest *req) {
	if(req->action == NN_COMP_SIGNAL) return NN_OK;
	if(req->action == NN_COMP_CHECKMETHOD) return NN_OK;
	nn_EEState *state = req->classState;
	nn_EEPROMRequest ereq;
	ereq.ctx = req->ctx;
	ereq.computer = req->computer;
	ereq.state = req->state;
	ereq.eeprom = &state->eeprom;
	nn_EEPROM eeprom = state->eeprom;
	if(req->action == NN_COMP_DROP) {
		ereq.action = NN_EEPROM_DROP;
		state->handler(&ereq);
		nn_free(req->ctx, state, sizeof(*state));
		return NN_OK;
	}
	nn_Computer *C = req->computer;
	nn_EENum method = req->methodIdx;
	nn_Exit e = NN_OK;
	if(method == NN_EENUM_GETSIZE) {
		req->returnCount = 1;
		return nn_pushinteger(C, eeprom.size);
	}
	if(method == NN_EENUM_GETDATASIZE) {
		req->returnCount = 1;
		return nn_pushinteger(C, eeprom.dataSize);
	}
	if(method == NN_EENUM_GET) {
		ereq.action = NN_EEPROM_GET;
		char buf[eeprom.size];
		ereq.buf = buf;
		ereq.buflen = eeprom.size;
		e = state->handler(&ereq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushlstring(C, ereq.buf, ereq.buflen);
	}
	if(method == NN_EENUM_GETDATA) {
		ereq.action = NN_EEPROM_GETDATA;
		char buf[eeprom.size];
		ereq.buf = buf;
		ereq.buflen = eeprom.size;
		e = state->handler(&ereq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushlstring(C, ereq.buf, ereq.buflen);
	}
	if(method == NN_EENUM_GETLABEL) {
		ereq.action = NN_EEPROM_GETLABEL;
		char buf[NN_MAX_LABEL];
		ereq.buf = buf;
		ereq.buflen = NN_MAX_LABEL;
		e = state->handler(&ereq);
		if(e) return e;
		req->returnCount = 1;
		if(ereq.buflen == 0) return nn_pushnull(C);
		return nn_pushlstring(C, ereq.buf, ereq.buflen);
	}
	if(method == NN_EENUM_GETARCH) {
		ereq.action = NN_EEPROM_GETARCH;
		char buf[NN_MAX_ARCHNAME];
		ereq.buf = buf;
		ereq.buflen = NN_MAX_ARCHNAME;
		e = state->handler(&ereq);
		if(e) return e;
		req->returnCount = 1;
		if(ereq.buflen == 0) return nn_pushnull(C);
		return nn_pushlstring(C, ereq.buf, ereq.buflen);
	}
	if(method == NN_EENUM_SET) {
		if(nn_checkstring(C, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
		ereq.action = NN_EEPROM_SET;
		ereq.robuf = nn_tolstring(C, 0, &ereq.buflen);
		if(ereq.buflen > eeprom.size) {
			nn_setError(C, "not enough space");
			return NN_EBADCALL;
		}
		e = state->handler(&ereq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushbool(C, true);
	}
	if(method == NN_EENUM_SETDATA) {
		if(nn_checkstring(C, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
		ereq.action = NN_EEPROM_SETDATA;
		ereq.robuf = nn_tolstring(C, 0, &ereq.buflen);
		if(ereq.buflen > eeprom.dataSize) {
			nn_setError(C, "not enough space");
			return NN_EBADCALL;
		}
		e = state->handler(&ereq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushbool(C, true);
	}
	if(method == NN_EENUM_SETLABEL) {
		e = nn_defaultstring(C, 0, "");
		if(e) return e;
		if(nn_checkstring(C, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
		ereq.action = NN_EEPROM_SETLABEL;
		ereq.robuf = nn_tolstring(C, 0, &ereq.buflen);
		e = state->handler(&ereq);
		if(e) return e;
		req->returnCount = 1;
		if(ereq.buflen == 0) return nn_pushnull(C);
		return nn_pushlstring(C, ereq.robuf, ereq.buflen);
	}
	if(method == NN_EENUM_SETARCH) {
		if(nn_checkstring(C, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
		ereq.action = NN_EEPROM_SETARCH;
		ereq.robuf = nn_tolstring(C, 0, &ereq.buflen);
		e = state->handler(&ereq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushbool(C, true);
	}
	nn_setError(C, "not implemented yet");
	return NN_EBADCALL;
}

nn_Component *nn_createEEPROM(nn_Universe *universe, const char *address, const nn_EEPROM *eeprom, void *state, nn_EEPROMHandler *handler) {
	nn_Component *c = nn_createComponent(universe, address, "eeprom");
	if(c == NULL) return NULL;
	const nn_Method methods[NN_EENUM_COUNT] = {
		[NN_EENUM_GETSIZE] = {"getSize", "function(): integer - Get maximum code size", NN_DIRECT},
		[NN_EENUM_GETDATASIZE] = {"getDataSize", "function(): integer - Get maximum data size", NN_DIRECT},
		[NN_EENUM_GET] = {"get", "function(): string - Get the code stored on the eeprom", NN_DIRECT},
		[NN_EENUM_GETDATA] = {"getData", "function(): string - Get the data stored on the eeprom", NN_DIRECT},
		[NN_EENUM_GETLABEL] = {"getLabel", "function(): string? - Get the label stored on the eeprom, if any", NN_DIRECT},
		[NN_EENUM_GETARCH] = {"getArchitecture", "function(): string? - Get the desired architecture stored on the eeprom, if any", NN_DIRECT},
		[NN_EENUM_SET] = {"set", "function(code: string) - Set the code on the EEPROM", NN_INDIRECT},
		[NN_EENUM_SETDATA] = {"setData", "function(data: string) - Set the data on the EEPROM", NN_INDIRECT},
		[NN_EENUM_SETLABEL] = {"setLabel", "function(label?: string) - Set the label", NN_INDIRECT},
		[NN_EENUM_SETARCH] = {"setArchitecture", "function(arch?: string) - Set the desired architecture", NN_INDIRECT},
	};
	nn_Exit e = nn_setComponentMethodsArray(c, methods, NN_EENUM_COUNT);
	if(e) {
		nn_dropComponent(c);
		return NULL;
	}
	nn_Context *ctx = &universe->ctx;
	nn_EEState *eestate = nn_alloc(ctx, sizeof(*eestate));
	if(eestate == NULL) {
		nn_dropComponent(c);
		return NULL;
	}
	eestate->ctx = ctx;
	eestate->eeprom = *eeprom;
	eestate->handler = handler;
	nn_setComponentState(c, state);
	nn_setComponentClassState(c, eestate);
	nn_setComponentHandler(c, nn_eepromHandler);
	return c;
}

typedef struct nn_VEEState {
	char *code;
	size_t codelen;
	char *data;
	size_t datalen;
	char label[NN_MAX_LABEL];
	size_t labellen;
	char arch[NN_MAX_ARCHNAME];
	size_t archlen;
} nn_VEEState;

static nn_Exit nn_veepromHandler(nn_EEPROMRequest *request) {
	nn_VEEState *state = request->state;
	nn_Computer *C = request->computer;
	const nn_EEPROM *eeprom = request->eeprom;
	nn_Context *ctx = request->ctx;
	if(request->action == NN_EEPROM_DROP) {
		nn_free(ctx, state->code, eeprom->size);
		nn_free(ctx, state->data, eeprom->dataSize);
		nn_free(ctx, state, sizeof(*state));
		return NN_OK;
	}
	if(request->action == NN_EEPROM_GET) {
		nn_memcpy(request->buf, state->code, state->codelen);
		request->buflen = state->codelen;
		return NN_OK;
	}
	if(request->action == NN_EEPROM_GETDATA) {
		nn_memcpy(request->buf, state->data, state->datalen);
		request->buflen = state->datalen;
		return NN_OK;
	}
	if(request->action == NN_EEPROM_GETLABEL) {
		nn_memcpy(request->buf, state->label, state->labellen);
		request->buflen = state->labellen;
		return NN_OK;
	}
	if(request->action == NN_EEPROM_GETARCH) {
		nn_memcpy(request->buf, state->arch, state->archlen);
		request->buflen = state->archlen;
		return NN_OK;
	}
	if(request->action == NN_EEPROM_SET) {
		state->codelen = request->buflen;
		nn_memcpy(state->code, request->robuf, state->codelen);
		return NN_OK;
	}
	if(request->action == NN_EEPROM_SETDATA) {
		state->datalen = request->buflen;
		nn_memcpy(state->data, request->robuf, state->datalen);
		return NN_OK;
	}
	if(request->action == NN_EEPROM_SETLABEL) {
		if(request->buflen > NN_MAX_LABEL) request->buflen = NN_MAX_LABEL;
		state->labellen = request->buflen;
		nn_memcpy(state->label, request->robuf, state->labellen);
		return NN_OK;
	}
	if(request->action == NN_EEPROM_SETARCH) {
		if(request->buflen > NN_MAX_ARCHNAME) request->buflen = NN_MAX_ARCHNAME;
		state->archlen = request->buflen;
		nn_memcpy(state->arch, request->robuf, state->archlen);
		return NN_OK;
	}
	nn_setError(C, "veeprom: not implemented yet");
	return NN_EBADCALL;
}

nn_Component *nn_createVEEPROM(nn_Universe *universe, const char *address, const nn_VEEPROM *veeprom, const nn_EEPROM *eeprom) {
	nn_Context *ctx = &universe->ctx;
	char *code = NULL;
	char *data = NULL;
	nn_VEEState *state = NULL;

	code = nn_alloc(ctx, eeprom->size);
	if(code == NULL) goto fail;
	data = nn_alloc(ctx, eeprom->dataSize);
	if(data == NULL) goto fail;
	state = nn_alloc(ctx, sizeof(*state));
	if(state == NULL) goto fail;

	state->code = code;
	nn_memcpy(code, veeprom->code, veeprom->codelen);
	state->codelen = veeprom->codelen;
	state->data = data;
	nn_memcpy(data, veeprom->data, veeprom->datalen);
	state->datalen = veeprom->datalen;
	nn_memcpy(state->label, veeprom->label, veeprom->labellen);
	state->labellen = veeprom->labellen;
	if(veeprom->arch == NULL) {
		state->archlen = 0;
	} else {
		state->archlen = nn_strlen(veeprom->arch);
	}
	nn_memcpy(state->arch, veeprom->arch, state->archlen);

	nn_Component *c = nn_createEEPROM(universe, address, eeprom, state, nn_veepromHandler);
	if(c == NULL) goto fail;

	return c;

fail:
	nn_free(ctx, code, eeprom->size);
	nn_free(ctx, data, eeprom->dataSize);
	nn_free(ctx, state, sizeof(*state));
	return NULL;
}

typedef enum nn_FSNum {
	// drive stuff
	NN_FSNUM_SPACETOTAL,
	NN_FSNUM_SPACEUSED,
	NN_FSNUM_GETLABEL,
	NN_FSNUM_SETLABEL,
	NN_FSNUM_ISRO,

	// file I/O
	NN_FSNUM_OPEN,
	NN_FSNUM_READ,
	NN_FSNUM_WRITE,
	NN_FSNUM_SEEK,
	NN_FSNUM_CLOSE,

	// metadata
	NN_FSNUM_LIST,
	NN_FSNUM_EXISTS,
	NN_FSNUM_ISDIR,
	NN_FSNUM_SIZE,
	NN_FSNUM_LASTMODIFIED,

	// exotic
	NN_FSNUM_MKDIR,
	NN_FSNUM_REMOVE,
	NN_FSNUM_RENAME,

	NN_FSNUM_COUNT,
} nn_FSNum;

typedef struct nn_FSState {
	nn_Context *ctx;
	nn_Filesystem fs;
	nn_FSHandler *handler;
} nn_FSState;

static nn_Exit nn_fsPathCheck(nn_Computer *C, char buf[NN_MAX_PATH], const char *path) {
	size_t l = nn_strlen(path);
	if(l >= NN_MAX_PATH) {
		nn_setError(C, "path too long");
		return NN_EBADCALL;
	}
	nn_simplifyPath(path, buf);
	return NN_OK;
}

static nn_Exit nn_fsHandler(nn_ComponentRequest *req) {
	if(req->action == NN_COMP_SIGNAL) return NN_OK;
	if(req->action == NN_COMP_CHECKMETHOD) return NN_OK;
	nn_FSState *state = req->classState;
	nn_FSRequest freq;
	freq.ctx = req->ctx;
	freq.computer = req->computer;
	freq.state = req->state;
	freq.fs = &state->fs;
	if(req->action == NN_COMP_DROP) {
		freq.action = NN_FS_DROP;
		state->handler(&freq);
		nn_free(req->ctx, state, sizeof(*state));
		return NN_OK;
	}
	nn_Computer *C = req->computer;
	nn_FSNum method = req->methodIdx;
	nn_Exit e = NN_OK;
	if(method == NN_FSNUM_SPACETOTAL) {
		req->returnCount = 1;
		return nn_pushinteger(C, state->fs.spaceTotal);
	}
	if(method == NN_FSNUM_SPACEUSED) {
		freq.action = NN_FS_SPACEUSED;
		freq.spaceUsed = 0;
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushinteger(C, freq.spaceUsed);
	}
	if(method == NN_FSNUM_GETLABEL) {
		char buf[NN_MAX_LABEL];
		freq.action = NN_FS_GETLABEL;
		freq.getlabel.buf = buf;
		freq.getlabel.len = NN_MAX_LABEL;
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		if(freq.getlabel.len == 0) return nn_pushnull(C);
		return nn_pushlstring(C, freq.getlabel.buf, freq.getlabel.len);
	}
	if(method == NN_FSNUM_SETLABEL) {
		e = nn_defaultstring(C, 0, "");
		if(e) return e;
		if(nn_checkstring(C, 0, "bad argument #1 (label expected)")) return NN_EBADCALL;
		freq.action = NN_FS_SETLABEL;
		freq.setlabel.buf = nn_tolstring(C, 0, &freq.setlabel.len);
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		if(freq.setlabel.len == 0) return nn_pushnull(C);
		return nn_pushlstring(C, freq.setlabel.buf, freq.setlabel.len);
	}
	if(method == NN_FSNUM_ISRO) {
		freq.action = NN_FS_ISRO;
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushbool(C, freq.isReadonly);
	}
	if(method == NN_FSNUM_OPEN) {
		if(nn_checkstring(C, 0, "bad argument #1 (path expected)")) return NN_EBADCALL;
		e = nn_defaultstring(C, 1, "r");
		if(e) return e;
		if(nn_checkstring(C, 1, "bad argument #2 (mode expected)")) return NN_EBADCALL;
		char truepath[NN_MAX_PATH];
		e = nn_fsPathCheck(C, truepath, nn_tostring(C, 0));
		if(e) return e;
		freq.action = NN_FS_OPEN;
		freq.open.path = truepath;
		freq.open.mode = nn_tostring(C, 1);
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushinteger(C, freq.fd);
	}
	if(method == NN_FSNUM_READ) {
		if(nn_checkinteger(C, 0, "bad argument #1 (fd expected)")) return NN_EBADCALL;
		e = nn_defaultinteger(C, 1, NN_MAX_READ);
		if(e) return e;
		if(nn_checknumber(C, 1, "bad argument #2 (number expected)")) return NN_EBADCALL;
		double requested = nn_tonumber(C, 1);
		if(requested > NN_MAX_READ) requested = NN_MAX_READ;
		freq.action = NN_FS_READ;
		freq.fd = nn_tointeger(C, 0);
		char buf[NN_MAX_READ];
		freq.read.buf = buf;
		freq.read.len = requested;
		e = state->handler(&freq);
		if(e) return e;
		if(freq.read.buf == NULL) return NN_OK;
		req->returnCount = 1;
		return nn_pushlstring(C, buf, freq.read.len);
	}
	if(method == NN_FSNUM_WRITE) {
		if(nn_checkinteger(C, 0, "bad argument #1 (fd expected)")) return NN_EBADCALL;
		if(nn_checkstring(C, 1, "bad argument #2 (string expected)")) return NN_EBADCALL;
		freq.action = NN_FS_WRITE;
		freq.fd = nn_tointeger(C, 0);
		freq.write.buf = nn_tolstring(C, 1, &freq.write.len);
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushbool(C, true);
	}
	if(method == NN_FSNUM_SEEK) {
		if(nn_checkinteger(C, 0, "bad argument #1 (fd expected)")) return NN_EBADCALL;
		e = nn_defaultstring(C, 1, "cur");
		if(e) return e;
		if(nn_checkinteger(C, 1, "bad argument #2 (whence expected)")) return NN_EBADCALL;
		e = nn_defaultinteger(C, 2, 0);
		if(e) return e;
		if(nn_checkinteger(C, 2, "bad argument #3 (integer expected)")) return NN_EBADCALL;
		const char *whence = nn_tostring(C, 1);
		nn_FSWhence seek = NN_SEEK_SET;
		if(nn_strcmp(whence, "set") == 0) {
			seek = NN_SEEK_SET;
		}
		if(nn_strcmp(whence, "cur") == 0) {
			seek = NN_SEEK_CUR;
		}
		if(nn_strcmp(whence, "end") == 0) {
			seek = NN_SEEK_END;
		}
		freq.action = NN_FS_CLOSE;
		freq.fd = nn_tointeger(C, 0);
		freq.seek.whence = seek;
		freq.seek.off = nn_tointeger(C, 2);
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushbool(C, true);
	}
	if(method == NN_FSNUM_CLOSE) {
		if(nn_checkinteger(C, 0, "bad argument #1 (fd expected)")) return NN_EBADCALL;
		freq.action = NN_FS_CLOSE;
		freq.fd = nn_tointeger(C, 0);
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushbool(C, true);
	}
	if(method == NN_FSNUM_LIST) {
		if(nn_checkstring(C, 0, "bad argument #1 (path expected)")) return NN_EBADCALL;
		char truepath[NN_MAX_PATH];
		e = nn_fsPathCheck(C, truepath, nn_tostring(C, 0));
		if(e) return e;
		freq.action = NN_FS_OPENDIR;
		freq.opendir = truepath;
		e = state->handler(&freq);
		if(e) return e;
		int dirfd = freq.fd;
		size_t entCount = 0;
		while(true) {
			char name[NN_MAX_PATH];
			freq.action = NN_FS_READDIR;
			freq.fd = dirfd;
			freq.readdir.dirpath = truepath;
			freq.readdir.buf = name;
			freq.readdir.len = NN_MAX_PATH;
			e = state->handler(&freq);
			if(e) goto done;
			if(freq.readdir.buf == NULL) break;
			if(nn_isLiterallyJust(freq.readdir.buf, freq.readdir.len, '.')) continue;
			e = nn_pushlstring(C, freq.readdir.buf, freq.readdir.len);
			if(e) goto done;
			entCount++;
		}
done:;
		freq.action = NN_FS_CLOSEDIR;
		freq.fd = dirfd;
		state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pusharraytable(C, entCount);
	}
	if(method == NN_FSNUM_EXISTS) {
		if(nn_checkstring(C, 0, "bad argument #1 (path expected)")) return NN_EBADCALL;
		char truepath[NN_MAX_PATH];
		e = nn_fsPathCheck(C, truepath, nn_tostring(C, 0));
		if(e) return e;
		freq.action = NN_FS_STAT;
		freq.stat.path = truepath;
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushbool(C, freq.stat.path != NULL);
	}
	if(method == NN_FSNUM_ISDIR) {
		if(nn_checkstring(C, 0, "bad argument #1 (path expected)")) return NN_EBADCALL;
		char truepath[NN_MAX_PATH];
		e = nn_fsPathCheck(C, truepath, nn_tostring(C, 0));
		if(e) return e;
		freq.action = NN_FS_STAT;
		freq.stat.path = truepath;
		e = state->handler(&freq);
		if(e) return e;
		if(freq.stat.path == NULL) {
			nn_setError(C, "no such file or directory");
			return NN_EBADCALL;
		}
		req->returnCount = 1;
		return nn_pushbool(C, freq.stat.isDirectory);
	}
	if(method == NN_FSNUM_SIZE) {
		if(nn_checkstring(C, 0, "bad argument #1 (path expected)")) return NN_EBADCALL;
		char truepath[NN_MAX_PATH];
		e = nn_fsPathCheck(C, truepath, nn_tostring(C, 0));
		if(e) return e;
		freq.action = NN_FS_STAT;
		freq.stat.path = truepath;
		e = state->handler(&freq);
		if(e) return e;
		if(freq.stat.path == NULL) {
			nn_setError(C, "no such file or directory");
			return NN_EBADCALL;
		}
		req->returnCount = 1;
		return nn_pushinteger(C, freq.stat.size);
	}
	if(method == NN_FSNUM_LASTMODIFIED) {
		if(nn_checkstring(C, 0, "bad argument #1 (path expected)")) return NN_EBADCALL;
		char truepath[NN_MAX_PATH];
		e = nn_fsPathCheck(C, truepath, nn_tostring(C, 0));
		if(e) return e;
		freq.action = NN_FS_STAT;
		freq.stat.path = truepath;
		e = state->handler(&freq);
		if(e) return e;
		if(freq.stat.path == NULL) {
			nn_setError(C, "no such file or directory");
			return NN_EBADCALL;
		}
		req->returnCount = 1;
		return nn_pushinteger(C, freq.stat.lastModified * 1000);
	}
	if(method == NN_FSNUM_MKDIR) {
		if(nn_checkstring(C, 0, "bad argument #1 (path expected)")) return NN_EBADCALL;
		char truepath[NN_MAX_PATH];
		e = nn_fsPathCheck(C, truepath, nn_tostring(C, 0));
		if(e) return e;
		freq.action = NN_FS_MKDIR;
		freq.mkdir = truepath;
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushbool(C, true);
	}
	if(method == NN_FSNUM_REMOVE) {
		if(nn_checkstring(C, 0, "bad argument #1 (path expected)")) return NN_EBADCALL;
		char truepath[NN_MAX_PATH];
		e = nn_fsPathCheck(C, truepath, nn_tostring(C, 0));
		if(e) return e;
		freq.action = NN_FS_RENAME;
		freq.rename.from = truepath;
		freq.rename.to = NULL;
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushbool(C, true);
	}
	if(method == NN_FSNUM_RENAME) {
		if(nn_checkstring(C, 0, "bad argument #1 (path expected)")) return NN_EBADCALL;
		if(nn_checkstring(C, 1, "bad argument #2 (path expected)")) return NN_EBADCALL;
		char truefrom[NN_MAX_PATH];
		e = nn_fsPathCheck(C, truefrom, nn_tostring(C, 0));
		if(e) return e;
		char trueto[NN_MAX_PATH];
		e = nn_fsPathCheck(C, trueto, nn_tostring(C, 1));
		if(e) return e;
		freq.action = NN_FS_RENAME;
		freq.rename.from = truefrom;
		freq.rename.to = trueto;
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushbool(C, true);
	}
	nn_setError(C, "not implemented yet");
	return NN_EBADCALL;
}

nn_Component *nn_createFilesystem(nn_Universe *universe, const char *address, const nn_Filesystem *fs, void *state, nn_FSHandler *handler) {
	nn_Component *c = nn_createComponent(universe, address, "filesystem");
	if(c == NULL) return NULL;
	const nn_Method methods[NN_FSNUM_COUNT] = {
		[NN_FSNUM_SPACETOTAL] = {"spaceTotal", "function(): integer - Capacity of the drive", NN_DIRECT},
		[NN_FSNUM_SPACEUSED] = {"spaceUsed", "function(): integer - Amount of space used", NN_DIRECT},
		[NN_FSNUM_GETLABEL] = {"getLabel", "function(): string? - Gets the label of the drive, if any", NN_DIRECT},
		[NN_FSNUM_SETLABEL] = {"setLabel", "function(label?: string): string - Sets the label of the drive. Returns the new label, which may be truncated", NN_INDIRECT},
		[NN_FSNUM_ISRO] = {"isReadOnly", "function(): boolean - Returns whether the drive is read-only", NN_DIRECT},
		[NN_FSNUM_OPEN] = {"open", "function(path: string, mode?: 'r'|'w'|'a'): integer - Open a file", NN_DIRECT},
		[NN_FSNUM_READ] = {"read", "function(fd: integer, len?: integer): string? - Read from a file, returns nothing on EoF", NN_DIRECT},
		[NN_FSNUM_WRITE] = {"write", "function(fd: integer, data: string): boolean - Writes to a file, returns whether the operation succeeded", NN_DIRECT},
		[NN_FSNUM_SEEK] = {"seek", "function(fd: integer, whence?: 'set'|'cur'|'end', off?: integer): integer - Seeks a file, returns new position", NN_DIRECT},
		[NN_FSNUM_CLOSE] = {"close", "function(fd: integer): boolean - Close a file", NN_DIRECT},
		[NN_FSNUM_LIST] = {"list", "function(path: string): string[] - Returns the entries in a directory", NN_DIRECT},
		[NN_FSNUM_EXISTS] = {"exists", "function(path: string): boolean - Returns whether an entry exists", NN_DIRECT},
		[NN_FSNUM_ISDIR] = {"isDirectory", "function(path: string): boolean - Returns whether an entry is a directory", NN_DIRECT},
		[NN_FSNUM_SIZE] = {"size", "function(path: string): integer - Returns the size of an entry", NN_DIRECT},
		[NN_FSNUM_LASTMODIFIED] = {"lastModified", "function(path: string): integer - Returns the UNIX timestamp of the last modified time", NN_DIRECT},
		[NN_FSNUM_MKDIR] = {"makeDirectory", "function(path: string): boolean - Create a directory, recursively. Does not fail if directory already exists", NN_DIRECT},
		[NN_FSNUM_REMOVE] = {"remove", "function(path: string): boolean - Recursively deletes an entry", NN_INDIRECT},
		[NN_FSNUM_RENAME] = {"rename", "function(from: string, to: string): boolean - Renames/moves an entry", NN_INDIRECT},
	};
	nn_Exit e = nn_setComponentMethodsArray(c, methods, NN_FSNUM_COUNT);
	if(e) {
		nn_dropComponent(c);
		return NULL;
	}
	nn_Context *ctx = &universe->ctx;
	nn_FSState *fsstate = nn_alloc(ctx, sizeof(*fsstate));
	if(fsstate == NULL) {
		nn_dropComponent(c);
		return NULL;
	}
	fsstate->ctx = ctx;
	fsstate->fs = *fs;
	fsstate->handler = handler;
	nn_setComponentState(c, state);
	nn_setComponentClassState(c, fsstate);
	nn_setComponentHandler(c, nn_fsHandler);
	return c;
}

nn_Component *nn_createVFilesystem(nn_Universe *universe, const char *address, const nn_VFilesystem *vfs, const nn_Filesystem *fs);

static void nn_drive_seekPenalty(nn_Computer *C, size_t lastSector, size_t newSector, const nn_Drive *drive) {
	// Check if SSD
	if(drive->rpm == 0) return;

	size_t maxSectors = drive->capacity / drive->sectorSize;
	size_t sectorsPerPlatter = maxSectors / drive->platterCount;
	// RPM over the number of sectors, over 60 seconds.
	double latencyPerSector = 1.0 / ((double)drive->rpm / 60 * maxSectors);

	// magic
	lastSector %= sectorsPerPlatter;
	newSector %= sectorsPerPlatter;

	size_t sectorDelta;
	if(newSector >= lastSector) {
		sectorDelta = newSector - lastSector;
	} else if(drive->onlySpinForwards) {
		sectorDelta = sectorsPerPlatter - (lastSector - newSector);
	} else {
		sectorDelta = lastSector - newSector;
	}

	nn_addIdleTime(C, sectorDelta * latencyPerSector);
}

typedef enum nn_DrvNum {
	NN_DRVNUM_GETCAPACITY,
	NN_DRVNUM_GETSECTORSIZE,
	NN_DRVNUM_GETPLATTERCOUNT,
	NN_DRVNUM_GETLABEL,
	NN_DRVNUM_SETLABEL,
	NN_DRVNUM_READSECTOR,
	NN_DRVNUM_WRITESECTOR,
	NN_DRVNUM_READBYTE,
	NN_DRVNUM_READUBYTE,
	NN_DRVNUM_WRITEBYTE,

	NN_DRVNUM_COUNT,
} nn_DrvNum;

typedef struct nn_DrvState {
	nn_Context *ctx;
	nn_Drive drive;
	nn_DriveHandler *handler;
} nn_DrvState;

static nn_Exit nn_drvHandler(nn_ComponentRequest *request) {
	nn_Context *ctx = request->ctx;
	nn_Computer *C = request->computer;
	nn_DrvState *state = request->classState;

	nn_DriveRequest dreq;
	dreq.ctx = ctx;
	dreq.computer = C;
	dreq.state = request->state;

	if(request->action == NN_COMP_DROP) {
		dreq.action = NN_DRIVE_DROP;
		state->handler(&dreq);
		nn_free(ctx, state, sizeof(*state));
		return NN_OK;
	}
	if(C) nn_setError(C, "bad call");
	return NN_EBADCALL;
}

nn_Component *nn_createDrive(nn_Universe *universe, const char *address, const nn_Drive *drive, void *state, nn_DriveHandler *handler) {
	nn_Component *c = nn_createComponent(universe, address, "drive");
	if(c == NULL) return NULL;
	const nn_Method methods[NN_DRVNUM_COUNT] = {
		[NN_DRVNUM_GETCAPACITY] = {"getCapacity", "function(): integer - Get drive capacity", NN_DIRECT},
		[NN_DRVNUM_GETSECTORSIZE] = {"getSectorSize", "function(): integer - Get sector size", NN_DIRECT},
		[NN_DRVNUM_GETPLATTERCOUNT] = {"getPlatterCount", "function(): integer - Get number of platters on this drive", NN_DIRECT},
		[NN_DRVNUM_GETLABEL] = {"getLabel", "function(): string? - Get drive label", NN_DIRECT},
		[NN_DRVNUM_SETLABEL] = {"setLabel", "function(label: string?): string - Set drive label", NN_DIRECT},
	};
	nn_Exit e = nn_setComponentMethodsArray(c, methods, NN_DRVNUM_COUNT);
	if(e) {
		nn_dropComponent(c);
		return NULL;
	}
	nn_Context *ctx = &universe->ctx;
	nn_DrvState *drvstate = nn_alloc(ctx, sizeof(*drvstate));
	if(drvstate == NULL) {
		nn_dropComponent(c);
		return NULL;
	}
	drvstate->ctx = ctx;
	drvstate->drive = *drive;
	drvstate->handler = handler;
	nn_setComponentState(c, state);
	nn_setComponentClassState(c, drvstate);
	nn_setComponentHandler(c, nn_drvHandler);
	return c;
}

nn_Component *nn_createVDrive(nn_Universe *universe, const char *address, const nn_VDrive *vdrive, const nn_Drive *drive);

typedef struct nn_ScreenState {
	nn_Context *ctx;
	nn_ScreenConfig scrconf;
	nn_ScreenHandler *handler;
} nn_ScreenState;

static nn_Exit nn_screenHandler(nn_ComponentRequest *req) {
	if(req->action == NN_COMP_CHECKMETHOD) return NN_OK;
	if(req->action == NN_COMP_SIGNAL) return NN_OK;
	nn_Context *ctx = req->ctx;
	nn_ScreenState *state = req->classState;
	nn_Computer *C = req->computer;
	nn_ScreenRequest sreq;
	sreq.ctx = ctx;
	sreq.state = req->state;
	sreq.computer = C;
	sreq.screen = &state->scrconf;

	if(req->action == NN_COMP_DROP) {
		sreq.action = NN_SCREEN_DROP;
		state->handler(&sreq);
		nn_free(ctx, state, sizeof(*state));
		return NN_OK;
	}

	nn_setError(C, "screen: not yet implemented");
	return NN_EBADCALL;
}

nn_Component *nn_createScreen(nn_Universe *universe, const char *address, const nn_ScreenConfig *scrconf, void *state, nn_ScreenHandler *handler) {
	nn_Component *c = nn_createComponent(universe, address, "screen");
	if(c == NULL) return NULL;
	// TODO: methods
	nn_Context *ctx = &universe->ctx;
	nn_ScreenState *scrstate = nn_alloc(ctx, sizeof(*scrstate));
	if(scrstate == NULL) {
		nn_dropComponent(c);
		return NULL;
	}
	scrstate->ctx = ctx;
	scrstate->scrconf = *scrconf;
	scrstate->handler = handler;
	nn_setComponentState(c, state);
	nn_setComponentClassState(c, scrstate);
	nn_setComponentHandler(c, nn_screenHandler);
	return c;
}

typedef struct nn_GPUState {
	nn_Context *ctx;
	nn_GPU gpu;
	nn_GPUHandler *handler;
} nn_GPUState;

static nn_Exit nn_gpuHandler(nn_ComponentRequest *req) {
	if(req->action == NN_COMP_CHECKMETHOD) return NN_OK;
	if(req->action == NN_COMP_SIGNAL) return NN_OK;
	nn_Context *ctx = req->ctx;
	nn_GPUState *state = req->classState;
	nn_Computer *C = req->computer;
	nn_GPURequest greq;
	greq.ctx = ctx;
	greq.state = req->state;
	greq.computer = C;
	greq.gpu = &state->gpu;

	if(req->action == NN_COMP_DROP) {
		greq.action = NN_GPU_DROP;
		state->handler(&greq);
		nn_free(ctx, state, sizeof(*state));
		return NN_OK;
	}

	nn_setError(C, "gpu: not yet implemented");
	return NN_EBADCALL;
}

nn_Component *nn_createGPU(nn_Universe *universe, const char *address, const nn_GPU *gpu, void *state, nn_GPUHandler *handler) {
	nn_Component *c = nn_createComponent(universe, address, "gpu");
	if(c == NULL) return NULL;
	// TODO: methods
	nn_Context *ctx = &universe->ctx;
	nn_GPUState *gpustate = nn_alloc(ctx, sizeof(*gpustate));
	if(gpustate == NULL) {
		nn_dropComponent(c);
		return NULL;
	}
	gpustate->ctx = ctx;
	gpustate->gpu = *gpu;
	gpustate->handler = handler;
	nn_setComponentState(c, state);
	nn_setComponentClassState(c, gpustate);
	nn_setComponentHandler(c, nn_gpuHandler);
	return c;
}
