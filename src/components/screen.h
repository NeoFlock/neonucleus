#ifndef NN_SCREEN_H
#define NN_SCREEN_H

#include "../neonucleus.h"

typedef struct nn_screen {
    nn_screenChar *buffer;
    nn_guard *lock;
    nn_refc refc;
    int width;
    int height;
    int viewportWidth;
    int viewportHeight;
    int maxWidth;
    int maxHeight;
    int maxDepth;
    int editableColors;
    int paletteColors;
    int *palette;
    bool isOn;
    nn_address keyboards[NN_MAX_SCREEN_KEYBOARDS];
    size_t keyboardCount;
} nn_screen;

#endif
