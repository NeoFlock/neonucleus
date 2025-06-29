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

    const unsigned char *text = b;

    int codepoint = 0x3f;   // Codepoint (defaults to '?')
    int octet = (unsigned char)(text[0]); // The first UTF8 octet

    if (octet <= 0x7f)
    {
        // Only one octet (ASCII range x00-7F)
        codepoint = text[0];
    }
    else if ((octet & 0xe0) == 0xc0)
    {
        // Two octets

        // [0]xC2-DF    [1]UTF8-tail(x80-BF)
        unsigned char octet1 = text[1];

        if ((octet1 == '\0') || ((octet1 >> 6) != 2)) { return codepoint; } // Unexpected sequence

        if ((octet >= 0xc2) && (octet <= 0xdf))
        {
            codepoint = ((octet & 0x1f) << 6) | (octet1 & 0x3f);
        }
    }
    else if ((octet & 0xf0) == 0xe0)
    {
        // Three octets
        unsigned char octet1 = text[1];
        unsigned char octet2 = '\0';

        if ((octet1 == '\0') || ((octet1 >> 6) != 2)) { return codepoint; } // Unexpected sequence

        octet2 = text[2];

        if ((octet2 == '\0') || ((octet2 >> 6) != 2)) { return codepoint; } // Unexpected sequence

        // [0]xE0    [1]xA0-BF       [2]UTF8-tail(x80-BF)
        // [0]xE1-EC [1]UTF8-tail    [2]UTF8-tail(x80-BF)
        // [0]xED    [1]x80-9F       [2]UTF8-tail(x80-BF)
        // [0]xEE-EF [1]UTF8-tail    [2]UTF8-tail(x80-BF)

        if (((octet == 0xe0) && !((octet1 >= 0xa0) && (octet1 <= 0xbf))) ||
            ((octet == 0xed) && !((octet1 >= 0x80) && (octet1 <= 0x9f)))) { return codepoint; }

        if ((octet >= 0xe0) && (octet <= 0xef))
        {
            codepoint = ((octet & 0xf) << 12) | ((octet1 & 0x3f) << 6) | (octet2 & 0x3f);
        }
    }
    else if ((octet & 0xf8) == 0xf0)
    {
        // Four octets
        if (octet > 0xf4) return codepoint;

        unsigned char octet1 = text[1];
        unsigned char octet2 = '\0';
        unsigned char octet3 = '\0';

        if ((octet1 == '\0') || ((octet1 >> 6) != 2)) { return codepoint; }  // Unexpected sequence

        octet2 = text[2];

        if ((octet2 == '\0') || ((octet2 >> 6) != 2)) { return codepoint; }  // Unexpected sequence

        octet3 = text[3];

        if ((octet3 == '\0') || ((octet3 >> 6) != 2)) { return codepoint; }  // Unexpected sequence

        // [0]xF0       [1]x90-BF       [2]UTF8-tail  [3]UTF8-tail
        // [0]xF1-F3    [1]UTF8-tail    [2]UTF8-tail  [3]UTF8-tail
        // [0]xF4       [1]x80-8F       [2]UTF8-tail  [3]UTF8-tail

        if (((octet == 0xf0) && !((octet1 >= 0x90) && (octet1 <= 0xbf))) ||
            ((octet == 0xf4) && !((octet1 >= 0x80) && (octet1 <= 0x8f)))) { return codepoint; } // Unexpected sequence

        if (octet >= 0xf0)
        {
            codepoint = ((octet & 0x7) << 18) | ((octet1 & 0x3f) << 12) | ((octet2 & 0x3f) << 6) | (octet3 & 0x3f);
        }
    }

    if (codepoint > 0x10ffff) codepoint = 0x3f;     // Codepoints after U+10ffff are invalid

    return codepoint;

/*
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
*/
}

size_t nn_unicode_codepointSize(unsigned int codepoint) {
    int size = 1;

    if (codepoint <= 0x7f)
    {
        size = 1;
    }
    else if (codepoint <= 0x7ff)
    {
        size = 2;
    }
    else if (codepoint <= 0xffff)
    {
        size = 3;
    }
    else if (codepoint <= 0x10ffff)
    {
        size = 4;
    }

    return size;

/*
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
*/
}

const char *nn_unicode_codepointToChar(unsigned int codepoint, size_t *len) {

    static char utf8[6] = { 0 };
    memset(utf8, 0, 6); // Clear static array
    int size = 0;       // Byte size of codepoint

    if (codepoint <= 0x7f)
    {
        utf8[0] = (char)codepoint;
        size = 1;
    }
    else if (codepoint <= 0x7ff)
    {
        utf8[0] = (char)(((codepoint >> 6) & 0x1f) | 0xc0);
        utf8[1] = (char)((codepoint & 0x3f) | 0x80);
        size = 2;
    }
    else if (codepoint <= 0xffff)
    {
        utf8[0] = (char)(((codepoint >> 12) & 0x0f) | 0xe0);
        utf8[1] = (char)(((codepoint >>  6) & 0x3f) | 0x80);
        utf8[2] = (char)((codepoint & 0x3f) | 0x80);
        size = 3;
    }
    else if (codepoint <= 0x10ffff)
    {
        utf8[0] = (char)(((codepoint >> 18) & 0x07) | 0xf0);
        utf8[1] = (char)(((codepoint >> 12) & 0x3f) | 0x80);
        utf8[2] = (char)(((codepoint >>  6) & 0x3f) | 0x80);
        utf8[3] = (char)((codepoint & 0x3f) | 0x80);
        size = 4;
    }

    *len = size;

    return utf8;
/*
    size_t codepointSize = nn_unicode_codepointSize(codepoint);
    *len = codepointSize;

    static char buffer[4];

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
*/
}

size_t nn_unicode_charWidth(unsigned int codepoint);

size_t nn_unicode_wlen(const char *s);

// NOT IMPLEMENTED YET
void nn_unicode_upper(char *s);
void nn_unicode_lower(char *s);
