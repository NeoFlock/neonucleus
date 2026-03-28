#ifndef NN_COMPLIB
#define NN_COMPLIB

#include "neonucleus.h"

typedef struct ncl_EncodedState {
	char *buf;
	size_t len;
} ncl_EncodedState;

nn_Exit ncl_encodeComponentState(nn_Universe *universe, nn_Component *comp, ncl_EncodedState *state);
void ncl_freeEncodedState(nn_Universe *universe, ncl_EncodedState *state);
nn_Exit ncl_loadComponentState(nn_Component *comp, const ncl_EncodedState *state);

nn_Component *ncl_createFilesystem(nn_Universe *universe, const char *address, const char *path, const nn_Filesystem *fs);
nn_Component *ncl_createDrive(nn_Universe *universe, const char *address, const char *path, const nn_Drive *drive);
nn_Component *ncl_createEEPROM(nn_Universe *universe, const char *address, const char *codepath, const char *datapath);
nn_Component *ncl_createScreen(nn_Universe *universe, const char *address, const nn_ScreenConfig *config);
nn_Component *ncl_createGPU(nn_Universe *universe, const char *address, const nn_GPU *gpu);

// TODO, stuff we could implement:
// redstone, hologram, oled, ipu, vt, led, tape_drive, cd_drive, serial, colorful_lamp

typedef struct ncl_Pixel {
	// 0xRRGGBB format
	unsigned int fgColor;
	// 0xRRGGBB format
	unsigned int bgColor;
	// the codepoint
	nn_codepoint codepoint;
} ncl_Pixel;

typedef struct ncl_ScreenState ncl_ScreenState;

void ncl_getScreenResolution(const ncl_ScreenState *state, size_t *width, size_t *height);
void ncl_getScreenViewport(const ncl_ScreenState *state, size_t *width, size_t *height);
ncl_Pixel ncl_getScreenPixel(const ncl_ScreenState *state, int x, int y);

#endif
