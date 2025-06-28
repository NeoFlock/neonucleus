#include "../neonucleus.h"
#include "screen.h"
#include <stdio.h>
#include <string.h>

typedef struct nni_gpu {
    nn_screen *currentScreen;
    nn_address screenAddress;
    nn_gpuControl ctrl;
    int currentFg;
    int currentBg;
    bool isFgPalette;
    bool isBgPalette;
    // TODO: think about buffers and stuff
} nni_gpu;

bool nni_samePixel(nn_scrchr_t a, nn_scrchr_t b) {
    return
        a.codepoint == b.codepoint &&
        a.fg == b.fg &&
        a.bg == b.bg &&
        a.isFgPalette == b.isFgPalette &&
        a.isBgPalette == b.isBgPalette
        ;
}

bool nni_inBounds(nni_gpu *gpu, int x, int y) {
    if(gpu->currentScreen == NULL) return false;
    return
        x >= 0 &&
        y >= 0 &&
        x < gpu->currentScreen->width &&
        y < gpu->currentScreen->height &&
        true;
}

nni_gpu *nni_newGPU(nn_gpuControl *ctrl) {
    nni_gpu *gpu = nn_malloc(sizeof(nni_gpu));
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
    if(gpu->screenAddress != NULL) {
        nn_free(gpu->screenAddress);
    }
    nn_free(gpu);
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
    bool reset = false;
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
        for(size_t i = 0; i < screen->width; i++) {
            for(size_t j = 0; j < screen->height; j++) {
                nn_setPixel(screen, i, j, nni_gpu_makePixel(gpu, " "));
            }
        }
        size_t area = screen->width * screen->height;
        nn_addHeat(computer, gpu->ctrl.pixelResetHeat * area);
        nn_callCost(computer, gpu->ctrl.pixelResetCost * area);
        nn_removeEnergy(computer, gpu->ctrl.pixelResetEnergy * area);
        nn_busySleep(gpu->ctrl.pixelResetLatency * area);
    }

    gpu->currentScreen = screen;
    if(gpu->screenAddress != NULL) {
        nn_free(gpu->screenAddress);
    }
    gpu->screenAddress = nn_strdup(addr);

    nn_addHeat(computer, gpu->ctrl.bindHeat);
    nn_callCost(computer, gpu->ctrl.bindCost);
    nn_removeEnergy(computer, gpu->ctrl.bindEnergy);
    nn_busySleep(gpu->ctrl.bindLatency);

    nn_return(computer, nn_values_boolean(true));
}

void nni_gpu_set(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
    int x = nn_toInt(nn_getArgument(computer, 0)) - 1;
    int y = nn_toInt(nn_getArgument(computer, 1)) - 1;
    const char *s = nn_toCString(nn_getArgument(computer, 2));
    bool isVertical = nn_toBoolean(nn_getArgument(computer, 3));

    if(s == NULL) {
        nn_setCError(computer, "bad argument #3 (string expected in set)");
        return;
    }

    if(!nn_unicode_validate(s)) {
        nn_setCError(computer, "invalid utf-8");
        return;
    }

    int current = 0;
    while(s[current]) {
        int codepoint = nn_unicode_codepointAt(s, current);
        nn_setPixel(gpu->currentScreen, x, y, nni_gpu_makePixel(gpu, s + current));
        if(isVertical) {
            y++;
        } else {
            x++;
        }
        current += nn_unicode_codepointSize(codepoint);
    }
}

void nni_gpu_get(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->screenAddress == NULL) return;
    int x = nn_toInt(nn_getArgument(computer, 0)) - 1;
    int y = nn_toInt(nn_getArgument(computer, 1)) - 1;
    nn_scrchr_t pxl = nn_getPixel(gpu->currentScreen, x, y);

    size_t l;
    const char *chr = nn_unicode_codepointToChar(pxl.codepoint, &l);

    // TODO: gosh darn palettes
    nn_return(computer, nn_values_cstring(chr));
    nn_return(computer, nn_values_integer(pxl.fg));
    nn_return(computer, nn_values_integer(pxl.bg));
}

void nni_gpu_getScreen(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->screenAddress == NULL) return;
    nn_return(computer, nn_values_string(gpu->screenAddress, 0));
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

    bool changed = w != lw || h != lh;

    if(w <= 0) w = 1;
    if(h <= 0) h = 1;
    if(w > mw) w = mw;
    if(h > mh) h = mh;
    nn_setResolution(gpu->currentScreen, w, h);

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
    bool isPalette = nn_toBoolean(nn_getArgument(computer, 1));

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
    
    nn_addHeat(computer, gpu->ctrl.colorChangeHeat);
    nn_callCost(computer, gpu->ctrl.colorChangeCost);
    nn_removeEnergy(computer, gpu->ctrl.colorChangeEnergy);
    nn_busySleep(gpu->ctrl.colorChangeLatency);

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
    bool isPalette = nn_toBoolean(nn_getArgument(computer, 1));

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
    
    nn_addHeat(computer, gpu->ctrl.colorChangeHeat);
    nn_callCost(computer, gpu->ctrl.colorChangeCost);
    nn_removeEnergy(computer, gpu->ctrl.colorChangeEnergy);
    nn_busySleep(gpu->ctrl.colorChangeLatency);

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
    if(!nn_unicode_validate(s)) {
        nn_setCError(computer, "invalid utf-8");
        return;
    }

    int codepoint = nn_unicode_codepointAt(s, 0);

    // prevent DoS
    if(x < 0) x = 0;
    if(y < 0) y = 0;
    if(w > gpu->currentScreen->width - x) w = gpu->currentScreen->width - x;
    if(h > gpu->currentScreen->height - y) y = gpu->currentScreen->height - y;
    
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

    nn_addHeat(computer, gpu->ctrl.pixelChangeHeat * changes);
    nn_callCost(computer, gpu->ctrl.pixelChangeCost * changes);
    nn_removeEnergy(computer, gpu->ctrl.pixelChangeEnergy * changes);
    nn_busySleep(gpu->ctrl.pixelChangeLatency * changes);
    
    nn_addHeat(computer, gpu->ctrl.pixelChangeHeat * clears);
    nn_callCost(computer, gpu->ctrl.pixelChangeCost * clears);
    nn_removeEnergy(computer, gpu->ctrl.pixelChangeEnergy * clears);
    nn_busySleep(gpu->ctrl.pixelChangeLatency * clears);

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

    for(int cx = x; cx < x + w; cx++) {
        for(int cy = y; cy < y + h; cy++) {
            nn_scrchr_t src = nn_getPixel(gpu->currentScreen, cx, cy);
            nn_scrchr_t old = nn_getPixel(gpu->currentScreen, cx + tx, cy + ty);
            if(!nni_samePixel(old, src)) {
                nn_setPixel(gpu->currentScreen, cx + tx, cy + ty, src);
                if(src.codepoint == ' ')
                    clears++;
                else changes++;
            }
        }
    }

    nn_addHeat(computer, gpu->ctrl.pixelChangeHeat * changes);
    nn_callCost(computer, gpu->ctrl.pixelChangeCost * changes);
    nn_removeEnergy(computer, gpu->ctrl.pixelChangeEnergy * changes);
    nn_busySleep(gpu->ctrl.pixelChangeLatency * changes);
    
    nn_addHeat(computer, gpu->ctrl.pixelChangeHeat * clears);
    nn_callCost(computer, gpu->ctrl.pixelChangeCost * clears);
    nn_removeEnergy(computer, gpu->ctrl.pixelChangeEnergy * clears);
    nn_busySleep(gpu->ctrl.pixelChangeLatency * clears);

    nn_return(computer, nn_values_boolean(true));
}

void nni_gpu_getViewport(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    int w, h;
    nn_getViewport(gpu->currentScreen, &w, &h);
    nn_return(computer, nn_values_integer(w));
    nn_return(computer, nn_values_integer(h));
}

void nn_loadGraphicsCardTable(nn_universe *universe) {
    nn_componentTable *gpuTable = nn_newComponentTable("gpu", NULL, NULL, (void *)nni_gpuDeinit);
    nn_storeUserdata(universe, "NN:GPU", gpuTable);

    nn_defineMethod(gpuTable, "bind", false, (void *)nni_gpu_bind, NULL, "bind(addr: string[, reset: boolean = false]): boolean - Bind a GPU to a screen. Very expensive. If reset is true, it will clear the screen.");
    nn_defineMethod(gpuTable, "getScreen", true, (void *)nni_gpu_getScreen, NULL, "getScreen(): string");
    nn_defineMethod(gpuTable, "set", false, (void *)nni_gpu_set, NULL, "set(x: integer, y: integer, text: string[, vertical: boolean = false]) - Modifies the screen at a specific x or y. If vertical is false, it will display it horizontally. If it is true, it will display it vertically.");
    nn_defineMethod(gpuTable, "get", false, (void *)nni_gpu_get, NULL, "get(x: integer, y: integer): string, integer, integer, integer?, integer? - Returns the character, foreground color, background color, foreground palette index (if applicable), background palette index (if applicable) of a pixel");
    nn_defineMethod(gpuTable, "maxResolution", true, (void *)nni_gpu_maxResolution, NULL, "maxResolution(): integer, integer - Gets the maximum resolution supported by the bound screen.");
    nn_defineMethod(gpuTable, "getResolution", true, (void *)nni_gpu_getResolution, NULL, "getResolution(): integer, integer - Gets the current resolution of the bound screen.");
    nn_defineMethod(gpuTable, "setResolution", true, (void *)nni_gpu_setResolution, NULL, "maxResolution(): integer, integer - Changes the resolution of the bound screen.");
    nn_defineMethod(gpuTable, "setBackground", true, (void *)nni_gpu_setBackground, NULL, "setBackground(color: integer, isPalette: boolean): integer, integer? - Sets the current background color. Returns the old one and palette index if applicable.");
    nn_defineMethod(gpuTable, "setForeground", true, (void *)nni_gpu_setForeground, NULL, "setForeground(color: integer, isPalette: boolean): integer, integer? - Sets the current foreground color. Returns the old one and palette index if applicable.");
    nn_defineMethod(gpuTable, "getBackground", true, (void *)nni_gpu_getBackground, NULL, "setBackground(color: integer, isPalette: boolean): integer, integer? - Sets the current background color. Returns the old one and palette index if applicable.");
    nn_defineMethod(gpuTable, "getForeground", true, (void *)nni_gpu_getForeground, NULL, "setForeground(color: integer, isPalette: boolean): integer, integer? - Sets the current foreground color. Returns the old one and palette index if applicable.");
    nn_defineMethod(gpuTable, "fill", true, (void *)nni_gpu_fill, NULL, "fill(x: integer, y: integer, w: integer, h: integer, s: string)");
    nn_defineMethod(gpuTable, "copy", true, (void *)nni_gpu_copy, NULL, "copy(x: integer, y: integer, w: integer, h: integer, tx: integer, ty: integer) - Copies stuff");
    nn_defineMethod(gpuTable, "getViewport", true, (void *)nni_gpu_getViewport, NULL, "getViewport(): integer, integer - Gets the current viewport resolution");
}

nn_component *nn_addGPU(nn_computer *computer, nn_address address, int slot, nn_gpuControl *control) {
    nn_componentTable *gpuTable = nn_queryUserdata(nn_getUniverse(computer), "NN:GPU");
    nni_gpu *gpu = nni_newGPU(control);
    if(gpu == NULL) {
        return NULL;
    }
    return nn_newComponent(computer, address, slot, gpuTable, gpu);
}
