#include "neonucleus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool nn_unicode_is_continuation(unsigned char byte) {
    return (byte >> 6) == 0b10;
}

bool nn_unicode_validate(const char *b) {
    // TODO: validate UTF-8-ness
    const unsigned char* s = (const unsigned char*)b;
    while (*s) {
        if(s[0] <= 0x7F) {
            s++;
        } else if((s[0] >> 5) == 0b110) {
            if (!nn_unicode_is_continuation(s[1])) {
                return false;
            }
            s += 2;
        } else if((s[0] >> 4) == 0b1110) {
            if (!nn_unicode_is_continuation(s[1])) {
                return false;
            }
            if (!nn_unicode_is_continuation(s[2])) {
                return false;
            }
            s += 3;
        } else if((s[0] >> 3) == 0b11110) {
            if (!nn_unicode_is_continuation(s[1])) {
                return false;
            }
            if (!nn_unicode_is_continuation(s[2])) {
                return false;
            }
            if (!nn_unicode_is_continuation(s[3])) {
                return false;
            }
            s += 4;
        } else {
            return false;
        }
    }
    return true;
}

// A general unicode library, which assumes unicode encoding.
// It is used to power the Lua architecture's Unicode API re-implementation.
// It can also just be used to deal with unicode.

char *nn_unicode_char(unsigned int *codepoints, size_t codepointCount) {
    size_t len = 0;
    for (size_t i = 0; i < codepointCount; i++) {
        unsigned int codepoint = codepoints[i];
        len += nn_unicode_codepointSize(codepoint);
    }

    char *buf = nn_malloc(len+1);
    if (buf == NULL) return buf;

    size_t j = 0;
    for (size_t i = 0; i < codepointCount; i++) {
        int codepoint = codepoints[i];
        size_t codepointLen = 0;
        const char *c = nn_unicode_codepointToChar(codepoint, &codepointLen);
        memcpy(buf + j, c, codepointLen);
        j += codepointLen;
    }
    buf[j] = '\0';
    assert(j == len); // better safe than sorry

    return buf;
}

unsigned int *nn_unicode_codepoints(const char *s) {
    size_t l = nn_unicode_len(s);
    unsigned int *buf = nn_malloc(sizeof(unsigned int) * l);
    if(buf == NULL) return NULL;
    size_t cur = 0;
    size_t bufidx = 0;
    while(s[cur] != 0) {
        unsigned int point = nn_unicode_codepointAt(s, cur);
        cur += nn_unicode_codepointSize(point);
        buf[bufidx++] = point;
    }
    return buf;
}

size_t nn_unicode_len(const char *b) {
    size_t count = 0;
    const unsigned char* s = (const unsigned char*)b;
    while (*s) {
        count++;
        if(s[0] <= 0x7F) {
            s++;
        } else if((s[0] >> 5) == 0b110) {
            s += 2;
        } else if((s[0] >> 4) == 0b1110) {
            s += 3;
        } else if((s[0] >> 3) == 0b11110) {
            s += 4;
        }
    }
    return count;
}

unsigned int nn_unicode_codepointAt(const char *s, size_t byteOffset) {
    unsigned int point = 0;
    const unsigned char *b = (const unsigned char *)s + byteOffset;

    const unsigned char subpartMask = 0b111111;
    // look into nn_unicode_codepointToChar as well.
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

size_t nn_unicode_codepointSize(unsigned int codepoint) {
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

const char *nn_unicode_codepointToChar(unsigned int codepoint, size_t *len) {
    size_t codepointSize = nn_unicode_codepointSize(codepoint);
    *len = codepointSize;

    static char buffer[4];
    memset(buffer, 0, 4); // Clear static array

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

    return buffer;
}

// NOT IMPLEMENTED YET

size_t nn_unicode_charWidth(unsigned int codepoint);

size_t nn_unicode_wlen(const char *s);

unsigned int nn_unicode_upperCodepoint(unsigned int codepoint);
char *nn_unicode_upper(const char *s);
unsigned int nn_unicode_lowerCodepoint(unsigned int codepoint);
char *nn_unicode_lower(const char *s);
