#include "screen.h"

nn_screen *nn_newScreen(nn_Context *context, int maxWidth, int maxHeight, int maxDepth, int editableColors, int paletteColors) {
    nn_Alloc *alloc = &context->allocator;
    nn_screen *screen = nn_alloc(alloc, sizeof(nn_screen));
    screen->ctx = *context;
    screen->buffer = nn_alloc(alloc, sizeof(nn_scrchr_t) * maxWidth * maxHeight);
    screen->lock = nn_newGuard(context);
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
    screen->palette = nn_alloc(alloc, sizeof(int) * screen->paletteColors);
    nn_memset(screen->palette, 0, sizeof(int) * screen->paletteColors);
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
    nn_Alloc a = screen->ctx.allocator;
    nn_deleteGuard(&screen->ctx, screen->lock);
    nn_dealloc(&a, screen->buffer, sizeof(nn_scrchr_t) * screen->maxWidth * screen->maxHeight);
    nn_dealloc(&a, screen->palette, sizeof(int) * screen->paletteColors);
    nn_dealloc(&a, screen, sizeof(nn_screen));
}

void nn_lockScreen(nn_screen *screen) {
    nn_lock(&screen->ctx, screen->lock);
}

void nn_unlockScreen(nn_screen *screen) {
    nn_unlock(&screen->ctx, screen->lock);
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
    char *kb = nn_strdup(&screen->ctx.allocator, address);
    if(kb == NULL) return;
    screen->keyboards[screen->keyboardCount++] = kb;
}

void nn_removeKeyboard(nn_screen *screen, nn_address address) {
    nn_size_t j = 0;
    for(nn_size_t i = 0; i < screen->keyboardCount; i++) {
        if(nn_strcmp(screen->keyboards[i], address) == 0) {
            nn_deallocStr(&screen->ctx.allocator, screen->keyboards[i]);
        } else {
            screen->keyboards[j] = screen->keyboards[i];
            j++;
        }
    }
    screen->keyboardCount = j;
}

nn_address nn_getKeyboard(nn_screen *screen, nn_size_t idx) {
    if(idx >= screen->keyboardCount) return NULL;
    return screen->keyboards[idx];
}

nn_size_t nn_getKeyboardCount(nn_screen *screen) {
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

void nn_setPixel(nn_screen *screen, int x, int y, nn_scrchr_t pixel) {
    if(x < 0) return;
    if(y < 0) return;
    if(x >= screen->width) return;
    if(y >= screen->height) return;
    screen->buffer[x + y * screen->maxWidth] = pixel;
    screen->isDirty = true; // stuff changed
}

nn_scrchr_t nn_getPixel(nn_screen *screen, int x, int y) {
    nn_scrchr_t blank = {
        .codepoint = ' ',
        .fg = 0xFFFFFF,
        .bg = 0x000000,
        .isFgPalette = false,
        .isBgPalette = false,
    };
    if(x < 0) return blank;
    if(y < 0) return blank;
    if(x >= screen->width) return blank;
    if(y >= screen->height) return blank;
    return screen->buffer[x + y * screen->maxWidth];
}

nn_bool_t nn_isDirty(nn_screen *screen) {
    return screen->isDirty;
}

void nn_setDirty(nn_screen *screen, nn_bool_t dirty) {
    screen->isDirty = dirty;
}

nn_bool_t nn_isPrecise(nn_screen *screen) {
    return screen->isPrecise;
}

void nn_setPrecise(nn_screen *screen, nn_bool_t precise) {
    screen->isPrecise = precise;
}

nn_bool_t nn_isTouchModeInverted(nn_screen *screen) {
    return screen->isTouchModeInverted;
}

void nn_setTouchModeInverted(nn_screen *screen, nn_bool_t touchModeInverted) {
    screen->isTouchModeInverted = touchModeInverted;
}

nn_bool_t nn_isOn(nn_screen *buffer) {
    return buffer->isOn;
}

void nn_setOn(nn_screen *buffer, nn_bool_t on) {
    buffer->isOn = on;
}

void nn_screenComp_destroy(void *_, nn_component *component, nn_screen *screen) {
    nn_destroyScreen(screen);
}

void nn_screenComp_getKeyboards(nn_screen *screen, void *_, nn_component *component, nn_computer *computer) {
    nn_lockScreen(screen);
    nn_value arr = nn_values_array(&screen->ctx.allocator, nn_getKeyboardCount(screen));

    nn_size_t len = arr.array->len;
    for(nn_size_t i = 0; i < len; i++) {
        nn_size_t addrlen = nn_strlen(nn_getKeyboard(screen, i));
        nn_value addr = nn_values_string(&screen->ctx.allocator, nn_getKeyboard(screen, i), addrlen);
        nn_values_set(arr, i, addr);
    }

    nn_unlockScreen(screen);
    nn_return(computer, arr);
}

void nn_screenComp_getAspectRatio(nn_screen *screen, void *_, nn_component *component, nn_computer *computer) {
    nn_lockScreen(screen);

    int w, h;
    nn_getAspectRatio(screen, &w, &h);

    nn_unlockScreen(screen);

    nn_return_integer(computer, w);
    nn_return_integer(computer, h);
}

void nn_loadScreenTable(nn_universe *universe) {
    nn_componentTable *screenTable = nn_newComponentTable(nn_getAllocator(universe), "screen", NULL, NULL, (nn_componentDestructor *)nn_screenComp_destroy);
    nn_storeUserdata(universe, "NN:SCREEN", screenTable);

    nn_defineMethod(screenTable, "getKeyboards", true, (nn_componentMethod *)nn_screenComp_getKeyboards, NULL, "getKeyboards(): string[] - Returns the keyboards registered to this screen.");
    nn_defineMethod(screenTable, "getAspectRatio", true, (nn_componentMethod *)nn_screenComp_getAspectRatio, NULL, "");
}

nn_componentTable *nn_getScreenTable(nn_universe *universe) {
    return nn_queryUserdata(universe, "NN:SCREEN");
}

nn_component *nn_addScreen(nn_computer *computer, nn_address address, int slot, nn_screen *screen) {
    nn_componentTable *screenTable = nn_queryUserdata(nn_getUniverse(computer), "NN:SCREEN");
    return nn_newComponent(computer, address, slot, screenTable, screen);
}

static const int nni_mcBlack = 0x1D1D21;
static const int nni_mcWhite = 0xFFF9FE;

void nn_getStd4BitPalette(int color[16]) {
    color[0] = nni_mcWhite; // white
    color[1] = 0xF9801D; // orange
    color[2] = 0xC74EBD; // magenta
    color[3] = 0x3AB3DA; // lightblue
    color[4] = 0xFED83D; // yellow
    color[5] = 0x80C71F; // lime
    color[6] = 0xF38BAA; // pink
    color[7] = 0x474F52; // gray
    color[8] = 0x9D9D97; // silver
    color[9] = 0x169C9C; // cyan
    color[10] = 0x8932B8; // purple
    color[11] = 0x3C44AA; // blue
    color[12] = 0x835432; // brown
    color[13] = 0x5E7C16; // green
    color[14] = 0xB02E26; // red
    color[15] = nni_mcBlack; // black
}

void nn_getStd8BitPalette(int color[256]) {
    // source: https://ocdoc.cil.li/component:gpu
    int reds[6] = {0x00, 0x33, 0x66, 0x99, 0xCC, 0xFF};
    int greens[8] = {0x00, 0x24, 0x49, 0x6D, 0x92, 0xB6, 0xDB, 0xFF};
    int blues[5] = {0x00, 0x40, 0x80, 0xC0, 0xFF};

    for(int r = 0; r < 6; r++) {
        for(int g = 0; g < 8; g++) {
            for(int b = 0; b < 5; b++) {
                int i = r * 8 * 5 + g * 5 + b;
                color[i] = (reds[r] << 16) | (greens[g] << 8) | (blues[b]);
            }
        }
    }

    // TODO: turn into an algorithm
    color[240] = 0x0F0F0F;
    color[241] = 0x1E1E1E;
    color[242] = 0x2D2D2D;
    color[243] = 0x3C3C3C;
    color[244] = 0x4B4B4B;
    color[245] = 0x5A5A5A;
    color[246] = 0x696969;
    color[247] = 0x787878;
    color[248] = 0x878787;
    color[249] = 0x969696;
    color[250] = 0xA5A5A5;
    color[251] = 0xB4B4B4;
    color[252] = 0xC3C3C3;
    color[253] = 0xD2D2D2;
    color[254] = 0xE1E1E1;
    color[255] = 0xF0F0F0;
}

static int nni_4bit_colors[16];
static nn_bool_t nni_4bit_did = false;
static int nni_8bit_colors[256];
static nn_bool_t nni_8bit_did = false;

int nn_mapDepth(int color, int depth) {
    if(depth == 1) {
        if(color == 0) return nni_mcBlack;
        return nni_mcWhite;
    }
    if(depth == 4) {
        if(!nni_4bit_did) {
            nni_4bit_did = true;
            nn_getStd4BitPalette(nni_4bit_colors);
        }
        return nn_mapColor(color, nni_4bit_colors, 16);
    }
    if(depth == 8) {
        if(!nni_8bit_did) {
            nni_8bit_did = true;
            nn_getStd8BitPalette(nni_8bit_colors);
        }
        return nn_mapColor(color, nni_8bit_colors, 256);
    }
    return color;
}
