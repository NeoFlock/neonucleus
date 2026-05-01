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

void nn_incRef(nn_refc_t *refc, size_t n) {
	(*refc) += n;
}

bool nn_decRef(nn_refc_t *refc, size_t n) {
	(*refc) -= n;
	return (*refc) == 0;
}
#elif defined(NN_ATOMIC_MSVC)
// MSVC lacks C11 <stdatomic.h> in C mode, but has interlocked intrinsics.
// _InterlockedExchangeAdd operates on long (32-bit),
// _InterlockedExchangeAdd64 operates on __int64 (64-bit).
// We pick the right one based on pointer size since size_t matches that.
#include <intrin.h>

typedef volatile size_t nn_refc_t;

void nn_incRef(nn_refc_t *refc, size_t n) {
#if defined(_WIN64)
    _InterlockedExchangeAdd64((__int64 volatile *)refc, (__int64)n);
#else
    _InterlockedExchangeAdd((long volatile *)refc, (long)n);
#endif
}

bool nn_decRef(nn_refc_t *refc, size_t n) {
#if defined(_WIN64)
    __int64 old = _InterlockedExchangeAdd64((__int64 volatile *)refc, -(__int64)n);
    return (size_t)old == n;
#else
    long old = _InterlockedExchangeAdd((long volatile *)refc, -(long)n);
    return (size_t)old == n;
#endif
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
	// nn_realloc passed memory (which is NULL here) as first argument
	// to nn_alloc instead of ctx. nn_alloc dereferences it as a context
	// struct to call ctx->alloc(), so this is a NULL pointer dereference.
	// Confirmed by test_realloc crashing on nn_realloc(&ctx, NULL, 0, 64).
	// Original: if(memory == NULL) return nn_alloc(memory, newSize); if(memory == ctx->alloc) return nn_alloc(memory, newSize);
	if(memory == NULL) return nn_alloc(ctx, newSize);
	if(memory == ctx->alloc) return nn_alloc(ctx, newSize);
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

void nn_memreverse(void *dest, size_t len) {
	size_t mid = len/2;
	char *bytes = (char *)dest;
	for(size_t i = 0; i < mid; i++) {
		size_t j = len - i - 1;
		char tmp = bytes[i];
		bytes[i] = bytes[j];
		bytes[j] = tmp;
	}
}

bool nn_isLittleEndian() {
	union {char c; size_t x;} test;
	test.x = 1;
	return test.c == 1;
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
#ifdef NN_POSIX
    struct timespec s;
    if(clock_gettime(CLOCK_REALTIME, &s)) return 0;
    return s.tv_sec + (double)s.tv_nsec / 1000000000;
#elif defined(NN_WINDOWS)
    // Without this, nn_defaultTime returns 0 on Windows.
    // This breaks nn_getUptime(), which is (currentTime - creationTimestamp).
    // If currentTime is always 0, uptime is always negative or 0.
    // OpenOS relies on computer.uptime() for all timeouts:
    //   computer.pullSignal(timeout) compares computer.uptime() against a deadline.
    //   If uptime never advances, pullSignal(2) returns instantly instead of waiting.
    // To verify: in OpenOS, run `lua -e "print(computer.uptime())"`.
    //   Before fix: always prints 0 or a tiny constant.
    //   After fix: prints seconds since boot, increasing each call.
    // QueryPerformanceCounter is the highest-resolution monotonic clock on Windows.
    // It is available since Windows 2000 and cannot fail on Vista+.
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart
         / (double)freq.QuadPart;
#else
    return 0;
#endif
#else
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

#ifdef NN_WINDOWS
// rand_s() requires _CRT_RAND_S defined before <stdlib.h>.
// However, we can avoid that dependency by using the Win32
// API directly. RtlGenRandom (aka SystemFunction036) is
// available on all Windows versions since XP and does not
// require linking any extra library it lives in advapi32
// which is always implicitly linked.
// It fills a buffer with cryptographically strong random bytes.
// To verify: print nn_rand() in a loop on Windows.
BOOLEAN NTAPI SystemFunction036(
    PVOID RandomBuffer, ULONG RandomBufferLength);
#pragma comment(lib, "advapi32")

static size_t nn_windowsRng(void *_) {
    unsigned int v = 0;
    SystemFunction036(&v, sizeof(v));
    return v;
}
#endif

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
    // The original code used CreateMutex/WaitForSingleObject/ReleaseMutex.
    // Windows Mutexes are kernel objects: every lock/unlock is a syscall
    // into the NT kernel (NtWaitForSingleObject / NtReleaseMutant),
    // even when uncontended. They also support cross-process sharing
    // and abandonment detection, none of which NN needs.
    //
    // CRITICAL_SECTION is a user-mode construct that uses a spinlock
    // with a kernel fallback (only enters kernel on actual contention).
    // For uncontended locks (the common case), EnterCriticalSection
    // is just an interlocked compare-exchange so no syscall at all.
    switch(req->action) {
    case NN_LOCK_CREATE:;
        CRITICAL_SECTION *cs =
            malloc(sizeof(CRITICAL_SECTION));
        if(cs == NULL) { req->lock = NULL; return; }
        InitializeCriticalSection(cs);
        req->lock = cs;
        return;
    case NN_LOCK_DESTROY:;
        DeleteCriticalSection(req->lock);
        free(req->lock);
        return;
    case NN_LOCK_LOCK:;
        EnterCriticalSection(req->lock);
        return;
    case NN_LOCK_UNLOCK:;
        LeaveCriticalSection(req->lock);
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
    // RAND_MAX on MSVC is 32767 (15 bits), and nn_randomUUID() calls nn_rand() & 0xF per hex digit,
    // but with only 15 bits total per rand() call, the UUIDs have very low entropy. nn_randf() divides by
    // (rngMaximum+1), so 15 bits gives ~0.00003 granularity instead of ~0.0000000005 with 31 bits.
    // To verify: generate 1000 UUIDs, check for duplicates
    //   or patterns. With 15 bits of entropy per call and
    //   32 hex digits, collisions become plausible.
    // On Windows we use rand_s() which returns a full
    // 32-bit cryptographic random number without needing
    // srand(). On POSIX, rand()+srand() remains fine as
    // RAND_MAX is typically 2^31-1.
#ifdef NN_WINDOWS
    ctx->rngMaximum = UINT_MAX;
    ctx->rng = nn_windowsRng;
#else
    srand(time(NULL));
    ctx->rngMaximum = RAND_MAX;
    ctx->rng = nn_defaultRng;
#endif
#else
    ctx->rngMaximum = 1;
    ctx->rng = nn_defaultRng;
#endif
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

void nn_hashClear(nn_HashMap *map) {
	for(size_t i = 0; i < map->bufsize; i++) {
		void *ent = NN_PTROFF(map->buf, i, map->hash->entSize);
		if(map->hash->handler(NN_HASH_CMP, ent, NULL) == NN_HASH_DIFFERENT) {
			map->hash->handler(NN_HASH_REMOVE, ent, NULL);
		}
	}
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

static size_t nn_methodHash(nn_HashAction act, void *_slot, void *_ent) {
	nn_MethodEntry *slot = _slot;
	nn_MethodEntry *ent = _ent;
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

typedef struct nn_Universe {
	nn_Context ctx;
	void *userdata;
	// 0 for unbounded
	size_t memoryLimit;
	// 0 for unbounded
	size_t storageLimit;
} nn_Universe;

typedef struct nn_ComponentEntry {
	const char *address;
	nn_Component *comp;
	double budgetUsed;
	int slot;
} nn_ComponentEntry;

static size_t nn_componentHash(nn_HashAction act, void *_slot, void *_ent) {
	nn_ComponentEntry *slot = _slot;
	nn_ComponentEntry *ent = _ent;
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
	nn_Lock *lock;
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
	nn_Beep beep;
	nn_Value callstack[NN_MAX_STACK];
	char errorBuffer[NN_MAX_ERROR_SIZE];
	nn_Architecture archs[NN_MAX_ARCHITECTURES];
	nn_Signal signals[NN_MAX_SIGNALS];
	char *users[NN_MAX_USERS];
} nn_Computer;

nn_Universe *nn_createUniverse(nn_Context *ctx, void *userdata) {
	nn_Universe *u = nn_alloc(ctx, sizeof(nn_Universe));
	if(u == NULL) return NULL;
	u->ctx = *ctx;
	u->userdata = userdata;
	u->memoryLimit = 0;
	u->storageLimit = 0;
	return u;
}

void nn_destroyUniverse(nn_Universe *universe) {
	nn_Context ctx = universe->ctx;
	nn_free(&ctx, universe, sizeof(nn_Universe));
}

void *nn_getUniverseData(nn_Universe *universe) {
	return universe->userdata;
}

size_t nn_getUniverseMemoryLimit(nn_Universe *universe) {
	return universe->memoryLimit;
}

void nn_setUniverseMemoryLimit(nn_Universe *universe, size_t limit) {
	universe->memoryLimit = limit;
}

size_t nn_limitMemory(nn_Universe *universe, size_t memory) {
	if(universe->memoryLimit == 0) return memory;
	if(memory > universe->memoryLimit) memory = universe->memoryLimit;
	return memory;
}

size_t nn_getUniverseStorageLimit(nn_Universe *universe) {
	return universe->storageLimit;
}

void nn_setUniverseStorageLimit(nn_Universe *universe, size_t limit) {
	universe->storageLimit = limit;
}

size_t nn_limitStorage(nn_Universe *universe, size_t storage) {
	if(universe->memoryLimit == 0) return storage;
	if(storage > universe->memoryLimit) storage = universe->storageLimit;
	return storage;
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

	totalMemory = nn_limitMemory(universe, totalMemory);

	nn_Computer *c = nn_alloc(ctx, sizeof(nn_Computer));
	if(c == NULL) return NULL;

	c->state = NN_BOOTUP;
	c->universe = universe;
	c->userdata = userdata;

	c->lock = nn_createLock(ctx);
	if(c->lock == NULL) {
		nn_free(ctx, c, sizeof(nn_Computer));
		return NULL;
	}

	c->address = nn_strdup(ctx, address);
	if(c->address == NULL) {
		nn_destroyLock(ctx, c->lock);
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
		nn_destroyLock(ctx, c->lock);
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
	nn_clearComputerBeep(c);
	return c;
}

void nn_lockComputer(nn_Computer *computer) {
	nn_lock(&computer->universe->ctx, computer->lock);
}

void nn_unlockComputer(nn_Computer *computer) {
	nn_unlock(&computer->universe->ctx, computer->lock);
}

static void nn_dropValue(nn_Value val);

static nn_ComponentEntry *nn_getInternalComponent(nn_Computer *computer, const char *address) {
	nn_ComponentEntry lookingFor = {
		.address = address,
	};
	return nn_hashGet(&computer->components, &lookingFor);
}

nn_Exit nn_startComputer(nn_Computer *computer) {
	if(nn_isComputerOn(computer)) {
		nn_stopComputer(computer);
	}
	nn_ArchitectureRequest req;
	req.computer = computer;
	req.globalState = computer->arch.state;
	req.localState = NULL;
	req.action = NN_ARCH_INIT;
	nn_Exit err = computer->arch.handler(&req);
	if(err) {
		computer->state = NN_CRASHED;
		if(err != NN_EBADCALL) nn_setErrorFromExit(computer, err);
		return err;
	}
	computer->archState = req.localState;
	return NN_OK;
}

void nn_stopComputer(nn_Computer *computer) {
	nn_Context *ctx = &computer->universe->ctx;
	if(nn_isComputerOn(computer)) {
		nn_ArchitectureRequest req;
		req.computer = computer;
		req.globalState = computer->arch.state;
		req.localState = computer->archState;
		req.action = NN_ARCH_DEINIT;
		computer->arch.handler(&req);
		computer->archState = NULL;
	}
	computer->state = NN_BOOTUP;
	for(size_t i = 0; i < computer->signalCount; i++) {
		nn_Signal s = computer->signals[i];
		for(size_t j = 0; j < s.len; j++) nn_dropValue(s.values[j]);
		nn_free(ctx, s.values, sizeof(nn_Value) * s.len);
	}
	computer->signalCount = 0;
}

void nn_forceCrashComputer(nn_Computer *computer, const char *s) {
	nn_stopComputer(computer);
	computer->state = NN_CRASHED;
	nn_setError(computer, s);
}

bool nn_isComputerOn(nn_Computer *computer) {
	return computer->archState != NULL;
}

void nn_setComputerBeep(nn_Computer *computer, nn_Beep beep) {
	if(beep.duration < 0) beep.duration = 0;
	computer->beep = beep;
	nn_addIdleTime(computer, beep.duration);
}

bool nn_getComputerBeep(nn_Computer *computer, nn_Beep *beep) {
	*beep = computer->beep;
	return computer->beep.volume > 0;
}

void nn_clearComputerBeep(nn_Computer *computer) {
	computer->beep.volume = 0;
}

void nn_destroyComputer(nn_Computer *computer) {
	nn_Context *ctx = &computer->universe->ctx;
	nn_stopComputer(computer);

	for(size_t i = 0; i < computer->stackSize; i++) {
		nn_dropValue(computer->callstack[i]);
	}
	for(size_t i = 0; i < computer->userCount; i++) {
		nn_strfree(ctx, computer->users[i]);
	}

	for(nn_ComponentEntry *c = nn_hashIterate(&computer->components, NULL); c != NULL; c = nn_hashIterate(&computer->components, c)) {
		nn_signalComponent(c->comp, computer, NN_CSIGUNMOUNTED);
		nn_dropComponent(c->comp);
	}
	nn_destroyLock(ctx, computer->lock);
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
	if(user == NULL) return true;
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
	if(computer->state == NN_CRASHED) {
		return NN_EBADSTATE;
	}
	nn_resetCallBudget(computer);
	nn_resetComponentBudgets(computer);
	nn_clearstack(computer);
	nn_Exit err;
	// idling pootr
	if(nn_isComputerIdle(computer)) return NN_OK;
	computer->idleTimestamp = nn_getUptime(computer);
	if(computer->state == NN_BOOTUP) {
		// init state
		err = nn_startComputer(computer);
		if(err) return err;
	} else if(computer->state != NN_RUNNING) {
		if(computer->state != NN_CRASHED) nn_setErrorFromExit(computer, NN_EBADSTATE);
		return NN_EBADSTATE;
	}
	computer->state = NN_RUNNING;
	nn_ArchitectureRequest req;
	req.computer = computer;
	req.globalState = computer->arch.state;
	req.localState = computer->archState;
	req.synchronized = false;
	req.action = NN_ARCH_TICK;
	err = computer->arch.handler(&req);
	if(err) {
		computer->state = NN_CRASHED;
		nn_setErrorFromExit(computer, err);
		return err;
	}
	return NN_OK;
}

nn_Exit nn_tickSynchronized(nn_Computer *computer) {
	if(!nn_isComputerOn(computer)) return NN_OK;
	// idling pootr
	if(nn_isComputerIdle(computer)) return NN_OK;
	nn_ArchitectureRequest req;
	req.computer = computer;
	req.globalState = computer->arch.state;
	req.localState = computer->archState;
	req.synchronized = true;
	req.action = NN_ARCH_TICK;
	nn_Exit err = computer->arch.handler(&req);
	if(err) {
		computer->state = NN_CRASHED;
		nn_setErrorFromExit(computer, err);
		return err;
	}
	return NN_OK;
}

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
	req.compAddress = c->address;
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

static nn_MethodEntry *nn_getComponentMethodEntry(nn_Component *c, const char *method) {
	nn_MethodEntry ent = {
		.name = method,
	};
	return nn_hashGet(&c->methodsMap, &ent);
}

// Sets the method flags
void nn_setComponentMethodFlags(nn_Component *c, const char *method, nn_MethodFlags flags) {
	nn_MethodEntry *ent = nn_getComponentMethodEntry(c, method);
	if(ent == NULL) return;
	ent->flags = flags;
}

// combines method flags
void nn_addComponentMethodFlags(nn_Component *c, const char *method, nn_MethodFlags flags) {
	nn_MethodEntry *ent = nn_getComponentMethodEntry(c, method);
	if(ent == NULL) return;
	ent->flags |= flags;
}

// removes method flags
void nn_removeComponentMethodFlags(nn_Component *c, const char *method, nn_MethodFlags flags) {
	nn_MethodEntry *ent = nn_getComponentMethodEntry(c, method);
	if(ent == NULL) return;
	ent->flags &= ~flags;
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

bool nn_hasComponentMethod(nn_Component *c,
    const char *method)
{
    nn_MethodEntry *ent =
        nn_getComponentMethodEntry(c, method);
    if(ent == NULL) return false;

    nn_ComponentRequest req;
    req.ctx = &c->universe->ctx;
    req.computer = NULL;
    req.state = c->state;
    req.classState = c->classState; // Don't remove it. It segfaults.
	req.compAddress = c->address;
    req.action = NN_COMP_CHECKMETHOD;
    req.methodIdx = ent->idx;
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

const char *nn_getComponentAddress(nn_Component *c) {
	return c->address;
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

nn_Exit nn_mountComponent(nn_Computer *c, nn_Component *comp, int slot, bool silent) {
	if(nn_getComponent(c, comp->address) != NULL) return NN_EBADSTATE;

	nn_ComponentEntry ent = {
		.address = comp->address,
		.comp = comp,
		.slot = slot,
		.budgetUsed = 0,
	};
	if(!nn_hashPut(&c->components, &ent)) return NN_ELIMIT;
	nn_retainComponent(comp);
	nn_signalComponent(comp, c, NN_CSIGMOUNTED);
	if(c->state == NN_RUNNING && !silent) {
		return nn_pushComponentAdded(c, comp->address, comp->type);
	}
	return NN_OK;
}

nn_Exit nn_unmountComponent(nn_Computer *c, const char *address, bool silent) {
	nn_Component *comp = nn_getComponent(c, address);
	if(comp == NULL) return NN_OK;
	nn_ComponentEntry lookingFor = {.address = address};
	nn_hashRemove(&c->components, &lookingFor);

	nn_Exit e = NN_OK;
	if(c->state == NN_RUNNING && !silent) {
		e = nn_pushComponentRemoved(c, address, comp->type);
	}
	nn_signalComponent(comp, c, NN_CSIGUNMOUNTED);
	nn_dropComponent(comp);
	return e;
}

nn_Exit nn_swapComponents(nn_Computer *c, nn_Component *previous, nn_Component *next, int slot) {
	bool silent = false;
	if(previous && next) {
		// means for reasons beyond our understanding the config changed
		silent = nn_strcmp(previous->address, next->address) == 0;
	}
	nn_Exit e;
	if(previous != NULL) {
		e = nn_unmountComponent(c, previous->address, silent);
		if(e) return e;
	}
	if(next != NULL) {
		e = nn_mountComponent(c, next, slot, silent);
		if(e) return e;
	}
	return NN_OK;
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
	req.compAddress = c->address;
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
	req.compAddress = component->address;
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
	if(str == NULL) return nn_pushnull(computer);
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
			total += 1;
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
    NN_INIT(nn_EEPROM) {
        .size = 4 * NN_KiB,
		.dataSize = 256,
		.readEnergyCost = 1,
		.writeEnergyCost = 100,
		.readDataEnergyCost = 0.1,
		.writeDataEnergyCost = 5,
		.writeDelay = 2,
		.writeDataDelay = 1,
	},
	NN_INIT(nn_EEPROM) {
		.size = 8 * NN_KiB,
		.dataSize = 1 * NN_KiB,
		.readEnergyCost = 2,
		.writeEnergyCost = 200,
		.readDataEnergyCost = 0.2,
		.writeDataEnergyCost = 10,
		.writeDelay = 2,
		.writeDataDelay = 1,
	},
	NN_INIT(nn_EEPROM) {
		.size = 16 * NN_KiB,
		.dataSize = 2 * NN_KiB,
		.readEnergyCost = 4,
		.writeEnergyCost = 400,
		.readDataEnergyCost = 0.4,
		.writeDataEnergyCost = 20,
		.writeDelay = 1,
		.writeDataDelay = 0.5,
	},
	NN_INIT(nn_EEPROM) {
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
	NN_INIT(nn_Filesystem) {
		.spaceTotal = 1 * NN_MiB,
		.readsPerTick = 4,
		.writesPerTick = 2,
		.opensPerTick = 4,
		.dataEnergyCost = 256.0 / NN_MiB,
		.maxReadSize = 4096,
	},
	NN_INIT(nn_Filesystem) {
		.spaceTotal = 2 * NN_MiB,
		.readsPerTick = 4,
		.writesPerTick = 2,
		.opensPerTick = 8,
		.dataEnergyCost = 512.0 / NN_MiB,
		.maxReadSize = 8192,
	},
	NN_INIT(nn_Filesystem) {
		.spaceTotal = 4 * NN_MiB,
		.readsPerTick = 7,
		.writesPerTick = 3,
		.opensPerTick = 16,
		.dataEnergyCost = 1024.0 / NN_MiB,
		.maxReadSize = 16384,
	},
	NN_INIT(nn_Filesystem) {
		.spaceTotal = 8 * NN_MiB,
		.readsPerTick = 13,
		.writesPerTick = 5,
		.opensPerTick = 32,
		.dataEnergyCost = 2048.0 / NN_MiB,
		.maxReadSize = 32768,
	},
};


const nn_Filesystem nn_defaultFloppy = NN_INIT(nn_Filesystem) {
	.spaceTotal = 512 * NN_KiB,
	.readsPerTick = 1,
	.writesPerTick = 1,
	.dataEnergyCost = 8.0 / NN_MiB,
	.maxReadSize = 2048,
};

const nn_Filesystem nn_defaultTmpFS = NN_INIT(nn_Filesystem) {
	.spaceTotal = 64 * NN_KiB,
	.readsPerTick = 1024,
	.writesPerTick = 512,
	.dataEnergyCost = 0.1 / NN_MiB,
};

const nn_Drive nn_defaultDrives[4] = {
	NN_INIT(nn_Drive) {
		.capacity = 1 * NN_MiB,
		.sectorSize = 512,
		.platterCount = 2,
		.readsPerTick = 20,
		.writesPerTick = 10,
		.rpm = 3600,
		.onlySpinForwards = false,
		.dataEnergyCost = 4.0 / NN_MiB,
	},
	NN_INIT(nn_Drive) {
		.capacity = 2 * NN_MiB,
		.sectorSize = 512,
		.platterCount = 4,
		.readsPerTick = 30,
		.writesPerTick = 15,
		.rpm = 5400,
		.onlySpinForwards = false,
		.dataEnergyCost = 8.0 / NN_MiB,
	},
	NN_INIT(nn_Drive) {
		.capacity = 4 * NN_MiB,
		.sectorSize = 512,
		.platterCount = 8,
		.readsPerTick = 40,
		.writesPerTick = 20,
		.rpm = 7200,
		.onlySpinForwards = false,
		.dataEnergyCost = 16.0 / NN_MiB,
	},
	NN_INIT(nn_Drive) {
		.capacity = 8 * NN_MiB,
		.sectorSize = 512,
		.platterCount = 16,
		.readsPerTick = 60,
		.writesPerTick = 30,
		.rpm = 7200,
		.onlySpinForwards = false,
		.dataEnergyCost = 32.0 / NN_MiB,
	},
};

const nn_Drive nn_floppyDrive = {
	.capacity = 512 * NN_KiB,
	.sectorSize = 512,
	.platterCount = 1,
	.readsPerTick = 10,
	.writesPerTick = 5,
	.rpm = 1800,
	.onlySpinForwards = true,
	.dataEnergyCost = 1.0 / NN_MiB,
};

const nn_NandFlash nn_defaultSSDs[4] = {
	NN_INIT(nn_NandFlash) {
		.capacity = 512 * NN_KiB,
		.sectorSize = 512,
		.readsPerTick = 10,
		.writesPerTick = 5,
		.cellLevel = 1,
		.maxWriteCount = 1<<10,
		.maxWriteAmplification = 4,
		.writeAmplificationExponent = 2,
		.dataEnergyCost = 64.0 / NN_MiB,
	},
	NN_INIT(nn_NandFlash) {
		.capacity = 1 * NN_MiB,
		.sectorSize = 512,
		.readsPerTick = 15,
		.writesPerTick = 7,
		.cellLevel = 2,
		.maxWriteCount = 1<<10,
		.maxWriteAmplification = 8,
		.writeAmplificationExponent = 2,
		.dataEnergyCost = 128.0 / NN_MiB,
	},
	NN_INIT(nn_NandFlash) {
		.capacity = 2 * NN_MiB,
		.sectorSize = 512,
		.readsPerTick = 20,
		.writesPerTick = 10,
		.cellLevel = 3,
		.maxWriteCount = 1<<10,
		.maxWriteAmplification = 12,
		.writeAmplificationExponent = 2,
		.dataEnergyCost = 256.0 / NN_MiB,
	},
	NN_INIT(nn_NandFlash) {
		.capacity = 4 * NN_MiB,
		.sectorSize = 512,
		.readsPerTick = 30,
		.writesPerTick = 15,
		.cellLevel = 4,
		.maxWriteCount = 1<<10,
		.maxWriteAmplification = 16,
		.writeAmplificationExponent = 2,
		.dataEnergyCost = 512.0 / NN_MiB,
	},
};

const nn_NandFlash nn_floppySSD = {
	.capacity = 256 * NN_KiB,
	.sectorSize = 512,
	.readsPerTick = 5,
	.writesPerTick = 2,
	.cellLevel = 1,
	.maxWriteCount = 1<<10,
	.maxWriteAmplification = 4,
	.writeAmplificationExponent = 2,
	.dataEnergyCost = 16.0 / NN_MiB,
};

const nn_ScreenConfig nn_defaultScreens[4] = {
	NN_INIT(nn_ScreenConfig) {
		.maxWidth = 50,
		.maxHeight = 16,
		.maxDepth = 1,
		.defaultPalette = nn_ocpalette4,
		.paletteColors = 0,
		.editableColors = 0,
		.features = NN_SCRF_NONE,
		.energyPerPixel = 0.05,
		.minBrightness = 0.5,
		.maxBrightness = 1,
	},
	NN_INIT(nn_ScreenConfig) {
		.maxWidth = 80,
		.maxHeight = 25,
		.maxDepth = 4,
		.defaultPalette = nn_ocpalette4,
		.paletteColors = 16,
		.editableColors = 0,
		.features = NN_SCRF_MOUSE | NN_SCRF_TOUCHINVERTED,
		.energyPerPixel = 0.05,
		.minBrightness = 0.25,
		.maxBrightness = 1.2,
	},
	NN_INIT(nn_ScreenConfig) {
		.maxWidth = 160,
		.maxHeight = 50,
		.maxDepth = 8,
		.defaultPalette = nn_ocpalette8,
		.paletteColors = 256,
		.editableColors = 16,
		.features = NN_SCRF_MOUSE | NN_SCRF_TOUCHINVERTED | NN_SCRF_PRECISE | NN_SCRF_EDITABLECOLORS,
		.energyPerPixel = 0.05,
		.minBrightness = 0.1,
		.maxBrightness = 1.5,
	},
	NN_INIT(nn_ScreenConfig) {
		.maxWidth = 240,
		.maxHeight = 80,
		.maxDepth = 16,
		.defaultPalette = nn_ocpalette8,
		.paletteColors = 256,
		.editableColors = 256,
		.features = NN_SCRF_NONE | NN_SCRF_EDITABLECOLORS,
		.energyPerPixel = 0.05,
		.minBrightness = 0.1,
		.maxBrightness = 2,
	},
};

const nn_GPU nn_defaultGPUs[4] = {
	NN_INIT(nn_GPU) {
		.maxWidth = 50,
		.maxHeight = 16,
		.maxDepth = 1,
		.totalVRAM = 5000,
		.copyPerTick = 16,
		.fillPerTick = 32,
		.setPerTick = 64,
		.setForegroundPerTick = 32,
		.setBackgroundPerTick = 32,
		.energyPerWrite = 0.0002,
		.energyPerClear = 0.0001,
	},
	NN_INIT(nn_GPU) {
		.maxWidth = 80,
		.maxHeight = 25,
		.maxDepth = 4,
		.totalVRAM = 10000,
		.copyPerTick = 32,
		.fillPerTick = 64,
		.setPerTick = 128,
		.setForegroundPerTick = 64,
		.setBackgroundPerTick = 64,
		.energyPerWrite = 0.001,
		.energyPerClear = 0.0005,
	},
	NN_INIT(nn_GPU) {
		.maxWidth = 160,
		.maxHeight = 50,
		.maxDepth = 8,
		.totalVRAM = 20000,
		.copyPerTick = 64,
		.fillPerTick = 128,
		.setPerTick = 256,
		.setForegroundPerTick = 128,
		.setBackgroundPerTick = 128,
		.energyPerWrite = 0.002,
		.energyPerClear = 0.001,
	},
	NN_INIT(nn_GPU) {
		.maxWidth = 240,
		.maxHeight = 80,
		.maxDepth = 16,
		.totalVRAM = 65536,
		.copyPerTick = 128,
		.fillPerTick = 256,
		.setPerTick = 512,
		.setForegroundPerTick = 256,
		.setBackgroundPerTick = 256,
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
	unsigned int _r = (color >> 16) & 0xFF;
	unsigned int _g = (color >> 8) & 0xFF;
	unsigned int _b = (color >> 0) & 0xFF;

	*r = (double)_r / 255;
	*g = (double)_g / 255;
	*b = (double)_b / 255;
}

double nn_colorLuminance(int color) {
	double r, g, b;
	nn_splitColor(color, &r, &g, &b);
	// taken from https://stackoverflow.com/questions/687261/converting-rgb-to-grayscale-intensity
	return r * 0.2126 + g * 0.7152 + b * 0.0722;
}

// Credit to Blendi for writing this, the old algorithm based off luminance gave bad results
static double nn_colorDistance(int a, int b) {
	double ar,ag,ab;
	double br,bg,bb;

	nn_splitColor(a, &ar, &ag, &ab);
	nn_splitColor(b, &br, &bg, &bb);
	
	double dr = ar-br, dg = ag-bg, db = ab-bb;

	return 0.2126 * dr*dr + 0.7152 * dg*dg + 0.0722 * db*db;
}

int nn_mapColor(int color, int *palette, size_t len) {
	int bestColor = color;
	// maximum distance, the one between white and black, is ~1.0 so this is way higher
	double bestDist = 100000;
	for(size_t i = 0; i < len; i++) {
		int entry = palette[i];
		double dist = nn_colorDistance(color, entry);
		if(dist <= bestDist) {
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
	nn_Exit err = nn_pushstring(computer, "drop");
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

// the value is not returned for all execution paths - not a windows bug probably, need tests on *nix
nn_Exit nn_pushScroll(nn_Computer *computer, const char *screenAddress, double x, double y, double direction, const char *player) {
	if(!nn_hasUser(computer, player)) return NN_OK;
	nn_Exit err = nn_pushstring(computer, "scroll");
	if(err) return err;
	err = nn_pushstring(computer, screenAddress);
	if(err) return err;
	err = nn_pushnumber(computer, x);
	if(err) return err;
	err = nn_pushnumber(computer, y);
	if(err) return err;
	err = nn_pushnumber(computer, direction);
	if(err) return err;
	err = nn_pushstring(computer, player);
	if(err) return err;
	return nn_pushSignal(computer, 6);
}

// the value is not returned for all execution paths - not a windows bug probably, need tests on *nix
nn_Exit nn_pushWalk(nn_Computer *computer, const char *screenAddress, double x, double y, const char *player) {
	if(!nn_hasUser(computer, player)) return NN_OK;
	nn_Exit err = nn_pushstring(computer, "walk");
	if(err) return err;
	err = nn_pushstring(computer, screenAddress);
	if(err) return err;
	err = nn_pushnumber(computer, x);
	if(err) return err;
	err = nn_pushnumber(computer, y);
	if(err) return err;
	err = nn_pushstring(computer, player);
	if(err) return err;
	return nn_pushSignal(computer, 5);
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

nn_Exit nn_pushRedstoneChanged(nn_Computer *computer, const char *redstoneAddress, int side, int oldValue, int newValue, int color);

nn_Exit nn_pushMotion(nn_Computer *computer, double relX, double relY, double relZ, const char *entityName);

typedef enum nn_NetworkValueTag {
	NN_NETVAL_NULL = 0x00,
	NN_NETVAL_TRUE = 0x01,
	NN_NETVAL_FALSE = 0x02,
	NN_NETVAL_NUM = 0x03,
	NN_NETVAL_STR = 0x04,
	NN_NETVAL_RESOURCE = 0x05,
	NN_NETVAL_TABLE = 0x06,
} nn_NetworkValueTag;

static size_t nn_sizeOfNetworkValue(nn_Value val);

static size_t nn_sizeOfNetworkContents(nn_Value *vals, size_t len) {
	size_t s = 0;
	for(size_t i = 0; i < len; i++) s += nn_sizeOfNetworkValue(vals[i]);
	return s;
}

static size_t nn_sizeOfNetworkValue(nn_Value val) {
	// 1-byte tag, + value-dependant encoding
	size_t n = 1;
	switch(val.type) {
	case NN_VAL_NULL:
	case NN_VAL_BOOL:
		break;
	case NN_VAL_NUM:
		n += sizeof(double);
		break;
	case NN_VAL_STR:
		n += sizeof(size_t) + val.string->len;
		break;
	case NN_VAL_USERDATA:
		n += sizeof(size_t);
		break;
	case NN_VAL_TABLE:
		n += sizeof(size_t) + nn_sizeOfNetworkContents(val.table->vals, val.table->len);
		break;
	}
	return n;
}

static size_t nn_encodeNetworkValue(nn_Value val, char *buf) {
	size_t n = 0;
	switch(val.type) {
	case NN_VAL_NULL:
		*buf = NN_NETVAL_NULL;
		return 1;
	case NN_VAL_BOOL:
		*buf = val.boolean ? NN_NETVAL_TRUE : NN_NETVAL_FALSE;
		return 1;
	case NN_VAL_NUM:
		*buf = NN_NETVAL_NUM;
		nn_memcpy(buf + 1, &val.number, sizeof(double));
		return 1 + sizeof(double);
	case NN_VAL_STR:
		*buf = NN_NETVAL_STR;
		nn_memcpy(buf + 1, &val.string->len, sizeof(size_t));
		nn_memcpy(buf + 1 + sizeof(size_t), val.string->data, val.string->len);
		return 1 + sizeof(size_t) + val.string->len;
	case NN_VAL_USERDATA:
		*buf = NN_NETVAL_RESOURCE;
		nn_memcpy(buf + 1, &val.userdataIdx, sizeof(size_t));
		return 1 + sizeof(size_t);
	case NN_VAL_TABLE:
		*buf = NN_NETVAL_TABLE;
		n = 1;
		nn_memcpy(buf + n, &val.table->len, sizeof(size_t));
		n += sizeof(size_t);
		for(size_t i = 0; i < val.table->len; i++) {
			n += nn_encodeNetworkValue(val.table->vals[i], buf + n);
		}
		return n;
	}
	*buf = NN_NETVAL_NULL;
	return 1;
}

nn_Exit nn_encodeNetworkContents(nn_Computer *computer, nn_EncodedNetworkContents *contents, size_t valueCount) {
	if(computer->stackSize < valueCount) return NN_EBELOWSTACK;
	nn_Value *vals = computer->callstack + computer->stackSize - valueCount;
	size_t len = nn_sizeOfNetworkContents(vals, valueCount);

	contents->ctx = &computer->universe->ctx;
	contents->valueCount = valueCount;
	contents->buflen = len;
	contents->buf = nn_alloc(contents->ctx, len);
	if(contents->buf == NULL) return NN_ENOMEM;
	nn_memset(contents->buf, 0, len);

	size_t n = 0;
	for(size_t i = 0; i < valueCount; i++) {
		n += nn_encodeNetworkValue(vals[i], contents->buf + n);
	}

	return NN_OK;
}

nn_Exit nn_copyNetworkContents(nn_Context *ctx, nn_EncodedNetworkContents *contents, const char *buf, size_t buflen, size_t valueCount) {
	contents->ctx = ctx;
	contents->valueCount = valueCount;
	contents->buflen = buflen;
	contents->buf = nn_alloc(ctx, buflen);
	if(contents->buf == NULL) return NN_ENOMEM;
	nn_memcpy(contents->buf, buf, buflen);
	return NN_OK;
}

void nn_dropNetworkContents(nn_EncodedNetworkContents *contents) {
	nn_free(contents->ctx, contents->buf, contents->buflen);
}

static nn_Exit nn_decodeNetworkValue(nn_Value *val, nn_Context *ctx, const char *buf, size_t *len) {
	size_t decodedLen = 0, off = 0;
	nn_Value tmpval;
	switch((nn_NetworkValueTag)buf[0]) {
	case NN_NETVAL_NULL:
		*len = 1;
		val->type = NN_VAL_NULL;
		return NN_OK;
	case NN_NETVAL_TRUE:
	case NN_NETVAL_FALSE:
		*len = 1;
		val->type = NN_VAL_BOOL;
		val->boolean = buf[0] == NN_NETVAL_TRUE;
		return NN_OK;
	case NN_NETVAL_NUM:
		*len = 1 + sizeof(double);
		val->type = NN_VAL_NUM;
		nn_memcpy(&val->number, buf + 1, sizeof(double));
		return NN_OK;
	case NN_NETVAL_STR:
		nn_memcpy(&decodedLen, buf + 1, sizeof(size_t));
		val->type = NN_VAL_STR;
		val->string = nn_alloc(ctx, sizeof(nn_String) + decodedLen + 1);
		if(val->string == NULL) return NN_ENOMEM;
		val->string->ctx = *ctx;
		val->string->refc = 1;
		val->string->len = decodedLen;
		nn_memcpy(val->string->data, buf + 1 + sizeof(size_t), decodedLen);
		val->string->data[decodedLen] = '\0';
		*len = 1 + sizeof(size_t) + decodedLen;
		return NN_OK;
	case NN_NETVAL_RESOURCE:
		val->type = NN_VAL_USERDATA;
		nn_memcpy(&val->userdataIdx, buf + 1, sizeof(size_t));
		*len = 1 + sizeof(size_t);
		return NN_OK;
	case NN_NETVAL_TABLE:
		val->type = NN_VAL_TABLE;
		nn_memcpy(&decodedLen, buf + 1, sizeof(size_t));
		val->table = nn_alloc(ctx, sizeof(nn_Table) + sizeof(nn_Value) * decodedLen * 2);
		if(val->table == NULL) return NN_ENOMEM;
		val->table->ctx = *ctx;
		val->table->refc = 1;
		val->table->len = decodedLen;
		off = 1 + sizeof(size_t);
		for(size_t i = 0; i < decodedLen*2; i++) {
			size_t tmplen = 0;
			nn_Exit e = nn_decodeNetworkValue(&tmpval, ctx, buf + off, &tmplen);
			if(e) {
				for(size_t j = 0; j < i; j++) nn_dropValue(val->table->vals[j]);
				return e;
			}
			val->table->vals[i] = tmpval;
			off += tmplen;
		}
		*len = off;
		return NN_OK;
	}
	*len = 1;
	val->type = NN_VAL_NULL;
	return NN_OK;
}

nn_Exit nn_pushNetworkContents(nn_Computer *C, const nn_EncodedNetworkContents *contents) {
	nn_Value val;
	size_t off = 0;
	for(size_t i = 0; i < contents->valueCount; i++) {
		size_t len = 0;
		nn_Exit e = nn_decodeNetworkValue(&val, &C->universe->ctx, contents->buf + off, &len);
		if(e) return e;
		e = nn_pushvalue(C, val);
		if(e) {
			nn_dropValue(val);
			return e;
		}
		off += len;
	}
	return NN_OK;
}

nn_Exit nn_pushModemMessage(nn_Computer *C, const char *modemAddress, const char *sender, int port, double distance, const nn_EncodedNetworkContents *contents) {
	size_t signalVals = 5 + contents->valueCount;
	nn_Exit e = nn_pushstring(C, "modem_message");
	if(e) return e;
	e = nn_pushstring(C, modemAddress);
	if(e) return e;
	e = nn_pushstring(C, sender);
	if(e) return e;
	e = nn_pushinteger(C, port);
	if(e) return e;
	e = nn_pushnumber(C, distance);
	if(e) return e;
	e = nn_pushNetworkContents(C, contents);
	if(e) return e;
	return nn_pushSignal(C, signalVals);
}

nn_Computer *nn_fromWrappedComputer(nn_Component *component) {
	if(nn_strcmp(component->internalID, "NN_WRAPPEDCOMPUTER") == 0) {
		return component->state;
	}
	return NULL;
}

nn_Exit nn_transferErrorFrom(nn_Exit exit, nn_Computer *from, nn_Computer *to) {
	const char *err = nn_getError(from);
	if(err != NULL) nn_setError(to, err);
	return exit;
}

typedef enum nn_CompNum {
	NN_COMPNUM_START,
	NN_COMPNUM_STOP,
	NN_COMPNUM_ISRUNNING,
	NN_COMPNUM_GETDEVICEINFO,
	NN_COMPNUM_CRASH,
	NN_COMPNUM_GETARCH,
	NN_COMPNUM_ISROBOT,
	NN_COMPNUM_BEEP,

	NN_COMPNUM_COUNT,
} nn_CompNum;

static nn_Exit nn_computerHandler(nn_ComponentRequest *req) {
	if(req->action == NN_COMP_DROP) return NN_OK;
	if(req->action == NN_COMP_SIGNAL) return NN_OK;
	nn_Computer *src = req->computer;
	if(src) nn_setError(src, "computer: not implemented yet");
	return NN_EBADCALL;
}

nn_Component *nn_wrapComputer(nn_Computer *computer) {
	const nn_Method methods[NN_COMPNUM_COUNT] = {
		[NN_COMPNUM_START] = {"start", "function(): boolean - Attempts to turn on the computer, will return false if it is already on or it failed", NN_INDIRECT},	
		[NN_COMPNUM_STOP] = {"stop", "function(): boolean - Attempts to turn ooff the computer, will return false if it is already off or it failed", NN_INDIRECT},	
		[NN_COMPNUM_ISRUNNING] = {"isRunning", "function(): boolean - Returns whether it is running or not", NN_INDIRECT},	
		[NN_COMPNUM_GETDEVICEINFO] = {"getDeviceInfo", "function(): table<string, table<string, string>> - Returns a table of device information for the computer", NN_INDIRECT},	
		[NN_COMPNUM_CRASH] = {"crash", "function(error: string) - Will forcefully crash the computer, if it is running", NN_INDIRECT},	
		[NN_COMPNUM_GETARCH] = {"getArchitecture", "function(): string - Get the architecture of the computer", NN_INDIRECT},	
		[NN_COMPNUM_ISROBOT] = {"isRobot", "function(): boolean - Returns whether the computer is a robot", NN_INDIRECT},	
		[NN_COMPNUM_BEEP] = {"beep", "function(frequency?: number, duration?: number, volume?: number) - Makes the computer make a beep sound", NN_INDIRECT},	
	};

	nn_Component *c = nn_createComponent(computer->universe, computer->address, "computer");
	if(c == NULL) return NULL;
	nn_setComponentState(c, computer);
	nn_setComponentHandler(c, nn_computerHandler);
	if(nn_setComponentTypeID(c, "NN_WRAPPEDCOMPUTER")) {
		nn_dropComponent(c);
		return NULL;
	}
	if(nn_setComponentMethodsArray(c, methods, NN_COMPNUM_COUNT)) {
		nn_dropComponent(c);
		return NULL;
	}
	return c;
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
	NN_EENUM_ISRO,
	NN_EENUM_GETCHKSUM,
	NN_EENUM_MKRO,
	
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
		nn_removeEnergy(C, eeprom.readEnergyCost);
		ereq.action = NN_EEPROM_GET;
		NN_VLA(char, buf, eeprom.size);
		ereq.buf = buf;
		ereq.buflen = eeprom.size;
		e = state->handler(&ereq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushlstring(C, ereq.buf, ereq.buflen);
	}
	if(method == NN_EENUM_GETDATA) {
		nn_removeEnergy(C, eeprom.readDataEnergyCost);
		ereq.action = NN_EEPROM_GETDATA;
		NN_VLA(char, buf, eeprom.size);
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
		nn_removeEnergy(C, eeprom.writeEnergyCost);
		nn_addIdleTime(C, eeprom.writeDelay);
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
		nn_removeEnergy(C, eeprom.writeDataEnergyCost);
		nn_addIdleTime(C, eeprom.writeDataDelay);
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
		[NN_EENUM_ISRO] = {"isReadonly", "function(): boolean - Returns whether the EEPROM is read-only.", NN_DIRECT},
		[NN_EENUM_GETCHKSUM] = {"getChecksum", "function(): string - Returns a checksum of the EEPROM code.", NN_DIRECT},
		[NN_EENUM_MKRO] = {"makeReadonly", "function(checksum: string): boolean - Make the EEPROM read-only if checksum passes.", NN_INDIRECT},
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

typedef enum nn_FSNum {
	// drive stuff
	NN_FSNUM_SPACETOTAL,
	NN_FSNUM_SPACEUSED,
	NN_FSNUM_GETMAXREAD,
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
	nn_Context *ctx = req->ctx;
	nn_FSState *state = req->classState;
	nn_FSRequest freq;
	freq.ctx = req->ctx;
	freq.computer = req->computer;
	freq.state = req->state;
	freq.fs = &state->fs;
	if(req->action == NN_COMP_DROP) {
		freq.action = NN_FS_DROP;
		state->handler(&freq);
		nn_free(ctx, state, sizeof(*state));
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
	if(method == NN_FSNUM_GETMAXREAD) {
		req->returnCount = 1;
		return nn_pushinteger(C, state->fs.maxReadSize);
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
		nn_costComponent(C, req->compAddress, state->fs.opensPerTick);
		return nn_pushinteger(C, freq.fd);
	}
	if(method == NN_FSNUM_READ) {
		if(nn_checkinteger(C, 0, "bad argument #1 (fd expected)")) return NN_EBADCALL;
		e = nn_defaultinteger(C, 1, state->fs.maxReadSize);
		if(e) return e;
		if(nn_checknumber(C, 1, "bad argument #2 (number expected)")) return NN_EBADCALL;
		double requested = nn_tonumber(C, 1);
		if(requested > state->fs.maxReadSize) requested = state->fs.maxReadSize;
		freq.action = NN_FS_READ;
		freq.fd = nn_tointeger(C, 0);
		char *buf = nn_alloc(ctx, state->fs.maxReadSize);
		if(buf == NULL) return NN_ENOMEM;
		freq.read.buf = buf;
		freq.read.len = requested;
		e = state->handler(&freq);
		if(e) {
			nn_free(ctx, buf, state->fs.maxReadSize);
			return e;
		}
		if(freq.read.buf == NULL) {
			nn_free(ctx, buf, state->fs.maxReadSize);
			return NN_OK;
		}
		nn_costComponent(C, req->compAddress, state->fs.readsPerTick);
		nn_removeEnergy(C, state->fs.dataEnergyCost * freq.read.len);
		req->returnCount = 1;
		e = nn_pushlstring(C, buf, freq.read.len);
		nn_free(ctx, buf, state->fs.maxReadSize);
		return e;
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
		nn_costComponent(C, req->compAddress, state->fs.writesPerTick);
		nn_removeEnergy(C, state->fs.dataEnergyCost * freq.write.len);
		return nn_pushbool(C, true);
	}
	if(method == NN_FSNUM_SEEK) {
		if(nn_checkinteger(C, 0, "bad argument #1 (fd expected)")) return NN_EBADCALL;
		e = nn_defaultstring(C, 1, "cur");
		if(e) return e;
		if(nn_checkstring(C, 1, "bad argument #2 (whence expected)")) return NN_EBADCALL;
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
		freq.action = NN_FS_SEEK;
		freq.fd = nn_tointeger(C, 0);
		freq.seek.whence = seek;
		freq.seek.off = nn_tointeger(C, 2);
		e = state->handler(&freq);
		if(e) return e;
		req->returnCount = 1;
		nn_costComponent(C, req->compAddress, state->fs.readsPerTick);
		return nn_pushinteger(C, freq.seek.off);
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
		[NN_FSNUM_GETMAXREAD] = {"getMaxRead", "function(): integer - Capacity of read buffer, the maximum amount of data which can be read", NN_DIRECT},
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
		[NN_FSNUM_MKDIR] = {"makeDirectory", "function(path: string): boolean - Create a directory, recursively. Does not fail if directory already exists", NN_INDIRECT},
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

bool nn_mergeFilesystems(nn_Filesystem *merged, const nn_Filesystem *fs, size_t len) {
	if(len == 0) return false;
	*merged = fs[0];
	for(size_t i = 1; i < len; i++) {
		merged->readsPerTick += fs[i].readsPerTick;
		merged->writesPerTick += fs[i].writesPerTick;
		if(merged->maxReadSize < fs[i].maxReadSize) merged->maxReadSize = fs[i].maxReadSize;
		merged->dataEnergyCost += fs[i].dataEnergyCost;
		merged->spaceTotal += fs[i].spaceTotal;
	}
	merged->readsPerTick /= len;
	merged->writesPerTick /= len;
	merged->dataEnergyCost /= len;
	return true;
}

static void nn_drive_seekPenalty(nn_Computer *C, size_t lastSector, size_t newSector, const nn_Drive *drive) {
	// Check if SSD
	if(drive->rpm == 0) return;

	size_t maxSectors = drive->capacity / drive->sectorSize;
	size_t sectorsPerPlatter = maxSectors / drive->platterCount;

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

	// RPM over the number of sectors, over 60 seconds.
	double latency = (double)sectorDelta * 60 / ((double)drive->rpm * maxSectors);
	nn_addIdleTime(C, latency);
}

typedef enum nn_DrvNum {
	NN_DRVNUM_GETCAPACITY,
	NN_DRVNUM_GETSECTORSIZE,
	NN_DRVNUM_GETPLATTERCOUNT,
	NN_DRVNUM_ISRO,
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
	dreq.drv = &state->drive;
	nn_Exit e;

	if(request->action == NN_COMP_SIGNAL) return NN_OK;
	if(request->action == NN_COMP_CHECKMETHOD) return NN_OK;

	if(request->action == NN_COMP_DROP) {
		dreq.action = NN_DRIVE_DROP;
		state->handler(&dreq);
		nn_free(ctx, state, sizeof(*state));
		return NN_OK;
	}
	size_t ss = state->drive.sectorSize;
	size_t sectorCount = state->drive.capacity / ss;
	unsigned int method = request->methodIdx;
	if(method == NN_DRVNUM_GETCAPACITY) {
		request->returnCount = 1;
		return nn_pushinteger(C, state->drive.capacity);
	}
	if(method == NN_DRVNUM_GETSECTORSIZE) {
		request->returnCount = 1;
		return nn_pushinteger(C, ss);
	}
	if(method == NN_DRVNUM_ISRO) {
		dreq.action = NN_DRIVE_ISRO;
		e = state->handler(&dreq);
		if(e) return e;
		request->returnCount = 1;
		return nn_pushbool(C, dreq.readonly);
	}
	if(method == NN_DRVNUM_GETLABEL) {
		dreq.action = NN_DRIVE_GETLABEL;
		char buf[NN_MAX_LABEL];
		dreq.getlabel.buf = buf;
		dreq.getlabel.len = NN_MAX_LABEL;
		e = state->handler(&dreq);
		if(e) return e;
		request->returnCount = 1;
		if(dreq.getlabel.len == 0) return nn_pushnull(C);
		return nn_pushlstring(C, dreq.getlabel.buf, dreq.getlabel.len);
	}
	if(method == NN_DRVNUM_SETLABEL) {
		e = nn_defaultstring(C, 0, "");
		if(e) return e;
		if(nn_checkstring(C, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
		dreq.action = NN_DRIVE_SETLABEL;
		dreq.setlabel.label = nn_tolstring(C, 0, &dreq.setlabel.len);
		e = state->handler(&dreq);
		if(e) return e;
		request->returnCount = 1;
		return nn_pushlstring(C, dreq.setlabel.label, dreq.setlabel.len);
	}
	if(method == NN_DRVNUM_READSECTOR) {
		if(nn_checkinteger(C, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
		int sec = nn_tointeger(C, 0);
		if(sec < 1 || sec > sectorCount) {
			nn_setError(C, "sector out of bounds");
			return NN_EBADCALL;
		}
		int curPos = 0;
		dreq.action = NN_DRIVE_CURPOS;
		e = state->handler(&dreq);
		if(e) return e;
		curPos = dreq.curpos;

		nn_drive_seekPenalty(C, curPos, sec, &state->drive);
		nn_costComponent(C, request->compAddress, state->drive.readsPerTick);
		nn_removeEnergy(C, state->drive.dataEnergyCost * ss);

		char *sector = nn_alloc(ctx, ss);
		if(sector == NULL) return NN_ENOMEM;

		dreq.action = NN_DRIVE_READSECTOR;
		dreq.readSector.sector = sec;
		dreq.readSector.buf = sector;
		e = state->handler(&dreq);
		if(e) {
			nn_free(ctx, sector, ss);
			return e;
		}
		request->returnCount = 1;
		e = nn_pushlstring(C, sector, ss);
		nn_free(ctx, sector, ss);
		return e;
	}

	if(C) nn_setError(C, "drive: not implemented yet");
	return NN_EBADCALL;
}

nn_Component *nn_createDrive(nn_Universe *universe, const char *address, const nn_Drive *drive, void *state, nn_DriveHandler *handler) {
	nn_Component *c = nn_createComponent(universe, address, "drive");
	if(c == NULL) return NULL;
	const nn_Method methods[NN_DRVNUM_COUNT] = {
		[NN_DRVNUM_GETCAPACITY] = {"getCapacity", "function(): integer - Get drive capacity", NN_DIRECT},
		[NN_DRVNUM_GETSECTORSIZE] = {"getSectorSize", "function(): integer - Get sector size", NN_DIRECT},
		[NN_DRVNUM_GETPLATTERCOUNT] = {"getPlatterCount", "function(): integer - Get number of platters on this drive", NN_DIRECT},
		[NN_DRVNUM_ISRO] = {"isReadOnly", "function(): boolean - Get whether the drive is read-only", NN_DIRECT},
		[NN_DRVNUM_GETLABEL] = {"getLabel", "function(): string? - Get drive label", NN_DIRECT},
		[NN_DRVNUM_SETLABEL] = {"setLabel", "function(label: string?): string - Set drive label", NN_INDIRECT},
		[NN_DRVNUM_READSECTOR] = {"readSector", "function(sector: integer): string - Read a sector from the drive", NN_DIRECT},
		[NN_DRVNUM_WRITESECTOR] = {"writeSector", "function(sector: integer): boolean - Read a sector from the drive", NN_DIRECT},
		[NN_DRVNUM_READBYTE] = {"readByte", "function(byte: integer): integer - Read a single signed byte", NN_DIRECT},
		[NN_DRVNUM_READUBYTE] = {"readUByte", "function(byte: integer): integer - Read a single unsigned byte", NN_DIRECT},
		[NN_DRVNUM_WRITEBYTE] = {"writeByte", "function(byte: integer, value: integer): boolean - Write a single byte", NN_DIRECT},
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

bool nn_mergeDrives(nn_Drive *merged, const nn_Drive *drives, size_t len) {
	if(len == 0) return false;
	*merged = drives[0];
	for(size_t i = 1; i < len; i++) {
		nn_Drive d = drives[i];
		// invalid SSD/HDD combo
		if(d.rpm == 0 && merged->rpm != 0) return false;
		if(d.rpm != 0 && merged->rpm == 0) return false;
		// conflicting sector sizes
		if(d.sectorSize != merged->sectorSize) return false;
		if(d.rpm != 0) {
			if(d.onlySpinForwards && !merged->onlySpinForwards) return false;
			if(!d.onlySpinForwards && merged->onlySpinForwards) return false;
		}

		merged->readsPerTick += d.readsPerTick;
		merged->writesPerTick += d.writesPerTick;
		merged->dataEnergyCost += d.dataEnergyCost;
		merged->rpm += d.rpm;
		merged->capacity += d.capacity;
		merged->platterCount += d.platterCount;
	}
	merged->readsPerTick /= len;
	merged->writesPerTick /= len;
	merged->dataEnergyCost /= len;
	merged->rpm /= len;
	return true;
}

static size_t nn_flash_writesAdded(nn_Context *ctx, const nn_NandFlash *flash) {
	double x = nn_randfi(ctx);
	double m = 1;
	// TODO: use O(log N) algorithm instead of O(N)
	for(size_t i = 0; i < flash->writeAmplificationExponent; i++) m *= x;

	size_t max = flash->maxWriteAmplification * flash->cellLevel;
	size_t amount = m * max;
	if(amount < 1) amount = 1;
	if(amount > max) amount = max;
	return amount;
}

typedef enum nn_FlashNum {
	NN_FLASHNUM_GETCAPACITY,
	NN_FLASHNUM_GETSECTORSIZE,
	NN_FLASHNUM_GETLAYERS,
	NN_FLASHNUM_GETWEARLEVEL,
	NN_FLASHNUM_ISRO,
	NN_FLASHNUM_GETLABEL,
	NN_FLASHNUM_SETLABEL,
	NN_FLASHNUM_READSECTOR,
	NN_FLASHNUM_WRITESECTOR,
	NN_FLASHNUM_READBYTE,
	NN_FLASHNUM_READUBYTE,
	NN_FLASHNUM_WRITEBYTE,

	NN_FLASHNUM_COUNT,
} nn_FlashNum;

typedef struct nn_FlashState {
	nn_Context *ctx;
	nn_NandFlash flash;
	nn_FlashHandler *handler;
} nn_FlashState;

static nn_Exit nn_flashHandler(nn_ComponentRequest *request) {
	nn_Context *ctx = request->ctx;
	nn_Computer *C = request->computer;
	nn_FlashState *state = request->classState;

	nn_FlashRequest freq;
	freq.ctx = ctx;
	freq.computer = C;
	freq.state = request->state;
	freq.flash = &state->flash;
	nn_Exit e;

	if(request->action == NN_COMP_SIGNAL) return NN_OK;
	if(request->action == NN_COMP_CHECKMETHOD) return NN_OK;

	if(request->action == NN_COMP_DROP) {
		freq.action = NN_FLASH_DROP;
		state->handler(&freq);
		nn_free(ctx, state, sizeof(*state));
		return NN_OK;
	}
	size_t ss = state->flash.sectorSize;
	size_t sectorCount = state->flash.capacity / ss;
	size_t maxWrite = state->flash.maxWriteCount;
	nn_FlashNum method = request->methodIdx;
	if(method == NN_FLASHNUM_GETCAPACITY) {
		request->returnCount = 1;
		return nn_pushinteger(C, state->flash.capacity);
	}
	if(method == NN_FLASHNUM_GETSECTORSIZE) {
		request->returnCount = 1;
		return nn_pushinteger(C, ss);
	}
	if(method == NN_FLASHNUM_GETLAYERS) {
		request->returnCount = 1;
		return nn_pushinteger(C, state->flash.cellLevel);
	}
	if(method == NN_FLASHNUM_GETWEARLEVEL) {
		freq.action = NN_FLASH_GETWRITES;
		e = state->handler(&freq);
		if(e) return e;
		request->returnCount = 1;
		// would crash the math
		if(maxWrite == 0) return nn_pushnumber(C, 100.0);
		if(sectorCount == 0) return nn_pushnumber(C, 100.0);
		double num = freq.writeCount * 100.0 / sectorCount / maxWrite;
		return nn_pushnumber(C, num);
	}
	if(method == NN_FLASHNUM_ISRO) {
		freq.action = NN_FLASH_ISRO;
		e = state->handler(&freq);
		if(e) return e;
		request->returnCount = 1;
		return nn_pushbool(C, freq.readonly);
	}
	if(method == NN_FLASHNUM_GETLABEL) {
		freq.action = NN_FLASH_GETLABEL;
		char buf[NN_MAX_LABEL];
		freq.getlabel.buf = buf;
		freq.getlabel.len = NN_MAX_LABEL;
		e = state->handler(&freq);
		if(e) return e;
		request->returnCount = 1;
		if(freq.getlabel.len == 0) return nn_pushnull(C);
		return nn_pushlstring(C, freq.getlabel.buf, freq.getlabel.len);
	}
	if(method == NN_FLASHNUM_SETLABEL) {
		e = nn_defaultstring(C, 0, "");
		if(e) return e;
		if(nn_checkstring(C, 0, "bad argument #1 (string expected)")) return NN_EBADCALL;
		freq.action = NN_FLASH_SETLABEL;
		freq.setlabel.buf = nn_tolstring(C, 0, &freq.setlabel.len);
		e = state->handler(&freq);
		if(e) return e;
		request->returnCount = 1;
		return nn_pushlstring(C, freq.setlabel.buf, freq.setlabel.len);
	}
	if(method == NN_FLASHNUM_READSECTOR) {
		if(nn_checkinteger(C, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
		int sec = nn_tointeger(C, 0);
		if(sec < 1 || sec > sectorCount) {
			nn_setError(C, "sector out of bounds");
			return NN_EBADCALL;
		}
		nn_costComponent(C, request->compAddress, state->flash.readsPerTick);
		nn_removeEnergy(C, state->flash.dataEnergyCost * ss);

		char *sector = nn_alloc(ctx, ss);
		if(sector == NULL) return NN_ENOMEM;

		freq.action = NN_FLASH_READSECTOR;
		freq.readsector.sec = sec;
		freq.readsector.buf = sector;
		e = state->handler(&freq);
		if(e) {
			nn_free(ctx, sector, ss);
			return e;
		}
		request->returnCount = 1;
		e = nn_pushlstring(C, sector, ss);
		nn_free(ctx, sector, ss);
		return e;
	}
	if(method == NN_FLASHNUM_WRITESECTOR) {
		if(nn_checkinteger(C, 0, "bad argument #1 (integer expected)")) return NN_EBADCALL;
		if(nn_checkstring(C, 1, "bad argument #2 (string expected)")) return NN_EBADCALL;
		int sec = nn_tointeger(C, 0);
		if(sec < 1 || sec > sectorCount) {
			nn_setError(C, "sector out of bounds");
			return NN_EBADCALL;
		}
		freq.action = NN_FLASH_GETWRITES;
		e = state->handler(&freq);
		if(e) return e;
		if(freq.writeCount >= maxWrite * sectorCount) {
			nn_setError(C, "flash is not conductive enough");
			return NN_EBADCALL;
		}

		nn_costComponent(C, request->compAddress, state->flash.writesPerTick);
		nn_removeEnergy(C, state->flash.dataEnergyCost * ss);

		size_t len;
		const char *sector = nn_tolstring(C, 1, &len);
		if(len != ss) {
			nn_setError(C, "incorrect sector size");
			return NN_EBADCALL;
		}

		freq.action = NN_FLASH_WRITESECTOR;
		freq.writesector.sec = sec;
		freq.writesector.buf = sector;
		freq.writesector.writesAdded = nn_flash_writesAdded(ctx, &state->flash);
		e = state->handler(&freq);
		if(e) return e;

		request->returnCount = 1;
		return nn_pushbool(C, true);
	}

	if(C) nn_setError(C, "nandflash: not implemented yet");
	return NN_EBADCALL;
}

nn_Component *nn_createFlash(nn_Universe *universe, const char *address, const nn_NandFlash *drive, void *state, nn_FlashHandler *handler) {
	nn_Component *c = nn_createComponent(universe, address, "nandflash");
	if(c == NULL) return NULL;
	const nn_Method methods[NN_FLASHNUM_COUNT] = {
		[NN_FLASHNUM_GETCAPACITY] = {"getCapacity", "function(): integer - Get the capacity of the flash storage", NN_DIRECT},
		[NN_FLASHNUM_GETSECTORSIZE] = {"getSectorSize", "function(): integer - Get the logical sector size", NN_DIRECT},
		[NN_FLASHNUM_GETLAYERS] = {"getLayers", "function(): integer - Get the amount of bits in a cell", NN_DIRECT},
		[NN_FLASHNUM_GETWEARLEVEL] = {"getWearLevel", "function(): number - Gets a number from 0 to 100 indicitive of estimated drive damage", NN_DIRECT},
		[NN_FLASHNUM_ISRO] = {"isReadonly", "function(): boolean - Checks whether the NAND is read-only", NN_DIRECT},
		[NN_FLASHNUM_GETLABEL] = {"getLabel", "function(): string? - Get the label of the flash storage", NN_DIRECT},
		[NN_FLASHNUM_SETLABEL] = {"setLabel", "function(label?: string): string - Set the label, which may be truncated", NN_INDIRECT},
		[NN_FLASHNUM_READSECTOR] = {"readSector", "function(sector: integer): string - Read contents of a logical sector", NN_DIRECT},
		[NN_FLASHNUM_WRITESECTOR] = {"writeSector", "function(sector: integer): string - Write contents of a logical sector, may lead to multiple real writes", NN_DIRECT},
		[NN_FLASHNUM_READBYTE] = {"readByte", "function(byte: integer): integer - Read an individual signed byte", NN_DIRECT},
		[NN_FLASHNUM_READUBYTE] = {"readUByte", "function(byte: integer): integer - Read an individual unsigned byte", NN_DIRECT},
		[NN_FLASHNUM_WRITEBYTE] = {"writeByte", "function(byte: integer, value: integer): boolean - Write a byte"},
	};
	nn_Exit e = nn_setComponentMethodsArray(c, methods, NN_FLASHNUM_COUNT);
	if(e) {
		nn_dropComponent(c);
		return NULL;
	}
	nn_Context *ctx = &universe->ctx;
	nn_FlashState *drvstate = nn_alloc(ctx, sizeof(*drvstate));
	if(drvstate == NULL) {
		nn_dropComponent(c);
		return NULL;
	}
	drvstate->ctx = ctx;
	drvstate->flash = *drive;
	drvstate->handler = handler;
	nn_setComponentState(c, state);
	nn_setComponentClassState(c, drvstate);
	nn_setComponentHandler(c, nn_flashHandler);
	return c;
}

bool nn_mergeFlash(nn_NandFlash *merged, const nn_NandFlash *flash, size_t len) {
	if(len == 0) return false;
	*merged = flash[0];
	for(size_t i = 1; i < len; i++) {
		nn_NandFlash f = flash[i];

		merged->readsPerTick += f.readsPerTick;
		merged->writesPerTick += f.writesPerTick;
		merged->dataEnergyCost += f.dataEnergyCost;
		merged->capacity += f.capacity;
		merged->maxWriteCount += f.maxWriteCount;
		merged->maxWriteAmplification += f.maxWriteAmplification;
		merged->writeAmplificationExponent += f.writeAmplificationExponent;
		merged->cellLevel += f.cellLevel;
	}
	merged->readsPerTick /= len;
	merged->writesPerTick /= len;
	merged->dataEnergyCost /= len;
	merged->maxWriteCount /= len;
	merged->maxWriteAmplification /= len;
	merged->writeAmplificationExponent /= len;
	merged->cellLevel /= len;
	return true;
}

typedef enum nn_ScreenNum {
    NN_SCRNUM_ISON,
    NN_SCRNUM_TURNON,
    NN_SCRNUM_TURNOFF,
    NN_SCRNUM_GETASPECTRATIO,
    NN_SCRNUM_GETKEYBOARDS,
    NN_SCRNUM_SETPRECISE,
    NN_SCRNUM_ISPRECISE,
    NN_SCRNUM_SETTOUCHINVERTED,
    NN_SCRNUM_ISTOUCHINVERTED,
	NN_SCRNUM_MINBRIGHTNESS,
	NN_SCRNUM_MAXBRIGHTNESS,
	NN_SCRNUM_SETBRIGHTNESS,
	NN_SCRNUM_GETBRIGHTNESS,

    NN_SCRNUM_COUNT,
} nn_ScreenNum;

typedef struct nn_ScreenClassState {
    nn_Context *ctx;
    nn_ScreenConfig scrconf;
    nn_ScreenHandler *handler;
} nn_ScreenClassState;

static nn_Exit nn_screenHandler(nn_ComponentRequest *req) {
    if(req->action == NN_COMP_SIGNAL) return NN_OK;
    nn_Context *ctx = req->ctx;
    nn_ScreenClassState *cls = req->classState;
    nn_Computer *C = req->computer;

    // Feature-gated methods
    if(req->action == NN_COMP_CHECKMETHOD) {
        nn_ScreenNum m = req->methodIdx;
        if(m == NN_SCRNUM_SETPRECISE
           || m == NN_SCRNUM_ISPRECISE)
            req->methodEnabled =
                (cls->scrconf.features
                 & NN_SCRF_PRECISE) != 0;
        if(m == NN_SCRNUM_SETTOUCHINVERTED
           || m == NN_SCRNUM_ISTOUCHINVERTED)
            req->methodEnabled =
                (cls->scrconf.features
                 & NN_SCRF_TOUCHINVERTED) != 0;
        return NN_OK;
    }

    nn_ScreenRequest s;
    s.ctx = ctx;
    s.state = req->state;
    s.computer = C;
    s.screen = &cls->scrconf;

    if(req->action == NN_COMP_DROP) {
        s.action = NN_SCREEN_DROP;
        cls->handler(&s);
        nn_free(ctx, cls, sizeof(*cls));
        return NN_OK;
    }

    nn_ScreenNum m = req->methodIdx;
    nn_Exit e = NN_OK;

    if(m == NN_SCRNUM_ISON) {
        s.action = NN_SCREEN_ISON;
        e = cls->handler(&s);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushbool(C, s.power.isOn);
    }
    if(m == NN_SCRNUM_TURNON) {
        s.action = NN_SCREEN_TURNON;
        e = cls->handler(&s);
        if(e) return e;
        e = nn_pushbool(C, s.power.wasOn);
        if(e) return e;
        e = nn_pushbool(C, s.power.isOn);
        if(e) return e;
        req->returnCount = 2;
        return NN_OK;
    }
    if(m == NN_SCRNUM_TURNOFF) {
        s.action = NN_SCREEN_TURNOFF;
        e = cls->handler(&s);
        if(e) return e;
        e = nn_pushbool(C, s.power.wasOn);
        if(e) return e;
        e = nn_pushbool(C, s.power.isOn);
        if(e) return e;
        req->returnCount = 2;
        return NN_OK;
    }
    if(m == NN_SCRNUM_GETASPECTRATIO) {
        s.action = NN_SCREEN_GETASPECTRATIO;
        e = cls->handler(&s);
        if(e) return e;
        e = nn_pushinteger(C, s.aspect.w);
        if(e) return e;
        e = nn_pushinteger(C, s.aspect.h);
        if(e) return e;
        req->returnCount = 2;
        return NN_OK;
    }
    if(m == NN_SCRNUM_GETKEYBOARDS) {
        // handler pushes addresses onto C's stack
        s.action = NN_SCREEN_GETKEYBOARDS;
        e = cls->handler(&s);
        if(e) return e;
        req->returnCount = 1;
        return nn_pusharraytable(C, s.kbCount);
    }
    if(m == NN_SCRNUM_SETPRECISE) {
        if(nn_checkboolean(C, 0,
            "bad argument #1 (boolean expected)"))
            return NN_EBADCALL;
        s.action = NN_SCREEN_SETPRECISE;
        s.flag = nn_toboolean(C, 0);
        e = cls->handler(&s);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushbool(C, s.flag);
    }
    if(m == NN_SCRNUM_ISPRECISE) {
        s.action = NN_SCREEN_ISPRECISE;
        e = cls->handler(&s);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushbool(C, s.flag);
    }
    if(m == NN_SCRNUM_SETTOUCHINVERTED) {
        if(nn_checkboolean(C, 0,
            "bad argument #1 (boolean expected)"))
            return NN_EBADCALL;
        s.action = NN_SCREEN_SETTOUCHINVERTED;
        s.flag = nn_toboolean(C, 0);
        e = cls->handler(&s);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushbool(C, s.flag);
    }
    if(m == NN_SCRNUM_ISTOUCHINVERTED) {
        s.action = NN_SCREEN_ISTOUCHINVERTED;
        e = cls->handler(&s);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushbool(C, s.flag);
    }
	if(m == NN_SCRNUM_MINBRIGHTNESS) {
		req->returnCount = 1;
		return nn_pushinteger(C, cls->scrconf.minBrightness * 100);
	}
	if(m == NN_SCRNUM_MAXBRIGHTNESS) {
		req->returnCount = 1;
		return nn_pushinteger(C, cls->scrconf.maxBrightness * 100);
	}
	if(m == NN_SCRNUM_GETBRIGHTNESS) {
		s.action = NN_SCREEN_GETBRIGHT;
		e = cls->handler(&s);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushnumber(C, s.brightness * 100);
	}
	if(m == NN_SCRNUM_SETBRIGHTNESS) {
		if(nn_checknumber(C, 0, "bad argument #1 (number expected)")) return NN_EBADCALL;
		s.action = NN_SCREEN_SETBRIGHT;
		s.brightness = nn_tonumber(C, 0) / 100;
		if(s.brightness < cls->scrconf.minBrightness) s.brightness = cls->scrconf.minBrightness;
		if(s.brightness > cls->scrconf.maxBrightness) s.brightness = cls->scrconf.maxBrightness;
		e = cls->handler(&s);
		if(e) return e;
		return nn_pushnumber(C, s.brightness * 100);
	}

    nn_setError(C, "screen: not implemented");
    return NN_EBADCALL;
}

// Replace nn_createScreen entirely:
nn_Component *nn_createScreen(
    nn_Universe *universe, const char *address,
    const nn_ScreenConfig *scrconf, void *state,
    nn_ScreenHandler *handler)
{
    nn_Component *c = nn_createComponent(
        universe, address, "screen");
    if(c == NULL) return NULL;

    const nn_Method methods[NN_SCRNUM_COUNT] = {
        [NN_SCRNUM_ISON] = {
            "isOn",
            "function(): boolean - Returns whether the screen is on",
            NN_DIRECT},
        [NN_SCRNUM_TURNON] = {
            "turnOn",
            "function(): boolean, boolean - Turn on",
            NN_INDIRECT},
        [NN_SCRNUM_TURNOFF] = {
            "turnOff",
            "function(): boolean, boolean - Turn off",
            NN_INDIRECT},
        [NN_SCRNUM_GETASPECTRATIO] = {
            "getAspectRatio",
            "function(): integer, integer - Block ratio",
            NN_DIRECT},
        [NN_SCRNUM_GETKEYBOARDS] = {
            "getKeyboards",
            "function(): string[] - Gets the keyboards attached to the screen",
            NN_DIRECT},
        [NN_SCRNUM_SETPRECISE] = {
            "setPrecise",
            "function(on: boolean): boolean - Enable/disable high-precision mouse events",
            NN_DIRECT},
        [NN_SCRNUM_ISPRECISE] = {
            "isPrecise",
            "function():boolean -- Returns whether high-precision mouse events are enabled",
            NN_DIRECT},
        [NN_SCRNUM_SETTOUCHINVERTED] = {
            "setTouchModeInverted",
            "function(on: boolean): boolean - Enables/disables inverse touch mode, which changes how the user interacts with the screen",
            NN_DIRECT},
        [NN_SCRNUM_ISTOUCHINVERTED] = {
            "isTouchModeInverted",
            "function(): boolean - Returns whether inverse touch mode is enabled",
            NN_DIRECT},
        [NN_SCRNUM_MINBRIGHTNESS] = {"minBrightness", "function(): number - Returns the minimum brightness", NN_DIRECT},
        [NN_SCRNUM_MAXBRIGHTNESS] = {"maxBrightness", "function(): number - Returns the maximum brightness", NN_DIRECT},
        [NN_SCRNUM_GETBRIGHTNESS] = {"getBrightness", "function(): number - Returns the current brightness", NN_DIRECT},
        [NN_SCRNUM_SETBRIGHTNESS] = {"setBrightness", "function(brightness: number): number - Sets the brightness, returns the new one", NN_DIRECT},
    };

    nn_Exit e = nn_setComponentMethodsArray(
        c, methods, NN_SCRNUM_COUNT);
    if(e) { nn_dropComponent(c); return NULL; }

    nn_Context *ctx = &universe->ctx;
    nn_ScreenClassState *cls =
        nn_alloc(ctx, sizeof(*cls));
    if(cls == NULL) {
        nn_dropComponent(c);
        return NULL;
    }
    cls->ctx = ctx;
    cls->scrconf = *scrconf;
    cls->handler = handler;
    nn_setComponentState(c, state);
    nn_setComponentClassState(c, cls);
    nn_setComponentHandler(c, nn_screenHandler);
    return c;
}

typedef enum nn_GPUNum {
    NN_GPUNUM_BIND,
    NN_GPUNUM_GETSCREEN,
    NN_GPUNUM_GETBG,
    NN_GPUNUM_SETBG,
    NN_GPUNUM_GETFG,
    NN_GPUNUM_SETFG,
    NN_GPUNUM_GETPALETTE,
    NN_GPUNUM_SETPALETTE,
    NN_GPUNUM_MAXDEPTH,
    NN_GPUNUM_GETDEPTH,
    NN_GPUNUM_SETDEPTH,
    NN_GPUNUM_MAXRES,
    NN_GPUNUM_GETRES,
    NN_GPUNUM_SETRES,
    NN_GPUNUM_GETVIEWPORT,
    NN_GPUNUM_SETVIEWPORT,
    NN_GPUNUM_GET,
    NN_GPUNUM_SET,
    NN_GPUNUM_COPY,
    NN_GPUNUM_FILL,
    NN_GPUNUM_GETACTIVEBUF,
    NN_GPUNUM_SETACTIVEBUF,
    NN_GPUNUM_BUFFERS,
    NN_GPUNUM_ALLOCBUF,
    NN_GPUNUM_FREEBUF,
    NN_GPUNUM_FREEALLBUFS,
    NN_GPUNUM_TOTALMEM,
    NN_GPUNUM_FREEMEM,
    NN_GPUNUM_GETBUFSIZE,
    NN_GPUNUM_BITBLT,
    NN_GPUNUM_COUNT,
} nn_GPUNum;

typedef struct nn_GPUClassState {
    nn_Context *ctx;
    nn_GPU gpu;
    nn_GPUHandler *handler;
} nn_GPUClassState;

static nn_Exit nn_gpuHandler(nn_ComponentRequest *req) {
    if(req->action == NN_COMP_CHECKMETHOD) return NN_OK;
    if(req->action == NN_COMP_SIGNAL) return NN_OK;
    nn_Context *ctx = req->ctx;
    nn_GPUClassState *cls = req->classState;
    nn_Computer *C = req->computer;

    nn_GPURequest g;
    g.ctx = ctx;
    g.state = req->state;
    g.computer = C;
    g.gpu = &cls->gpu;

    if(req->action == NN_COMP_DROP) {
        g.action = NN_GPU_DROP;
        cls->handler(&g);
        nn_free(ctx, cls, sizeof(*cls));
        return NN_OK;
    }

    nn_GPUNum m = req->methodIdx;
    nn_Exit e = NN_OK;
	
	g.action = NN_GPU_GETACTIVEBUF;
	e = cls->handler(&g);
	if(e) return e;
	int activeBuf = g.buffer.index;
	bool isScreen = activeBuf == 0;

    //  bind 
    if(m == NN_GPUNUM_BIND) {
        if(nn_checkstring(C, 0,
            "bad argument #1 (string expected)"))
            return NN_EBADCALL;
        e = nn_defaultboolean(C, 1, true);
        if(e) return e;
        g.action = NN_GPU_BIND;
        g.bind.address = nn_tostring(C, 0);
        g.bind.reset = nn_toboolean(C, 1);
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushbool(C, true);
    }
    //  getScreen 
    if(m == NN_GPUNUM_GETSCREEN) {
        g.action = NN_GPU_GETSCREEN;
        g.screenAddr[0] = '\0';
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        if(g.screenAddr[0] == '\0')
            return nn_pushnull(C);
        return nn_pushstring(C, g.screenAddr);
    }
    //  getBackground 
    if(m == NN_GPUNUM_GETBG) {
        g.action = NN_GPU_GETBG;
        e = cls->handler(&g);
        if(e) return e;
        e = nn_pushinteger(C, g.color.color);
        if(e) return e;
        e = nn_pushbool(C, g.color.isPalette);
        if(e) return e;
        req->returnCount = 2;
        return NN_OK;
    }
    //  setBackground 
    if(m == NN_GPUNUM_SETBG) {
		if(isScreen) nn_costComponent(C, req->compAddress, cls->gpu.setBackgroundPerTick);
        if(nn_checknumber(C, 0,
            "bad argument #1 (number expected)"))
            return NN_EBADCALL;
        e = nn_defaultboolean(C, 1, false);
        if(e) return e;
        g.action = NN_GPU_SETBG;
        g.color.color = nn_tointeger(C, 0);
        g.color.isPalette = nn_toboolean(C, 1);
        e = cls->handler(&g);
        if(e) return e;
        e = nn_pushinteger(C, g.color.oldColor);
        if(e) return e;
        req->returnCount = 1;
        if(g.color.oldPaletteIdx >= 0) {
            e = nn_pushinteger(C, g.color.oldPaletteIdx);
            if(e) return e;
            req->returnCount = 2;
        }
        return NN_OK;
    }
    //  getForeground 
    if(m == NN_GPUNUM_GETFG) {
        g.action = NN_GPU_GETFG;
        e = cls->handler(&g);
        if(e) return e;
        e = nn_pushinteger(C, g.color.color);
        if(e) return e;
        e = nn_pushbool(C, g.color.isPalette);
        if(e) return e;
        req->returnCount = 2;
        return NN_OK;
    }
    //  setForeground 
    if(m == NN_GPUNUM_SETFG) {
		if(isScreen) nn_costComponent(C, req->compAddress, cls->gpu.setForegroundPerTick);
        if(nn_checknumber(C, 0,
            "bad argument #1 (number expected)"))
            return NN_EBADCALL;
        e = nn_defaultboolean(C, 1, false);
        if(e) return e;
        g.action = NN_GPU_SETFG;
        g.color.color = nn_tointeger(C, 0);
        g.color.isPalette = nn_toboolean(C, 1);
        e = cls->handler(&g);
        if(e) return e;
        e = nn_pushinteger(C, g.color.oldColor);
        if(e) return e;
        req->returnCount = 1;
        if(g.color.oldPaletteIdx >= 0) {
            e = nn_pushinteger(C, g.color.oldPaletteIdx);
            if(e) return e;
            req->returnCount = 2;
        }
        return NN_OK;
    }
    //  getPaletteColor 
    if(m == NN_GPUNUM_GETPALETTE) {
        if(nn_checkinteger(C, 0,
            "bad argument #1 (number expected)"))
            return NN_EBADCALL;
        g.action = NN_GPU_GETPALETTE;
        g.palette.index = nn_tointeger(C, 0);
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushinteger(C, g.palette.color);
    }
    //  setPaletteColor 
    if(m == NN_GPUNUM_SETPALETTE) {
        if(nn_checkinteger(C, 0,
            "bad argument #1 (number expected)"))
            return NN_EBADCALL;
        if(nn_checkinteger(C, 1,
            "bad argument #2 (number expected)"))
            return NN_EBADCALL;
        g.action = NN_GPU_SETPALETTE;
        g.palette.index = nn_tointeger(C, 0);
        g.palette.color = nn_tointeger(C, 1);
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushinteger(C, g.palette.oldColor);
    }
    //  maxDepth 
    if(m == NN_GPUNUM_MAXDEPTH) {
        g.action = NN_GPU_MAXDEPTH;
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushinteger(C, g.depth.depth);
    }
    //  getDepth 
    if(m == NN_GPUNUM_GETDEPTH) {
        g.action = NN_GPU_GETDEPTH;
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushinteger(C, g.depth.depth);
    }
    //  setDepth 
    if(m == NN_GPUNUM_SETDEPTH) {
        if(nn_checkinteger(C, 0,
            "bad argument #1 (number expected)"))
            return NN_EBADCALL;
        g.action = NN_GPU_SETDEPTH;
        g.depth.depth = (char)nn_tointeger(C, 0);
        e = cls->handler(&g);
        if(e) return e;
        const char *name = nn_depthName(g.depth.oldDepth);
        if(name == NULL) name = "Unknown";
        req->returnCount = 1;
        e = nn_pushstring(C, name);
		if(e) return e;
		return nn_pushinteger(C, g.depth.oldDepth);
    }
    //  maxResolution 
    if(m == NN_GPUNUM_MAXRES) {
        g.action = NN_GPU_MAXRES;
        e = cls->handler(&g);
        if(e) return e;
        e = nn_pushinteger(C, g.resolution.width);
        if(e) return e;
        e = nn_pushinteger(C, g.resolution.height);
        if(e) return e;
        req->returnCount = 2;
        return NN_OK;
    }
    //  getResolution 
    if(m == NN_GPUNUM_GETRES) {
        g.action = NN_GPU_GETRES;
        e = cls->handler(&g);
        if(e) return e;
        e = nn_pushinteger(C, g.resolution.width);
        if(e) return e;
        e = nn_pushinteger(C, g.resolution.height);
        if(e) return e;
        req->returnCount = 2;
        return NN_OK;
    }
    //  setResolution 
    if(m == NN_GPUNUM_SETRES) {
        if(nn_checkinteger(C, 0,
            "bad argument #1 (number expected)"))
            return NN_EBADCALL;
        if(nn_checkinteger(C, 1,
            "bad argument #2 (number expected)"))
            return NN_EBADCALL;
        g.action = NN_GPU_SETRES;
        g.resolution.width = nn_tointeger(C, 0);
        g.resolution.height = nn_tointeger(C, 1);
        e = cls->handler(&g);
        if(e) return e;
        // signal is best-effort, resolution is
        // already changed at this point
        nn_GPURequest s = g;
        s.action = NN_GPU_GETSCREEN;
        s.screenAddr[0] = '\0';
        if(cls->handler(&s) == NN_OK
           && s.screenAddr[0] != '\0') {
            nn_pushScreenResized(C, s.screenAddr,
                g.resolution.width,
                g.resolution.height);
        }
        req->returnCount = 1;
        return nn_pushbool(C, true);
    }
    //  getViewport 
    if(m == NN_GPUNUM_GETVIEWPORT) {
        g.action = NN_GPU_GETVIEWPORT;
        e = cls->handler(&g);
        if(e) return e;
        e = nn_pushinteger(C, g.resolution.width);
        if(e) return e;
        e = nn_pushinteger(C, g.resolution.height);
        if(e) return e;
        req->returnCount = 2;
        return NN_OK;
    }
    //  setViewport 
    if(m == NN_GPUNUM_SETVIEWPORT) {
        if(nn_checkinteger(C, 0,
            "bad argument #1 (number expected)"))
            return NN_EBADCALL;
        if(nn_checkinteger(C, 1,
            "bad argument #2 (number expected)"))
            return NN_EBADCALL;
        g.action = NN_GPU_SETVIEWPORT;
        g.resolution.width = nn_tointeger(C, 0);
        g.resolution.height = nn_tointeger(C, 1);
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushbool(C, true);
    }
    //  get 
    if(m == NN_GPUNUM_GET) {
        if(nn_checkinteger(C, 0,
            "bad argument #1 (number expected)"))
            return NN_EBADCALL;
        if(nn_checkinteger(C, 1,
            "bad argument #2 (number expected)"))
            return NN_EBADCALL;
        g.action = NN_GPU_GET;
        g.get.x = nn_tointeger(C, 0);
        g.get.y = nn_tointeger(C, 1);
        e = cls->handler(&g);
        if(e) return e;
        char buf[NN_MAX_UNICODE_BUFFER];
        size_t len = nn_unicode_codepointToChar(
            buf, g.get.codepoint);
        e = nn_pushlstring(C, buf, len);
        if(e) return e;
        e = nn_pushinteger(C, g.get.fg);
        if(e) return e;
        e = nn_pushinteger(C, g.get.bg);
        if(e) return e;
        req->returnCount = 3;
        if(g.get.fgIdx >= 0) {
            e = nn_pushinteger(C, g.get.fgIdx);
            if(e) return e;
        } else {
            e = nn_pushnull(C);
            if(e) return e;
        }
        if(g.get.bgIdx >= 0) {
            e = nn_pushinteger(C, g.get.bgIdx);
            if(e) return e;
        } else {
            e = nn_pushnull(C);
            if(e) return e;
        }
        req->returnCount = 5;
        return NN_OK;
    }
    //  set 
    if(m == NN_GPUNUM_SET) {
        if(nn_checkinteger(C, 0,
            "bad argument #1 (number expected)"))
            return NN_EBADCALL;
        if(nn_checkinteger(C, 1,
            "bad argument #2 (number expected)"))
            return NN_EBADCALL;
        if(nn_checkstring(C, 2,
            "bad argument #3 (string expected)"))
            return NN_EBADCALL;
        e = nn_defaultboolean(C, 3, false);
        if(e) return e;
        g.action = NN_GPU_SET;
        g.set.x = nn_tointeger(C, 0);
        g.set.y = nn_tointeger(C, 1);
        g.set.value = nn_tolstring(C, 2, &g.set.len);
        g.set.vertical = nn_toboolean(C, 3);
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
		if(isScreen) {
			nn_costComponent(C, req->compAddress, cls->gpu.setPerTick);
			nn_removeEnergy(C, cls->gpu.energyPerWrite * g.set.len);
		}
        return nn_pushbool(C, true);
    }
    //  copy 
    if(m == NN_GPUNUM_COPY) {
        for(int i = 0; i < 6; i++) {
            if(nn_checkinteger(C, i,
                "bad argument (number expected)"))
                return NN_EBADCALL;
        }
        g.action = NN_GPU_COPY;
        g.copy.x  = nn_tointeger(C, 0);
        g.copy.y  = nn_tointeger(C, 1);
        g.copy.w  = nn_tointeger(C, 2);
        g.copy.h  = nn_tointeger(C, 3);
        g.copy.tx = nn_tointeger(C, 4);
        g.copy.ty = nn_tointeger(C, 5);
		// prevent issues
		if(g.copy.w < 0) g.copy.w = 0;
		if(g.copy.w > g.gpu->maxWidth) g.copy.w = g.gpu->maxWidth;
		if(g.copy.h < 0) g.copy.h = 0;
		if(g.copy.h > g.gpu->maxHeight) g.copy.h = g.gpu->maxHeight;
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
		if(isScreen) {
			nn_costComponent(C, req->compAddress, cls->gpu.copyPerTick);
			nn_removeEnergy(C, cls->gpu.energyPerWrite * g.copy.w  * g.copy.h);
		}
        return nn_pushbool(C, true);
    }
    //  fill 
    if(m == NN_GPUNUM_FILL) {
        for(int i = 0; i < 4; i++) {
            if(nn_checkinteger(C, i,
                "bad argument (number expected)"))
                return NN_EBADCALL;
        }
        if(nn_checkstring(C, 4,
            "bad argument #5 (string expected)"))
            return NN_EBADCALL;
        g.action = NN_GPU_FILL;
        g.fill.x = nn_tointeger(C, 0);
        g.fill.y = nn_tointeger(C, 1);
        g.fill.w = nn_tointeger(C, 2);
        g.fill.h = nn_tointeger(C, 3);
		// prevent issues
		if(g.fill.w < 0) g.fill.w = 0;
		if(g.fill.w > g.gpu->maxWidth) g.fill.w = g.gpu->maxWidth;
		if(g.fill.h < 0) g.fill.h = 0;
		if(g.fill.h > g.gpu->maxHeight) g.fill.h = g.gpu->maxHeight;
        g.fill.codepoint = nn_unicode_firstCodepoint(
            nn_tostring(C, 4));
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
		if(isScreen) {
			nn_costComponent(C, req->compAddress, cls->gpu.fillPerTick);
			nn_removeEnergy(C, (g.fill.codepoint == ' ' ? cls->gpu.energyPerClear : cls->gpu.energyPerWrite) * g.fill.w * g.fill.h);
		}
        return nn_pushbool(C, true);
    }
    //  VRAM: getActiveBuffer 
    if(m == NN_GPUNUM_GETACTIVEBUF) {
        g.action = NN_GPU_GETACTIVEBUF;
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushinteger(C, g.buffer.index);
    }
    //  VRAM: setActiveBuffer 
    if(m == NN_GPUNUM_SETACTIVEBUF) {
        if(nn_checkinteger(C, 0,
            "bad argument #1 (number expected)"))
            return NN_EBADCALL;
        g.action = NN_GPU_SETACTIVEBUF;
        g.buffer.index = nn_tointeger(C, 0);
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushinteger(C, g.buffer.index);
    }
    //  VRAM: buffers 
    if(m == NN_GPUNUM_BUFFERS) {
        g.action = NN_GPU_BUFFERS;
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pusharraytable(C, g.bufCount);
    }
    //  VRAM: allocateBuffer 
    if(m == NN_GPUNUM_ALLOCBUF) {
        e = nn_defaultinteger(C, 0, 0);
        if(e) return e;
        e = nn_defaultinteger(C, 1, 0);
        if(e) return e;
        g.action = NN_GPU_ALLOCBUF;
        g.allocBuf.w = nn_tointeger(C, 0);
        g.allocBuf.h = nn_tointeger(C, 1);
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushinteger(C, g.allocBuf.index);
    }
    //  VRAM: freeBuffer 
    if(m == NN_GPUNUM_FREEBUF) {
        e = nn_defaultinteger(C, 0, 0);
        if(e) return e;
        g.action = NN_GPU_FREEBUF;
        g.buffer.index = nn_tointeger(C, 0);
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushbool(C, true);
    }
    //  VRAM: freeAllBuffers 
    if(m == NN_GPUNUM_FREEALLBUFS) {
        g.action = NN_GPU_FREEALLBUFS;
        e = cls->handler(&g);
        if(e) return e;
        return NN_OK;
    }
    //  VRAM: totalMemory 
    if(m == NN_GPUNUM_TOTALMEM) {
        req->returnCount = 1;
        return nn_pushinteger(C, cls->gpu.totalVRAM);
    }
    //  VRAM: freeMemory 
    if(m == NN_GPUNUM_FREEMEM) {
        g.action = NN_GPU_FREEMEM;
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushinteger(C, g.memory);
    }
    //  VRAM: getBufferSize 
    if(m == NN_GPUNUM_GETBUFSIZE) {
        e = nn_defaultinteger(C, 0, 0);
        if(e) return e;
        g.action = NN_GPU_GETBUFSIZE;
        g.bufSize.index = nn_tointeger(C, 0);
        e = cls->handler(&g);
        if(e) return e;
        e = nn_pushinteger(C, g.bufSize.w);
        if(e) return e;
        e = nn_pushinteger(C, g.bufSize.h);
        if(e) return e;
        req->returnCount = 2;
        return NN_OK;
    }
    //  VRAM: bitblt 
    if(m == NN_GPUNUM_BITBLT) {
        e = nn_defaultinteger(C, 0, 0);
        if(e) return e;
        for(int i = 1; i < 8; i++) {
            e = nn_defaultinteger(C, i, 0);
            if(e) return e;
        }
        g.action = NN_GPU_BITBLT;
        g.bitblt.dst     = nn_tointeger(C, 0);
        g.bitblt.col     = nn_tointeger(C, 1);
        g.bitblt.row     = nn_tointeger(C, 2);
        g.bitblt.w       = nn_tointeger(C, 3);
        g.bitblt.h       = nn_tointeger(C, 4);
        g.bitblt.src     = nn_tointeger(C, 5);
        g.bitblt.fromCol = nn_tointeger(C, 6);
        g.bitblt.fromRow = nn_tointeger(C, 7);
		if(g.bitblt.w < 0) g.copy.w = 0;
		if(g.bitblt.w > g.gpu->maxWidth) g.bitblt.w = g.gpu->maxWidth;
		if(g.bitblt.h < 0) g.copy.h = 0;
		if(g.bitblt.h > g.gpu->maxHeight) g.bitblt.h = g.gpu->maxHeight;
		if(g.bitblt.dst == 0 || g.bitblt.src == 0) {
			// taxed as a copy
			nn_costComponent(C, req->compAddress, g.gpu->copyPerTick);
			nn_removeEnergy(C, g.gpu->energyPerWrite * g.bitblt.w * g.bitblt.h);
		}
        e = cls->handler(&g);
        if(e) return e;
        req->returnCount = 1;
        return nn_pushbool(C, true);
    }

    nn_setError(C, "gpu: not implemented");
    return NN_EBADCALL;
}

// Replace nn_createGPU entirely:
nn_Component *nn_createGPU(
    nn_Universe *universe, const char *address,
    const nn_GPU *gpu, void *state,
    nn_GPUHandler *handler)
{
    nn_Component *c = nn_createComponent(
        universe, address, "gpu");
    if(c == NULL) return NULL;

    const nn_Method methods[NN_GPUNUM_COUNT] = {
        [NN_GPUNUM_BIND] = {
            "bind",
            "function(address: string, reset?:boolean): boolean - Attempts to bind the GPU to a screen",
            NN_INDIRECT},
        [NN_GPUNUM_GETSCREEN] = {
            "getScreen",
            "function(): string? - Get the bound screen, if any",
            NN_DIRECT},
        [NN_GPUNUM_GETBG] = {
            "getBackground",
            "function(): integer, boolean - Returns the current background, and whether its a palette index",
            NN_DIRECT},
        [NN_GPUNUM_SETBG] = {
            "setBackground",
            "function(color: integer, palette?: boolean): integer, integer? - Sets the current background, returns the old one",
            NN_DIRECT},
        [NN_GPUNUM_GETFG] = {
            "getForeground",
            "function(): integer,boolean - Returns the current foreground, and whether its a plette index",
            NN_DIRECT},
        [NN_GPUNUM_SETFG] = {
            "setForeground",
            "function(color: integer, palette: boolean): integer, integer? - Sets the current foreground, returns the old one",
            NN_DIRECT},
        [NN_GPUNUM_GETPALETTE] = {
            "getPaletteColor",
            "function(index: integer): integer - Returns a color from the palette",
            NN_DIRECT},
        [NN_GPUNUM_SETPALETTE] = {
            "setPaletteColor",
            "function(index: integer, value: integer): integer - Changes a color from the palette, returns the old one",
            NN_DIRECT},
        [NN_GPUNUM_MAXDEPTH] = {
            "maxDepth",
            "function(): integer - Returns the maximum supported color depth (by GPU and/or screen)",
            NN_DIRECT},
        [NN_GPUNUM_GETDEPTH] = {
            "getDepth",
            "function(): integer - Returns the current depth",
            NN_DIRECT},
        [NN_GPUNUM_SETDEPTH] = {
            "setDepth",
            "function(depth:integer): string, integer - Change the current depth, returns the name of the old one, and its value",
            NN_DIRECT},
        [NN_GPUNUM_MAXRES] = {
            "maxResolution",
            "function(): integer, integer - Retuns the maximum supported resolution (by GPU and/or screen)",
            NN_DIRECT},
        [NN_GPUNUM_GETRES] = {
            "getResolution",
            "function(): integer, integer - Returns the current screen resolution",
            NN_DIRECT},
        [NN_GPUNUM_SETRES] = {
            "setResolution",
            "function(width: integer, height: integer): boolean - Changes the current screen resolution",
            NN_DIRECT},
        [NN_GPUNUM_GETVIEWPORT] = {
            "getViewport",
            "function(): integer, integer - Get the current viewport, the region of the screen that can actually be seen",
            NN_DIRECT},
        [NN_GPUNUM_SETVIEWPORT] = {
            "setViewport",
            "function(width: integer, height: integer): boolean - Change the viewport to a new size",
            NN_DIRECT},
        [NN_GPUNUM_GET] = {
            "get",
            "function(x: integer, y: integer): string, integer, integer, integer?, integer? - Get the character, foreground, background, foreground index and "
			"background index of a pixel",
            NN_DIRECT},
        [NN_GPUNUM_SET] = {
            "set",
            "function(x: integer, y: integer, s: string, vertical?: boolean): boolean - Set a horizontal/vertical line of text at a given (x,y) coordinate.",
            NN_DIRECT},
        [NN_GPUNUM_COPY] = {
            "copy",
            "function(x: integer, y: integer, w: integer, h: integer, tx: integer, ty: integer): boolean - Copy a region on the screen. (tx, ty) is relative "
			"to the top-left corner",
            NN_DIRECT},
        [NN_GPUNUM_FILL] = {
            "fill",
            "function(x: integer, y: integer, w: integer, h: integer, char: string): boolean - Fill a rectangle with a specific character",
            NN_DIRECT},
        [NN_GPUNUM_GETACTIVEBUF] = {
            "getActiveBuffer",
            "function(): integer - Get the current active buffer index, 0 means the bound screen. May return 0 even when there is no screen",
            NN_DIRECT},
        [NN_GPUNUM_SETACTIVEBUF] = {
            "setActiveBuffer",
            "function(index: integer): integer - Set the active buffer, returns the old one",
            NN_DIRECT},
        [NN_GPUNUM_BUFFERS] = {
            "buffers",
            "function(): integer[] - Returns the list of VRAM buffers; this never includes 0, as it is the screen",
            NN_DIRECT},
        [NN_GPUNUM_ALLOCBUF] = {
            "allocateBuffer",
            "function(width: integer, height: integer): integer - Allocate a new VRAM buffer",
            NN_DIRECT},
        [NN_GPUNUM_FREEBUF] = {
            "freeBuffer",
            "function([index:number]):boolean"
            " -- Free VRAM page",
            NN_DIRECT},
        [NN_GPUNUM_FREEALLBUFS] = {
            "freeAllBuffers",
            "function() -- Free all VRAM pages",
            NN_DIRECT},
        [NN_GPUNUM_TOTALMEM] = {
            "totalMemory",
            "function(): integer - Returns the VRAM capacity, in pixels",
            NN_DIRECT},
        [NN_GPUNUM_FREEMEM] = {
            "freeMemory",
            "function(): integer - Returns the amount of unused VRAM, in pixels",
            NN_DIRECT},
        [NN_GPUNUM_GETBUFSIZE] = {
            "getBufferSize",
            "function(index?: integer): integer, integer - Returns buffer dimensions",
            NN_DIRECT},
        [NN_GPUNUM_BITBLT] = {
            "bitblt",
            "function(dst: integer, col: integer, row:integer, width:integer, height: integer, src: integer, fromCol: integer, fromRow: integer): boolean - "
            "Copy from buffer to buffer, buffer to screen or screen to buffer",
            NN_DIRECT},
    };

    nn_Exit e = nn_setComponentMethodsArray(
        c, methods, NN_GPUNUM_COUNT);
    if(e) { nn_dropComponent(c); return NULL; }

    nn_Context *ctx = &universe->ctx;
    nn_GPUClassState *cls = nn_alloc(ctx, sizeof(*cls));
    if(cls == NULL) {
        nn_dropComponent(c);
        return NULL;
    }
    cls->ctx = ctx;
    cls->gpu = *gpu;
    cls->handler = handler;
    nn_setComponentState(c, state);
    nn_setComponentClassState(c, cls);
    nn_setComponentHandler(c, nn_gpuHandler);
    return c;
}

typedef enum nn_ModemNum {
	NN_MODEMNUM_ISWIRED,
	NN_MODEMNUM_ISWIRELESS,
	NN_MODEMNUM_MAXPACKETSIZE,
	NN_MODEMNUM_MAXVALUES,

	NN_MODEMNUM_GETSTRENGTH,
	NN_MODEMNUM_SETSTRENGTH,
	NN_MODEMNUM_MAXSTRENGTH,

	NN_MODEMNUM_ISOPEN,
	NN_MODEMNUM_OPEN,
	NN_MODEMNUM_CLOSE,
	NN_MODEMNUM_GETPORTS,

	NN_MODEMNUM_SEND,
	NN_MODEMNUM_BROADCAST,

	NN_MODEMNUM_GETWAKE,
	NN_MODEMNUM_SETWAKE,

	NN_MODEMNUM_COUNT,
} nn_ModemNum;

typedef struct nn_ModemState {
    nn_Context *ctx;
    nn_Modem modem;
    nn_ModemHandler *handler;
} nn_ModemState;

static nn_Exit nn_modemHandler(nn_ComponentRequest *req) {
	if(req->action == NN_COMP_SIGNAL) return NN_OK;
	nn_Context *ctx = req->ctx;
	nn_ModemState *state = req->classState;
	nn_Computer *C = req->computer;
	
	bool isWired = state->modem.isWired;
	bool isWireless = state->modem.maxRange > 0;
	nn_ModemNum method = req->methodIdx;

	if(req->action == NN_COMP_CHECKMETHOD) {
		if(method == NN_MODEMNUM_GETSTRENGTH || method == NN_MODEMNUM_SETSTRENGTH || method == NN_MODEMNUM_MAXSTRENGTH) {
			req->methodEnabled = isWireless;
			return NN_OK;
		}
		return NN_OK;
	}

	nn_ModemRequest mreq;
	mreq.ctx = ctx;
	mreq.computer = C;
	mreq.state = req->state;
	mreq.modem = &state->modem;
	mreq.localAddress = req->compAddress;
	nn_Exit e;

	if(req->action == NN_COMP_DROP) {
		mreq.action = NN_MODEM_DROP;
		state->handler(&mreq);
		nn_free(ctx, state, sizeof(*state));
		return NN_OK;
	}

	if(method == NN_MODEMNUM_ISWIRED) {
		return nn_pushbool(C, isWired);
	}
	if(method == NN_MODEMNUM_ISWIRELESS) {
		return nn_pushbool(C, isWireless);
	}
	if(method == NN_MODEMNUM_MAXPACKETSIZE) {
		return nn_pushinteger(C, state->modem.maxPacketSize);
	}
	if(method == NN_MODEMNUM_MAXVALUES) {
		return nn_pushinteger(C, state->modem.maxValues);
	}
	if(method == NN_MODEMNUM_GETSTRENGTH) {
		mreq.action = NN_MODEM_GETSTRENGTH;
		e = state->handler(&mreq);
		if(e) return e;
		req->returnCount = 1;
		return nn_pushinteger(C, mreq.strength);
	}	
	if(method == NN_MODEMNUM_MAXSTRENGTH) {
		return nn_pushinteger(C, state->modem.maxRange);
	}

	if(C) nn_setError(C, "modem: not implemented yet");
	return NN_EBADCALL;
}

nn_Component *nn_createModem(nn_Universe *universe, const char *address, const nn_Modem *modem, void *state, nn_ModemHandler *handler) {
    nn_Component *c = nn_createComponent(
        universe, address, "gpu");
    if(c == NULL) return NULL;

    const nn_Method methods[NN_MODEMNUM_COUNT] = {
		[NN_MODEMNUM_ISWIRED] = {"isWired", "function(): boolean - Returns whether the modem supports wired connectivity", NN_DIRECT},
		[NN_MODEMNUM_ISWIRELESS] = {"isWireless", "function(): boolean - Returns whether the modem supports wireless connectivity", NN_DIRECT},
		[NN_MODEMNUM_MAXPACKETSIZE] = {"maxPacketSize", "function(): integer - Returns the maximum logical packet size", NN_DIRECT},
		[NN_MODEMNUM_MAXVALUES] = {"maxValues", "function(): integer - Returns the maximum amount of values", NN_DIRECT},

		[NN_MODEMNUM_GETSTRENGTH] = {"getStrength", "function(): integer - Returns the range of wireless message", NN_DIRECT},
		[NN_MODEMNUM_SETSTRENGTH] = {"setStrength", "function(strength: integer): integer - Changes the wireless signal strength", NN_INDIRECT},
		[NN_MODEMNUM_MAXSTRENGTH] = {"maxStrength", "function(): integer - Returns the maximum strength of wireless messages", NN_DIRECT},

		[NN_MODEMNUM_ISOPEN] = {"isOpen", "function(port: integer): boolean - Returns whether a port is open", NN_DIRECT},
		[NN_MODEMNUM_OPEN] = {"open", "function(port: integer): boolean - Open a port", NN_DIRECT},
		[NN_MODEMNUM_CLOSE] = {"close", "function(port?: integer): boolean - Close a port, or all ports if none specified", NN_DIRECT},
		[NN_MODEMNUM_GETPORTS] = {"getOpenPorts", "function(): integer[] - Returns a list of all open ports", NN_DIRECT},
	
		[NN_MODEMNUM_SEND] = {"send", "function(targetAddress: string, port: integer, ...): boolean - Send a packet", NN_INDIRECT},
		[NN_MODEMNUM_BROADCAST] = {"broadcast", "function(port: integer, ...): boolean - Broadcast a packet", NN_INDIRECT},
	
		[NN_MODEMNUM_GETWAKE] = {"getWakeMessage", "function(): string?, boolean - Returns the wake message, if any, and whether it is fuzzy", NN_DIRECT},
		[NN_MODEMNUM_SETWAKE] = {"setWakeMessage", "function(message: string?, fuzzy: boolean) - Changes the wake-up message of the modem", NN_INDIRECT},
    };

    nn_Exit e = nn_setComponentMethodsArray(
        c, methods, NN_MODEMNUM_COUNT);
    if(e) { nn_dropComponent(c); return NULL; }

    nn_Context *ctx = &universe->ctx;
    nn_ModemState *cls = nn_alloc(ctx, sizeof(*cls));
    if(cls == NULL) {
        nn_dropComponent(c);
        return NULL;
    }
    cls->ctx = ctx;
    cls->modem = *modem;
    cls->handler = handler;
    nn_setComponentState(c, state);
    nn_setComponentClassState(c, cls);
    nn_setComponentHandler(c, nn_modemHandler);
    return c;
}

nn_Modem nn_defaultWiredModem = {
	.maxRange = 0,
	.maxValues = 8,
	.maxPacketSize = 8192,
	.maxOpenPorts = 16,
	.isWired = true,
	.basePacketCost = 50,
	.fullPacketCost = 100,
	.costPerStrength = 0,
};
nn_Modem nn_defaultWirelessModems[2] = {
	NN_INIT(nn_Modem) {
		.maxRange = 16,
		.maxValues = 8,
		.maxPacketSize = 8192,
		.maxOpenPorts = 16,
		.isWired = true,
		.basePacketCost = 100,
		.fullPacketCost = 500,
		.costPerStrength = 30,
	},
	NN_INIT(nn_Modem) {
		.maxRange = 400,
		.maxValues = 8,
		.maxPacketSize = 8192,
		.maxOpenPorts = 16,
		.isWired = true,
		.basePacketCost = 200,
		.fullPacketCost = 1000,
		.costPerStrength = 20,
	},
};
