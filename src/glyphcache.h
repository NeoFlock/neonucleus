#ifndef NCL_GLYPHCACHE_H
#define NCL_GLYPHCACHE_H

#include <raylib.h>
#include "neonucleus.h"

typedef struct ncl_GlyphCache ncl_GlyphCache;

ncl_GlyphCache *ncl_createGlyphCache(const char *fontPath, int fontSize);
void            ncl_destroyGlyphCache(ncl_GlyphCache *gc);
Font            ncl_getFont(ncl_GlyphCache *gc);
void            ncl_flushGlyphs(ncl_GlyphCache *gc);
void            ncl_needGlyph(ncl_GlyphCache *gc, nn_codepoint cp);
void            ncl_drawGlyph(ncl_GlyphCache *gc, nn_codepoint cp,
                              Vector2 pos, float size, Color tint);
int             ncl_cellWidth(ncl_GlyphCache *gc);
int             ncl_cellHeight(ncl_GlyphCache *gc);

#endif