#include <raylib.h>
#include "glyphcache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 *  Dynamic glyph cache for raylib
 *      (C) - "Raylib's text renderer handles unicode like garbage"
 *  Problem: raylib's LoadFont only loads ~95 ASCII glyphs.
 *  LoadFontEx can load arbitrary codepoints, but you must know
 *  them upfront, and rebuilding every frame is expensive.
 *
 *  So we should lazily collect codepoints the screen actually uses,
 *  rebuild the font atlas only when new ones appear.
 *  Typically this stabilises after the first few frames.
 * 
*/

#define GC_INITIAL_CAP   4096
#define GC_BUCKET_COUNT  8192   // must be power of 2
#define GC_BUCKET_MASK   (GC_BUCKET_COUNT - 1)

// Codepoint set (open-addressing hash set)

typedef struct {
    int     *slots;          // 0 = empty sentinel (U+0000 never needed)
    size_t   count;
    size_t   cap;            // always a power of 2
} CpSet;

static void cpset_init(CpSet *s) {
    s->cap   = GC_BUCKET_COUNT;
    s->count = 0;
    s->slots = calloc(s->cap, sizeof(int));
}

static void cpset_free(CpSet *s) {
    free(s->slots);
    s->slots = NULL;
    s->count = 0;
}

static bool cpset_contains(const CpSet *s, int cp) {
    size_t idx = (unsigned)cp & (s->cap - 1);
    for(size_t i = 0; i < s->cap; i++) {
        size_t j = (idx + i) & (s->cap - 1);
        if(s->slots[j] == 0)  return false;
        if(s->slots[j] == cp) return true;
    }
    return false;
}

static void cpset_grow(CpSet *s);

// Returns true if the codepoint was newly inserted.
static bool cpset_insert(CpSet *s, int cp) {
    if(cp == 0) return false;                    // sentinel
    if(cpset_contains(s, cp)) return false;

    if(s->count * 4 >= s->cap * 3) cpset_grow(s);

    size_t idx = (unsigned)cp & (s->cap - 1);
    for(size_t i = 0; i < s->cap; i++) {
        size_t j = (idx + i) & (s->cap - 1);
        if(s->slots[j] == 0) {
            s->slots[j] = cp;
            s->count++;
            return true;
        }
    }
    return false;   // should never happen after grow
}

static void cpset_grow(CpSet *s) {
    size_t oldCap = s->cap;
    int   *old    = s->slots;

    s->cap   *= 2;
    s->slots  = calloc(s->cap, sizeof(int));
    s->count  = 0;

    for(size_t i = 0; i < oldCap; i++)
        if(old[i] != 0) cpset_insert(s, old[i]);

    free(old);
}

// Fill dst (must hold at least s->count ints).
static void cpset_collect(const CpSet *s, int *dst) {
    size_t n = 0;
    for(size_t i = 0; i < s->cap; i++)
        if(s->slots[i] != 0) dst[n++] = s->slots[i];
}

// Glyph cache

struct ncl_GlyphCache {
    char   *fontPath;
    int     fontSize;
    Font    font;
    bool    fontLoaded;
    bool    dirty;           // new codepoints since last rebuild
    CpSet   known;           // all codepoints we have glyphs for
};

// Pre-seed the most common ranges so the first frame is not barren.
static void gc_seed(ncl_GlyphCache *gc) {
    // ASCII printable
    for(int i = 0x0020; i <= 0x007E; i++) cpset_insert(&gc->known, i);

    // Latin-1 Supplement
    for(int i = 0x00A0; i <= 0x00FF; i++) cpset_insert(&gc->known, i);

    // Cyrillic (common)
    for(int i = 0x0400; i <= 0x04FF; i++) cpset_insert(&gc->known, i);

    // General punctuation
    for(int i = 0x2010; i <= 0x2027; i++) cpset_insert(&gc->known, i);

    // Box drawing
    for(int i = 0x2500; i <= 0x257F; i++) cpset_insert(&gc->known, i);

    // Block elements
    for(int i = 0x2580; i <= 0x259F; i++) cpset_insert(&gc->known, i);

    // Geometric shapes (partial)
    for(int i = 0x25A0; i <= 0x25FF; i++) cpset_insert(&gc->known, i);

    // Braille patterns
    for(int i = 0x2800; i <= 0x28FF; i++) cpset_insert(&gc->known, i);

    // Powerline / private use (common in OC themes)
    for(int i = 0xE000; i <= 0xE0FF; i++) cpset_insert(&gc->known, i);

    gc->dirty = true;
}

static void gc_rebuild(ncl_GlyphCache *gc) {
    if(gc->fontLoaded) UnloadFont(gc->font);

    size_t n = gc->known.count;
    if(n == 0) { gc->fontLoaded = false; gc->dirty = false; return; }

    int *cps = malloc(sizeof(int) * n);
    cpset_collect(&gc->known, cps);

    gc->font       = LoadFontEx(gc->fontPath, gc->fontSize, cps, (int)n);
    gc->fontLoaded = true;
    gc->dirty      = false;

    // Let raylib use bilinear for scaled glyphs, nearest for 1:1.
    SetTextureFilter(gc->font.texture, TEXTURE_FILTER_POINT);

    free(cps);

    fprintf(stderr, "[glyphcache] rebuilt atlas: %zu glyphs, tex %dx%d\n",
            n, gc->font.texture.width, gc->font.texture.height);
}

// Public API

ncl_GlyphCache *ncl_createGlyphCache(const char *fontPath, int fontSize) {
    ncl_GlyphCache *gc = calloc(1, sizeof(*gc));
    gc->fontPath  = strdup(fontPath);
    gc->fontSize  = fontSize;
    gc->fontLoaded = false;
    gc->dirty      = false;
    cpset_init(&gc->known);
    gc_seed(gc);
    gc_rebuild(gc);
    return gc;
}

void ncl_destroyGlyphCache(ncl_GlyphCache *gc) {
    if(!gc) return;
    if(gc->fontLoaded) UnloadFont(gc->font);
    cpset_free(&gc->known);
    free(gc->fontPath);
    free(gc);
}

Font ncl_getFont(ncl_GlyphCache *gc) {
    return gc->font;
}

void ncl_flushGlyphs(ncl_GlyphCache *gc) {
    if(gc->dirty) gc_rebuild(gc);
}

void ncl_needGlyph(ncl_GlyphCache *gc, nn_codepoint cp) {
    if(cp == 0) return;
    if(cpset_insert(&gc->known, (int)cp))
        gc->dirty = true;
}

void ncl_drawGlyph(ncl_GlyphCache *gc, nn_codepoint cp,
                    Vector2 pos, float size, Color tint)
{
    ncl_needGlyph(gc, cp);
    DrawTextCodepoint(gc->font, (int)cp, pos, size, tint);
}

int ncl_cellWidth(ncl_GlyphCache *gc) {
    // Measure 'A' as the reference cell.
    if(!gc->fontLoaded) return 8;
    return MeasureTextEx(gc->font, "A", (float)gc->fontSize, 0).x;
}

int ncl_cellHeight(ncl_GlyphCache *gc) {
    return gc->fontSize;
}