#include "screen.h"
#include <string.h>

nn_screen *nn_newScreen(int maxWidth, int maxHeight, int maxDepth, int editableColors, int paletteColors) {
    nn_screen *screen = nn_malloc(sizeof(nn_screen));
    screen->buffer = nn_malloc(sizeof(nn_screenChar) * maxWidth * maxHeight);
    screen->lock = nn_newGuard();
    screen->refc = 1;
    screen->width = maxWidth;
    screen->height = maxHeight;
    screen->viewportWidth = maxWidth;
    screen->viewportHeight = maxHeight;
    screen->maxWidth = maxWidth;
    screen->maxHeight = maxHeight;
    screen->maxDepth = maxDepth;
    screen->depth = maxDepth;
    screen->editableColors = editableColors;
    screen->paletteColors = paletteColors;
    screen->palette = nn_malloc(sizeof(int) * screen->paletteColors);
    memset(screen->palette, 0, sizeof(int) * screen->paletteColors);
    screen->aspectRatioWidth = 1;
    screen->aspectRatioHeight = 1;
    screen->isOn = true;
    screen->isTouchModeInverted = true;
    screen->isPrecise = true;
    screen->isDirty = true;
    screen->keyboardCount = 0;
    return screen;
}

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

void nn_removeKeyboard(nn_screen *screen, nn_address address) {
    size_t j = 0;
    for(size_t i = 0; i < screen->keyboardCount; i++) {
        if(strcmp(screen->keyboards[i], address) == 0) {
            nn_free(screen->keyboards[i]);
        } else {
            screen->keyboards[j] = screen->keyboards[i];
            j++;
        }
    }
    screen->keyboardCount = j;
}

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

void nn_screenComp_destroy(void *_, nn_component *component, nn_screen *screen) {
    nn_destroyScreen(screen);
}

void nn_screenComp_getKeyboards(nn_screen *screen, void *_, nn_component *component, nn_computer *computer) {
    nn_value arr = nn_values_array(nn_getKeyboardCount(screen));
}

void nn_loadScreenTable(nn_universe *universe) {
    nn_componentTable *screenTable = nn_newComponentTable("screen", NULL, NULL, (void *)nn_screenComp_destroy);
    nn_storeUserdata(universe, "NN:SCREEN", screenTable);

    nn_defineMethod(screenTable, "getKeyboards", false, (void *)nn_screenComp_getKeyboards, NULL, "getKeyboards(): string[] - Returns the keyboards registered to this screen.");
}

nn_component *nn_addScreen(nn_computer *computer, nn_address address, int slot, nn_screen *screen);
