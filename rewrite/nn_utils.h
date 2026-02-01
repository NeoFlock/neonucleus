#ifndef NN_UTILS_H
#define NN_UTILS_H

#include "neonucleus.h"

void *nn_alloc(nn_Context *ctx, size_t size);
void *nn_realloc(nn_Context *ctx, void *memory, size_t oldSize, size_t newSize);
void nn_free(nn_Context *ctx, void *memory, size_t size);

size_t nn_strlen(const char *s);
char *nn_strdup(nn_Context *ctx, const char *s);
void nn_strfree(nn_Context *ctx, char *s);

void nn_memcpy(void *dest, const void *src, size_t len);
void nn_memset(void *dest, int x, size_t len);

typedef struct nn_Lock nn_Lock;

nn_Lock *nn_createLock(nn_Context *ctx);
void nn_destroyLock(nn_Context *ctx, nn_Lock *lock);

void nn_lock(nn_Context *ctx, nn_Lock *lock);
void nn_unlock(nn_Context *ctx, nn_Lock *lock);

double nn_currentTime(nn_Context *ctx);

size_t nn_rand(nn_Context *ctx);
// nn_rand but in the range of [0, 1)
double nn_randf(nn_Context *ctx);

// nn_rand but in the range of [0, 1] (aka both inclusive)
double nn_randfi(nn_Context *ctx);

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

#endif
