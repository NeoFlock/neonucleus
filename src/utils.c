#include "neonucleus.h"

#ifndef NN_BAREMETAL

#ifdef NN_POSIX
#include <time.h>
#else
#include <time.h>
#include <windows.h>
#endif

#endif

void *nn_alloc(nn_Alloc *alloc, nn_size_t size) {
    if(size == 0) return alloc->proc;
    return alloc->proc(alloc->userdata, NULL, 0, size, NULL);
}

void *nn_resize(nn_Alloc *alloc, void *memory, nn_size_t oldSize, nn_size_t newSize) {
    if(oldSize == newSize) return memory;
    if(newSize == 0) {
        nn_dealloc(alloc, memory, oldSize);
        return alloc->proc;
    }
    if(memory == NULL) {
        return nn_alloc(alloc, newSize);
    }
    if(memory == alloc->proc) {
        if(newSize == 0) return memory;
        return nn_alloc(alloc, newSize);
    }
    return alloc->proc(alloc->userdata, memory, oldSize, newSize, NULL);
}

void nn_dealloc(nn_Alloc *alloc, void *memory, nn_size_t size) {
    if(memory == NULL) return; // matches free()
    if(memory == alloc->proc) return; // 0-sized memory
    alloc->proc(alloc->userdata, memory, size, 0, NULL);
}

#ifndef NN_BAREMETAL

#include <stdlib.h>

static void *nn_libcAllocProc(void *_, void *ptr, nn_size_t oldSize, nn_size_t newSize, void *__) {
    if(newSize == 0) {
        //printf("Freed %lu bytes from %p\n", oldSize, ptr);
        free(ptr);
        return NULL;
    } else {
        void *rptr = realloc(ptr, newSize);
        //printf("Allocated %lu bytes for %p\n", newSize - oldSize, rptr);
        return rptr;
    }
}

nn_Alloc nn_libcAllocator() {
    return (nn_Alloc) {
        .userdata = NULL,
        .proc = nn_libcAllocProc,
    };
}

static nn_size_t nni_rand(void *userdata) {
    return rand();
}

nn_Rng nn_libcRng() {
    srand(time(NULL));
    return (nn_Rng) {
        .userdata = NULL,
        .maximum = RAND_MAX,
        .proc = nni_rand,
    };
}

nn_Context nn_libcContext() {
    return (nn_Context) {
        .allocator = nn_libcAllocator(),
        .clock = nn_libcRealTime(),
        .lockManager = nn_libcMutex(),
        .rng = nn_libcRng(),
    };
}
#endif

// Utilities, both internal and external
char *nn_strdup(nn_Alloc *alloc, const char *s) {
    nn_size_t l = nn_strlen(s);
    char *m = nn_alloc(alloc, l+1);
    if(m == NULL) return m;
    return nn_strcpy(m, s);
}

void *nn_memdup(nn_Alloc *alloc, const void *buf, nn_size_t len) {
    char *m = nn_alloc(alloc, len);
    if(m == NULL) return m;
    nn_memcpy(m, buf, len);
    return m;
}

void nn_deallocStr(nn_Alloc *alloc, char *s) {
    if(s == NULL) return;
    nn_dealloc(alloc, s, nn_strlen(s)+1);
}

static void nni_randomHex(nn_Context *ctx, char *buf, nn_size_t len) {
    const char *hex = "0123456789abcdef";

    for(nn_size_t i = 0; i < len; i++) {
        int r = nn_rand(&ctx->rng) % 16;
        buf[i] = hex[r];
    }
}

nn_address nn_randomUUID(nn_Context *ctx) {
    nn_address addr = nn_alloc(&ctx->allocator, 37);
    if(addr == NULL) return NULL;
    nni_randomHex(ctx, addr + 0, 8);
    addr[8] = '-';
    nni_randomHex(ctx, addr + 9, 4);
    addr[13] = '-';
    nni_randomHex(ctx, addr + 14, 4);
    addr[18] = '-';
    nni_randomHex(ctx, addr + 19, 4);
    addr[23] = '-';
    nni_randomHex(ctx, addr + 24, 12);
    addr[36] = '\0';


    // UUIDv4 variant 1
    addr[14] = '4';
    addr[19] = '1';
    return addr;
}

nn_size_t nn_rand(nn_Rng *rng) {
    return rng->proc(rng->userdata);
}

// returns from 0 to 1 (inclusive)
double nn_randf(nn_Rng *rng) {
    double x = nn_rand(rng);
    return x / rng->maximum;
}

// returns from 0 to 1 (exclusive)
double nn_randfe(nn_Rng *rng) {
    double x = nn_rand(rng);
    if(x >= rng->maximum) return 0;
    return x / rng->maximum;
}

#ifndef NN_BAREMETAL

#ifdef NN_POSIX

static double nni_realTime() {
    struct timespec time;
    if(clock_gettime(CLOCK_MONOTONIC, &time) < 0) return 0; // oh no
    return time.tv_sec + ((double)time.tv_nsec) / 1e9;
}

#else

static double nni_realTime() {
    LARGE_INTEGER frequency = {0};
    if(!QueryPerformanceFrequency(&frequency)) return 0;

    LARGE_INTEGER now = {0};
    if(!QueryPerformanceCounter(&now)) return 0;

    return (double)now.QuadPart / frequency.QuadPart;
}

#endif

static double nni_realTimeClock(void *_) {
    return nni_realTime();
}

nn_Clock nn_libcRealTime() {
    return (nn_Clock) {
        .userdata = NULL,
        .proc = nni_realTimeClock,
    };
}

#endif

// TODO: use OKLAB the color space for more accurate results.

typedef struct nn_rgbColor {
    double r, g, b;
} nn_rgbColor;

nn_rgbColor nni_splitColorToRgb(int color) {
    double r = (color & 0xFF0000) >> 16;
    double g = (color & 0x00FF00) >> 8;
    double b = color & 0x0000FF;

    int max = 0xFF;
    return (nn_rgbColor) {
        .r = r / max,
        .g = g / max,
        .b = b / max,
    };
}

double nn_colorDistance(int colorA, int colorB) {
    if(colorA == colorB) return 0;
    nn_rgbColor a = nni_splitColorToRgb(colorA);
    nn_rgbColor b = nni_splitColorToRgb(colorB);

    nn_rgbColor delta;
    delta.r = a.r - b.r;
    delta.g = a.g - b.g;
    delta.b = a.b - b.b;

    return delta.r*delta.r + delta.g*delta.g + delta.b*delta.b;
}

int nn_mapColor(int color, int *palette, int paletteSize) {
    if(paletteSize <= 0) return color;
    int bestColor = palette[0];
    double fitness = nn_colorDistance(color, bestColor);

    for(int i = 1; i < paletteSize; i++) {
        double dist = nn_colorDistance(color, palette[i]);
        if(dist < fitness) {
            bestColor = palette[i];
            fitness = dist;
        }
        if(bestColor == color) return color;
    }
    return bestColor;
}

void nn_memset(void *buf, unsigned char byte, nn_size_t len) {
    unsigned char *bytes = buf;
    for(nn_size_t i = 0; i < len; i++) bytes[i] = byte;
}

void nn_memcpy(void *dest, const void *src, nn_size_t len) {
    char *destBytes = dest;
    const char *srcBytes = src;
    for(nn_size_t i = 0; i < len; i++) {
        destBytes[i] = srcBytes[i];
    }
}

char *nn_strcpy(char *dest, const char *src) {
    nn_size_t i = 0;
    while(src[i]) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
    return dest;
}

int nn_strcmp(const char *a, const char *b) {
    nn_size_t i = 0;
    while(NN_TRUE) {
        unsigned char ca = a[i];
        unsigned char cb = b[i];

        if(ca < cb) {
            return -1;
        }
        if(ca > cb) {
            return -1;
        }
        if(ca == 0 && cb == 0) { // reached terminator
            return 0;
        }
        i++;
    }
}

const char *nn_strchr(const char *str, int ch) {
    nn_size_t i = 0;
    while(NN_TRUE) {
        if(str[i] == ch) return str + i;
        if(str[i] == 0) return NULL;
        i++;
    }
}

nn_size_t nn_strlen(const char *a) {
    nn_size_t l = 0;
    while(a[l]) l++;
    return l;
}
