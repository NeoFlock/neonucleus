#include "neonucleus.h"
#include "ncomplib.h"
#include <stdlib.h>
#include <stdio.h>

typedef struct ncl_ScreenPixel {
	nn_codepoint codepoint;
	int storedFg;
	int storedBg;
	// if negative, its in palette
	int realFg;
	// if negative, its in palette
	int realBg;
} ncl_ScreenPixel;

typedef struct ncl_ScreenState {
	nn_Context *ctx;
	nn_ScreenConfig conf;
	int width;
	int height;
	int viewportWidth;
	int viewportHeight;
	char depth;
	int *palette;
	int *resolvedPalette;
	ncl_ScreenPixel *pixels;
} ncl_ScreenState;

nn_Component *ncl_createFilesystem(nn_Universe *universe, const char *address, const char *path, const nn_Filesystem *fs);
nn_Component *ncl_createDrive(nn_Universe *universe, const char *address, const char *path, const nn_Drive *drive);
nn_Component *ncl_createEEPROM(nn_Universe *universe, const char *address, const char *codepath, const char *datapath);

static ncl_ScreenPixel ncl_getRealScreenPixel(const ncl_ScreenState *state, int x, int y) {
	if(x < 1 || y < 1 || x >= state->width || y >= state->height) {
		return (ncl_ScreenPixel) {
			.codepoint = ' ',
			.storedFg = 0xFFFFFF,
			.storedBg = 0x000000,
			.realFg = 0xFFFFFF,
			.realBg = 0x000000,
		};
	}

	// make it 0-indexed
	x--;
	y--;

	return state->pixels[x + y * state->conf.maxWidth];
}

static ncl_ScreenPixel *ncl_getRealScreenPixelPointer(const ncl_ScreenState *state, int x, int y) {
	if(x < 1 || y < 1 || x >= state->width || y >= state->height) {
		return NULL;
	}

	// make it 0-indexed
	x--;
	y--;

	return &state->pixels[x + y * state->conf.maxWidth];
}

static void ncl_setRealScreenPixel(const ncl_ScreenState *state, int x, int y, ncl_ScreenPixel pixel) {
	if(x < 1 || y < 1 || x >= state->width || y >= state->height) return;
	x--;
	y--;

	state->pixels[x + y * state->conf.maxWidth] = pixel;
}

static void ncl_recomputeScreen(const ncl_ScreenState *state) {
	for(int y = 1; y <= state->height; y++) {
		for(int x = 1; x <= state->width; x++) {
			ncl_ScreenPixel *pixel = ncl_getRealScreenPixelPointer(state, x, y);
			if(pixel == NULL) continue;
			if(pixel->realFg >= 0) {
				pixel->realFg = nn_mapDepth(pixel->storedFg, state->depth);
			}
			if(pixel->realBg >= 0) {
				pixel->realBg = nn_mapDepth(pixel->storedBg, state->depth);
			}
		}
	}

	for(int i = 0; i < state->conf.paletteColors; i++) {
		state->resolvedPalette[i] = nn_mapDepth(state->palette[i], state->depth);
	}
}

nn_Component *ncl_createScreen(nn_Universe *universe, const char *address, const nn_ScreenConfig *config);
nn_Component *ncl_createGPU(nn_Universe *universe, const char *address, const nn_GPU *gpu);

void ncl_getScreenResolution(const ncl_ScreenState *state, size_t *width, size_t *height) {
	*width = state->width;
	*height = state->height;
}

void ncl_getScreenViewport(const ncl_ScreenState *state, size_t *width, size_t *height) {
	*width = state->viewportWidth;
	*height = state->viewportHeight;
}

ncl_Pixel ncl_getScreenPixel(const ncl_ScreenState *state, int x, int y) {
	ncl_ScreenPixel p = ncl_getRealScreenPixel(state, x, y);
	return (ncl_Pixel) {
		.codepoint = p.codepoint,
		.fgColor = p.realFg < 0 ? state->resolvedPalette[p.storedFg] : p.realFg,
		.bgColor = p.realBg < 0 ? state->resolvedPalette[p.storedBg] : p.realBg,
	};
}

// all of these are encoding states

nn_Exit ncl_encodeComponentState(nn_Universe *universe, nn_Component *comp, ncl_EncodedState *state);
void ncl_freeEncodedState(nn_Universe *universe, ncl_EncodedState *state);
nn_Exit ncl_loadComponentState(nn_Component *comp, const ncl_EncodedState *state);
