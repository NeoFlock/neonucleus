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
} nn_screen;
