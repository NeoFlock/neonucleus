#include "neonucleus.h"
#include <string.h>

bool nn_unicode_validate(const char *s) {
    // TODO: validate UTF-8-ness
    return true;
}

// A general unicode library, which assumes unicode encoding.
// It is used to power the Lua architecture's Unicode API re-implementation.
// It can also just be used to deal with unicode.

char *nn_unicode_char(int *codepoints, size_t codepointCount) {
    size_t len = 0;
    for(size_t i = 0; i < codepointCount; i++) {
        int codepoint = codepoints[i];
        len += nn_unicode_codepointSize(codepoint);
    }

    char *buf = nn_malloc(len+1);
    if(buf == NULL) return buf;
    buf[len] = '\0';

    size_t j = 0;
    for(size_t i = 0; i < codepointCount; i++) {
        int codepoint = codepoints[i];
        size_t codepointLen = 0;
        const char *c = nn_unicode_codepointToChar(codepoint, &codepointLen);
        memcpy(buf + j, c, codepointLen);
        j += codepointLen;
    }

    return buf;
}

int *nn_unicode_codepoints(const char *s);

size_t nn_unicode_len(const char *s) {
    size_t count = 0;
    while (*s) {
        count += (*s++ & 0xC0) != 0x80;
    }
    return count;
}

int nn_unicode_codepointAt(const char *s, size_t byteOffset);

size_t nn_unicode_codepointSize(int codepoint);

const char *nn_unicode_codepointToChar(int codepoint, size_t *len);

size_t nn_unicode_charWidth(int codepoint);

size_t nn_unicode_wlen(const char *s);

void nn_unicode_upper(char *s);
void nn_unicode_lower(char *s);
