#include "screen.h"

nn_screen *nn_newScreen(int maxWidth, int maxHeight, int maxDepth, int editableColors, int paletteColors);

void nn_retainScreen(nn_screen *screen) {
    nn_incRef(&screen->refc);
}

void nn_destroyScreen(nn_screen *screen) {
    if(!nn_decRef(&screen->refc)) return;
    nn_deleteGuard(screen->lock);
    nn_free(screen->buffer);
    nn_free(screen->palette);
    nn_free(screen);
}

void nn_lockScreen(nn_screen *screen) {
    nn_lock(screen->lock);
}

void nn_unlockScreen(nn_screen *screen) {
    nn_unlock(screen->lock);
}

void nn_getResolution(nn_screen *screen, int *width, int *height) {
    *width = screen->width;
    *height = screen->height;
}

void nn_maxResolution(nn_screen *screen, int *width, int *height) {
    *width = screen->maxWidth;
    *height = screen->maxHeight;
}

void nn_setResolution(nn_screen *screen, int width, int height) {
    screen->width = width;
    screen->height = height;
}

void nn_getViewport(nn_screen *screen, int *width, int *height) {
    *width = screen->viewportWidth;
    *height = screen->viewportHeight;
}

void nn_setViewport(nn_screen *screen, int width, int height) {
    screen->viewportWidth = width;
    screen->viewportHeight = height;
}

void nn_getAspectRatio(nn_screen *screen, int *width, int *height) {
    *width = screen->aspectRatioWidth;
    *height = screen->aspectRatioHeight;
}

void nn_setAspectRatio(nn_screen *screen, int width, int height) {
    screen->aspectRatioWidth = width;
    screen->aspectRatioHeight = height;
}

void nn_addKeyboard(nn_screen *screen, nn_address address) {
    if(screen->keyboardCount == NN_MAX_SCREEN_KEYBOARDS) return;
    screen->keyboards[screen->keyboardCount++] = nn_strdup(address);
}

void nn_removeKeyboard(nn_screen *screen, nn_address address);

nn_address nn_getKeyboard(nn_screen *screen, size_t idx) {
    if(idx >= screen->keyboardCount) return NULL;
    return screen->keyboards[idx];
}

size_t nn_getKeyboardCount(nn_screen *screen) {
    return screen->keyboardCount;
}

void nn_setEditableColors(nn_screen *screen, int count) {
    screen->editableColors = count;
}

int nn_getEditableColors(nn_screen *screen) {
    return screen->editableColors;
}

void nn_setPaletteColor(nn_screen *screen, int idx, int color) {
    if(idx >= screen->paletteColors) return;
    screen->palette[idx] = color;
}

int nn_getPaletteColor(nn_screen *screen, int idx) {
    if(idx >= screen->paletteColors) return 0;
    return screen->palette[idx];
}

int nn_getPaletteCount(nn_screen *screen) {
    return screen->paletteColors;
}

int nn_maxDepth(nn_screen *screen) {
    return screen->maxDepth;
}

int nn_getDepth(nn_screen *screen) {
    return screen->depth;
}

void nn_setDepth(nn_screen *screen, int depth) {
    if(depth > screen->maxDepth) depth = screen->maxDepth;
    screen->depth = depth;
}

void nn_setPixel(nn_screen *screen, int x, int y, nn_screenChar pixel) {
    screen->buffer[x + y * screen->maxWidth] = pixel;
}

nn_screenChar nn_getPixel(nn_screen *screen, int x, int y) {
    return screen->buffer[x + y * screen->maxWidth];
}

bool nn_isDirty(nn_screen *screen) {
    return screen->isDirty;
}

void nn_setDirty(nn_screen *screen, bool dirty) {
    screen->isDirty = dirty;
}

bool nn_isPrecise(nn_screen *screen) {
    return screen->isPrecise;
}

void nn_setPrecise(nn_screen *screen, bool precise) {
    screen->isPrecise = precise;
}

bool nn_isTouchModeInverted(nn_screen *screen) {
    return screen->isTouchModeInverted;
}

void nn_setTouchModeInverted(nn_screen *screen, bool touchModeInverted) {
    screen->isTouchModeInverted = touchModeInverted;
}

bool nn_isOn(nn_screen *buffer) {
    return buffer->isOn;
}

void nn_setOn(nn_screen *buffer, bool on) {
    buffer->isOn = on;
}

void nn_loadScreenTable(nn_universe *universe);
nn_component *nn_addScreen(nn_computer *computer, nn_address address, int slot, nn_screen *screen);
