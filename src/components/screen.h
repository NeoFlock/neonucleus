#ifndef NN_SCREEN_H
#define NN_SCREEN_H

#include "../neonucleus.h"

typedef struct nn_screen {
    nn_Context ctx;
    nn_scrchr_t *buffer;
    nn_guard *lock;
    nn_refc refc;
    int width;
    int height;
    int viewportWidth;
    int viewportHeight;
    int maxWidth;
    int maxHeight;
    int maxDepth;
    int depth;
    int editableColors;
    int paletteColors;
    int *palette;
    int aspectRatioWidth;
    int aspectRatioHeight;
    nn_bool_t isOn;
    nn_bool_t isTouchModeInverted;
    nn_bool_t isPrecise;
    nn_bool_t isDirty;
    nn_address keyboards[NN_MAX_SCREEN_KEYBOARDS];
    nn_size_t keyboardCount;
} nn_screen;

#endif
