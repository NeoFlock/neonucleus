#include "neonucleus.h"
#include <stdlib.h>
#include <string.h>

#ifdef NN_POSIX
#include <time.h>
#else
#include <windows.h>
#endif

void *nn_malloc(size_t size) {
    return malloc(size);
}

void *nn_realloc(void *memory, size_t newSize) {
    return realloc(memory, newSize);
}

void nn_free(void *memory) {
    free(memory);
}

// Utilities, both internal and external
char *nn_strdup(const char *s) {
    size_t l = strlen(s);
    char *m = nn_malloc(l+1);
    if(m == NULL) return m;
    return strcpy(m, s);
}

void *nn_memdup(const void *buf, size_t len) {
    char *m = malloc(len);
    if(m == NULL) return m;
    return memcpy(m, buf, len);
}

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

double nn_realTimeClock(void *_) {
    return nn_realTime();
}

void nn_busySleep(double t) {
    double deadline = nn_realTime() + t;
    while(nn_realTime() < deadline) {}
}
