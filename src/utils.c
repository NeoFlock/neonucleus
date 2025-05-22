#include "neonucleus.h"
#include <stdlib.h>
#include <string.h>

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
