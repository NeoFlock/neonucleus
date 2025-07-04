#include "neonucleus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NN_BAREMETAL

#ifdef NN_POSIX
#include <time.h>
#else
#include <windows.h>
#endif

#endif

void *nn_alloc(nn_Alloc *alloc, size_t size) {
    if(size == 0) return alloc->proc;
    return alloc->proc(alloc->userdata, NULL, 0, size, NULL);
}

void *nn_resize(nn_Alloc *alloc, void *memory, size_t oldSize, size_t newSize) {
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

void nn_dealloc(nn_Alloc *alloc, void *memory, size_t size) {
    if(memory == NULL) return; // matches free()
    if(memory == alloc->proc) return; // 0-sized memory
    alloc->proc(alloc->userdata, memory, size, 0, NULL);
}

static void *nn_libcAllocProc(void *_, void *ptr, size_t oldSize, size_t newSize, void *__) {
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

// Utilities, both internal and external
char *nn_strdup(nn_Alloc *alloc, const char *s) {
    size_t l = strlen(s);
    char *m = nn_alloc(alloc, l+1);
    if(m == NULL) return m;
    return strcpy(m, s);
}

void *nn_memdup(nn_Alloc *alloc, const void *buf, size_t len) {
    char *m = nn_alloc(alloc, len);
    if(m == NULL) return m;
    return memcpy(m, buf, len);
}

void nn_deallocStr(nn_Alloc *alloc, char *s) {
    if(s == NULL) return;
    nn_dealloc(alloc, s, strlen(s)+1);
}

#ifdef NN_BAREMETAL

double nn_realTime() {
    return 0;
}

#else

#ifdef NN_POSIX

double nn_realTime() {
    struct timespec time;
    if(clock_gettime(CLOCK_MONOTONIC, &time) < 0) return 0; // oh no
    return time.tv_sec + ((double)time.tv_nsec) / 1e9;
}

#else

double nn_realTime() {
    LARGE_INTEGER frequency = {0};
    if(!QueryPerformanceFrequency(&frequency)) return 0;

    LARGE_INTEGER now = {0};
    if(!QueryPerformanceCounter(&now)) return 0;

    return (double)now.QuadPart / frequency.QuadPart;
}

#endif

#endif

double nn_realTimeClock(void *_) {
    return nn_realTime();
}

