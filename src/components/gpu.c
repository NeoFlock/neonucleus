#include "../neonucleus.h"
#include "screen.h"

typedef struct nni_gpu {
    nn_Alloc alloc;
    nn_screen *currentScreen;
    nn_address screenAddress;
    nn_gpuControl ctrl;
    int currentFg;
    int currentBg;
    nn_bool_t isFgPalette;
    nn_bool_t isBgPalette;
    // TODO: think about buffers and stuff
} nni_gpu;

nn_bool_t nni_samePixel(nn_scrchr_t a, nn_scrchr_t b) {
    return
        a.codepoint == b.codepoint &&
        a.fg == b.fg &&
        a.bg == b.bg &&
        a.isFgPalette == b.isFgPalette &&
        a.isBgPalette == b.isBgPalette
        ;
}

nn_bool_t nni_inBounds(nni_gpu *gpu, int x, int y) {
    if(gpu->currentScreen == NULL) return false;
    return
        x >= 0 &&
        y >= 0 &&
        x < gpu->currentScreen->width &&
        y < gpu->currentScreen->height &&
        true;
}

nni_gpu *nni_newGPU(nn_Alloc *alloc, nn_gpuControl *ctrl) {
    nni_gpu *gpu = nn_alloc(alloc, sizeof(nni_gpu));
    if(gpu == NULL) return NULL;
    gpu->alloc = *alloc;
    gpu->currentScreen = NULL;
    gpu->screenAddress = NULL;
    gpu->ctrl = *ctrl;
    gpu->currentFg = 0xFFFFFF;
    gpu->currentBg = 0x000000;
    gpu->isFgPalette = false;
    gpu->isBgPalette = false;
    return gpu;
}

void nni_gpuDeinit(nni_gpu *gpu) {
    if(gpu->currentScreen != NULL) {
        nn_destroyScreen(gpu->currentScreen);
    }
    nn_Alloc a = gpu->alloc;
    if(gpu->screenAddress != NULL) {
        nn_deallocStr(&a, gpu->screenAddress);
    }
    nn_dealloc(&a, gpu, sizeof(nni_gpu));
}

nn_scrchr_t nni_gpu_makePixel(nni_gpu *gpu, const char *s) {
    return (nn_scrchr_t) {
        .codepoint = nn_unicode_codepointAt(s, 0),
        .fg = gpu->currentFg,
        .bg = gpu->currentBg,
        .isFgPalette = gpu->isFgPalette,
        .isBgPalette = gpu->isBgPalette,
    };
}

void nni_gpu_bind(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    nn_value argVal = nn_getArgument(computer, 0);
    nn_value resetVal = nn_getArgument(computer, 1);

    const char *addr = nn_toCString(argVal);
    if(addr == NULL) {
        nn_setCError(computer, "bad argument #1 (address expected)");
        return;
    }
    nn_bool_t reset = false;
    if(resetVal.tag == NN_VALUE_BOOL) reset = nn_toBoolean(resetVal);

    nn_component *c = nn_findComponent(computer, (nn_address)addr);
    if(c == NULL) {
        nn_setCError(computer, "no such screen");
        return;
    }

    nn_componentTable *supportedTable = nn_getScreenTable(nn_getUniverse(computer));
    if(supportedTable != nn_getComponentTable(c)) {
        nn_setCError(computer, "incompatible screen");
        return;
    }

    nn_screen *screen = nn_getComponentUserdata(c);
    nn_retainScreen(screen);
    if(gpu->currentScreen != NULL) nn_destroyScreen(gpu->currentScreen);
    gpu->currentScreen = screen;
    

    if(reset) {
        for(nn_size_t i = 0; i < screen->width; i++) {
            for(nn_size_t j = 0; j < screen->height; j++) {
                nn_setPixel(screen, i, j, nni_gpu_makePixel(gpu, " "));
            }
        }
        nn_size_t area = screen->width * screen->height;
        nn_addHeat(computer, gpu->ctrl.heatPerPixelReset * area);
        nn_simulateBufferedIndirect(component, 1, gpu->ctrl.screenFillPerTick);
        nn_removeEnergy(computer, gpu->ctrl.energyPerPixelReset * area);
    }

    gpu->currentScreen = screen;
    if(gpu->screenAddress != NULL) {
        nn_deallocStr(&gpu->alloc, gpu->screenAddress);
    }
    gpu->screenAddress = nn_strdup(&gpu->alloc, addr);

    nn_return(computer, nn_values_boolean(true));
}

void nni_gpu_set(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    int x = nn_toInt(nn_getArgument(computer, 0)) - 1;
    int y = nn_toInt(nn_getArgument(computer, 1)) - 1;
    const char *s = nn_toCString(nn_getArgument(computer, 2));
    nn_bool_t isVertical = nn_toBoolean(nn_getArgument(computer, 3));

    if(s == NULL) {
        nn_setCError(computer, "bad argument #3 (string expected in set)");
        return;
    }

    nn_size_t current = 0;
    while(s[current] != 0) {
        unsigned int codepoint = nn_unicode_nextCodepointPermissive(s, &current);
        char buf[NN_MAXIMUM_UNICODE_BUFFER];
        nn_unicode_codepointToChar(buf, codepoint, NULL);
        nn_setPixel(gpu->currentScreen, x, y, nni_gpu_makePixel(gpu, buf));
        if(isVertical) {
            y++;
        } else {
            x++;
        }
    }

    nn_simulateBufferedIndirect(component, 1, gpu->ctrl.screenSetsPerTick);
}

void nni_gpu_get(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->screenAddress == NULL) return;
    int x = nn_toInt(nn_getArgument(computer, 0)) - 1;
    int y = nn_toInt(nn_getArgument(computer, 1)) - 1;
    nn_scrchr_t pxl = nn_getPixel(gpu->currentScreen, x, y);

    nn_size_t l;
    char chr[NN_MAXIMUM_UNICODE_BUFFER];
    nn_unicode_codepointToChar(chr, pxl.codepoint, &l);

    // TODO: gosh darn palettes
    nn_return_string(computer, chr, l);
    nn_return_integer(computer, pxl.fg);
    nn_return_integer(computer, pxl.bg);
}

void nni_gpu_getScreen(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->screenAddress == NULL) return;
    nn_return_string(computer, gpu->screenAddress, nn_strlen(gpu->screenAddress));
}

void nni_gpu_maxResolution(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    int w, h;
    nn_maxResolution(gpu->currentScreen, &w, &h);
    nn_return(computer, nn_values_integer(w));
    nn_return(computer, nn_values_integer(h));
}

void nni_gpu_getResolution(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    int w, h;
    nn_getResolution(gpu->currentScreen, &w, &h);
    nn_return(computer, nn_values_integer(w));
    nn_return(computer, nn_values_integer(h));
}

void nni_gpu_setResolution(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    int mw, mh;
    nn_maxResolution(gpu->currentScreen, &mw, &mh);
    
    int lw, lh;
    nn_getResolution(gpu->currentScreen, &lw, &lh);

    int w = nn_toInt(nn_getArgument(computer, 0));
    int h = nn_toInt(nn_getArgument(computer, 1));

    nn_bool_t changed = w != lw || h != lh;

    if(w <= 0) w = 1;
    if(h <= 0) h = 1;
    if(w > mw) w = mw;
    if(h > mh) h = mh;
    nn_setResolution(gpu->currentScreen, w, h);
    nn_setViewport(gpu->currentScreen, w, h);

    nn_return(computer, nn_values_boolean(changed));
    
    nn_value signalShit[] = {
        nn_values_cstring("screen_resized"),
        nn_values_cstring(gpu->screenAddress),
        nn_values_integer(w),
        nn_values_integer(h),
    };
    nn_pushSignal(computer, signalShit, 4);
}

void nni_gpu_setBackground(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    int color = nn_toInt(nn_getArgument(computer, 0));
    nn_bool_t isPalette = nn_toBoolean(nn_getArgument(computer, 1));

    if(isPalette && (color < 0 || color >= gpu->currentScreen->paletteColors)) {
        nn_setCError(computer, "invalid palette index");
        return;
    }

    int old = gpu->currentBg;
    int idx = -1;
    if(gpu->isBgPalette) {
        idx = old;
        old = gpu->currentScreen->palette[old];
    }
    
    gpu->currentBg = color;
    gpu->isBgPalette = isPalette;
    
    nn_simulateBufferedIndirect(component, 1, gpu->ctrl.screenColorChangesPerTick);

    nn_return(computer, nn_values_integer(old));
    if(idx != -1) {
        nn_return(computer, nn_values_integer(idx));
    }
}

void nni_gpu_getBackground(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    nn_return(computer, nn_values_integer(gpu->currentBg));
}

void nni_gpu_setForeground(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    int color = nn_toInt(nn_getArgument(computer, 0));
    nn_bool_t isPalette = nn_toBoolean(nn_getArgument(computer, 1));

    if(isPalette && (color < 0 || color >= gpu->currentScreen->paletteColors)) {
        nn_setCError(computer, "invalid palette index");
        return;
    }

    int old = gpu->currentFg;
    int idx = -1;
    if(gpu->isFgPalette) {
        idx = old;
        old = gpu->currentScreen->palette[old];
    }
    
    gpu->currentFg = color;
    gpu->isFgPalette = isPalette;
    
    nn_simulateBufferedIndirect(component, 1, gpu->ctrl.screenColorChangesPerTick);

    nn_return(computer, nn_values_integer(old));
    if(idx != -1) {
        nn_return(computer, nn_values_integer(idx));
    }
}

void nni_gpu_getForeground(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    nn_return(computer, nn_values_integer(gpu->currentFg));   
}

void nni_gpu_fill(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    int x = nn_toInt(nn_getArgument(computer, 0)) - 1;
    int y = nn_toInt(nn_getArgument(computer, 1)) - 1;
    int w = nn_toInt(nn_getArgument(computer, 2));
    int h = nn_toInt(nn_getArgument(computer, 3));
    const char *s = nn_toCString(nn_getArgument(computer, 4));
    if(s == NULL) {
        nn_setCError(computer, "bad argument #5 (character expected)");
        return;
    }

    nn_size_t startIdx = 0;
    int codepoint = nn_unicode_nextCodepointPermissive(s, &startIdx);

    // prevent DoS
    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(w > gpu->currentScreen->width - x) w = gpu->currentScreen->width - x;
    if(h > gpu->currentScreen->height - y) h = gpu->currentScreen->height - y;
    
    int changes = 0, clears = 0;

    nn_scrchr_t new = nni_gpu_makePixel(gpu, s);

    for(int cx = x; cx < x + w; cx++) {
        for(int cy = y; cy < y + h; cy++) {
            nn_scrchr_t old = nn_getPixel(gpu->currentScreen, cx, cy);
            if(!nni_samePixel(old, new)) {
                nn_setPixel(gpu->currentScreen, cx, cy, new);
                if(codepoint == ' ')
                    clears++;
                else changes++;
            }
        }
    }

    nn_addHeat(computer, gpu->ctrl.heatPerPixelChange * changes);
    nn_removeEnergy(computer, gpu->ctrl.energyPerPixelChange * changes);
    
    nn_addHeat(computer, gpu->ctrl.heatPerPixelReset * clears);
    nn_removeEnergy(computer, gpu->ctrl.energyPerPixelReset * clears);
    
    nn_simulateBufferedIndirect(component, 1, gpu->ctrl.screenFillPerTick);

    nn_return(computer, nn_values_boolean(true));
}

void nni_gpu_copy(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    int x = nn_toInt(nn_getArgument(computer, 0)) - 1;
    int y = nn_toInt(nn_getArgument(computer, 1)) - 1;
    int w = nn_toInt(nn_getArgument(computer, 2));
    int h = nn_toInt(nn_getArgument(computer, 3));
    int tx = nn_toInt(nn_getArgument(computer, 4));
    int ty = nn_toInt(nn_getArgument(computer, 5));

    // prevent DoS
    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(w > gpu->currentScreen->width - x) w = gpu->currentScreen->width - x;
    if(h > gpu->currentScreen->height - y) y = gpu->currentScreen->height - y;
    
    int changes = 0, clears = 0;

    nn_scrchr_t *tmpBuffer = nn_alloc(&gpu->alloc, sizeof(nn_scrchr_t) * w * h);
    if(tmpBuffer == NULL) {
        nn_setCError(computer, "out of memory");
        return;
    }

    for(int cx = x; cx < x + w; cx++) {
        for(int cy = y; cy < y + h; cy++) {
            int ox = cx - x;
            int oy = cy - y;
            nn_scrchr_t src = nn_getPixel(gpu->currentScreen, cx, cy);
            nn_scrchr_t old = nn_getPixel(gpu->currentScreen, cx + tx, cy + ty);
            tmpBuffer[ox + oy * w] = src;
            if(!nni_samePixel(old, src)) {
                if(src.codepoint == ' ')
                    clears++;
                else changes++;
            }
        }
    }

    for(int ox = 0; ox < w; ox++) {
        for(int oy = 0; oy < h; oy++) {
            nn_scrchr_t p = tmpBuffer[ox + oy * w];
            nn_setPixel(gpu->currentScreen, ox + x + tx, oy + y + ty, p);
        }
    }

    nn_dealloc(&gpu->alloc, tmpBuffer, sizeof(nn_scrchr_t) * w * h);
    
    nn_addHeat(computer, gpu->ctrl.heatPerPixelChange * changes);
    nn_removeEnergy(computer, gpu->ctrl.energyPerPixelChange * changes);
    
    nn_addHeat(computer, gpu->ctrl.heatPerPixelReset * clears);
    nn_removeEnergy(computer, gpu->ctrl.energyPerPixelReset * clears);

    nn_simulateBufferedIndirect(component, 1, gpu->ctrl.screenCopyPerTick);

    nn_return(computer, nn_values_boolean(true));
}

void nni_gpu_getViewport(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    int w, h;
    nn_getViewport(gpu->currentScreen, &w, &h);
    nn_return(computer, nn_values_integer(w));
    nn_return(computer, nn_values_integer(h));
}
void nni_gpu_getDepth(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    nn_return(computer, nn_values_integer(gpu->currentScreen->depth));
}

const char *nn_depthName(int depth) {
    if(depth == 1) return "OneBit";
    if(depth == 4) return "FourBit";
    if(depth == 8) return "EightBit";
    if(depth == 16) return "SixteenBit";
    if(depth == 24) return "TwentyFourBit";
    return NULL;
}

void nni_gpu_setDepth(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    int depth = nn_toInt(nn_getArgument(computer, 0));
    int maxDepth = nn_maxDepth(gpu->currentScreen);

    if(nn_depthName(depth) == NULL) {
        nn_setCError(computer, "invalid depth");
        return;
    }

    if(depth > maxDepth) {
        nn_setCError(computer, "depth out of range");
        return;
    }

    int old = nn_getDepth(gpu->currentScreen);
    nn_setDepth(gpu->currentScreen, depth);
    
    nn_return_cstring(computer, nn_depthName(old));
}

void nni_gpu_maxDepth(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    nn_return(computer, nn_values_integer(gpu->currentScreen->maxDepth));
}

void nni_gpu_useless(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
	nn_return_boolean(computer, true);
}

void nn_loadGraphicsCardTable(nn_universe *universe) {
    nn_componentTable *gpuTable = nn_newComponentTable(nn_getAllocator(universe), "gpu", NULL, NULL, (nn_componentDestructor *)nni_gpuDeinit);
    nn_storeUserdata(universe, "NN:GPU", gpuTable);

    nn_method_t *method = NULL;

    method = nn_defineMethod(gpuTable, "bind", (nn_componentMethod *)nni_gpu_bind, "bind(addr: string[, reset: boolean = false]): boolean - Bind a GPU to a screen. Very expensive. If reset is true, it will clear the screen.");
    nn_method_setDirect(method, false);

    nn_defineMethod(gpuTable, "getScreen", (nn_componentMethod *)nni_gpu_getScreen, "getScreen(): string");
    nn_defineMethod(gpuTable, "set", (nn_componentMethod *)nni_gpu_set, "set(x: integer, y: integer, text: string[, vertical: boolean = false]) - Modifies the screen at a specific x or y. If vertical is false, it will display it horizontally. If it is true, it will display it vertically.");
    nn_defineMethod(gpuTable, "get", (nn_componentMethod *)nni_gpu_get, "get(x: integer, y: integer): string, integer, integer, integer?, integer? - Returns the character, foreground color, background color, foreground palette index (if applicable), background palette index (if applicable) of a pixel");
    nn_defineMethod(gpuTable, "maxResolution", (nn_componentMethod *)nni_gpu_maxResolution, "maxResolution(): integer, integer - Gets the maximum resolution supported by the bound screen.");
    nn_defineMethod(gpuTable, "getResolution", (nn_componentMethod *)nni_gpu_getResolution, "getResolution(): integer, integer - Gets the current resolution of the bound screen.");
    nn_defineMethod(gpuTable, "setResolution", (nn_componentMethod *)nni_gpu_setResolution, "maxResolution(): integer, integer - Changes the resolution of the bound screen.");
    nn_defineMethod(gpuTable, "setBackground", (nn_componentMethod *)nni_gpu_setBackground, "setBackground(color: integer, isPalette: boolean): integer, integer? - Sets the current background color. Returns the old one and palette index if applicable.");
    nn_defineMethod(gpuTable, "setForeground", (nn_componentMethod *)nni_gpu_setForeground, "setForeground(color: integer, isPalette: boolean): integer, integer? - Sets the current foreground color. Returns the old one and palette index if applicable.");
    nn_defineMethod(gpuTable, "getBackground", (nn_componentMethod *)nni_gpu_getBackground, "setBackground(color: integer, isPalette: boolean): integer, integer? - Sets the current background color. Returns the old one and palette index if applicable.");
    nn_defineMethod(gpuTable, "getForeground", (nn_componentMethod *)nni_gpu_getForeground, "setForeground(color: integer, isPalette: boolean): integer, integer? - Sets the current foreground color. Returns the old one and palette index if applicable.");
    nn_defineMethod(gpuTable, "getDepth", (nn_componentMethod *)nni_gpu_getDepth, "getDepth(): number - The currently set color depth of the screen, in bits. Can be 1, 4 or 8.");
    nn_defineMethod(gpuTable, "setDepth", (nn_componentMethod *)nni_gpu_setDepth, "setDepth(depth: integer): string - Changes the screen depth. Valid values can be 1, 4, 8, 16 or 24, however check maxDepth for the maximum supported value of the screen. Using a depth higher than what is supported by the screen will error. Returns the name of the new depth.");
    nn_defineMethod(gpuTable, "maxDepth", (nn_componentMethod *)nni_gpu_maxDepth, "maxDepth(): number - The maximum supported depth of the screen.");
    nn_defineMethod(gpuTable, "fill", (nn_componentMethod *)nni_gpu_fill, "fill(x: integer, y: integer, w: integer, h: integer, s: string)");
    nn_defineMethod(gpuTable, "copy", (nn_componentMethod *)nni_gpu_copy, "copy(x: integer, y: integer, w: integer, h: integer, tx: integer, ty: integer) - Copies stuff");
    nn_defineMethod(gpuTable, "getViewport", (nn_componentMethod *)nni_gpu_getViewport, "getViewport(): integer, integer - Gets the current viewport resolution");

    nn_defineMethod(gpuTable, "freeAllBuffers", (nn_componentMethod *)nni_gpu_useless, "dummy for now");
}

nn_component *nn_addGPU(nn_computer *computer, nn_address address, int slot, nn_gpuControl *control) {
    nn_componentTable *gpuTable = nn_queryUserdata(nn_getUniverse(computer), "NN:GPU");
    nni_gpu *gpu = nni_newGPU(nn_getAllocator(nn_getUniverse(computer)), control);
    if(gpu == NULL) {
        return NULL;
    }
    return nn_newComponent(computer, address, slot, gpuTable, gpu);
}
