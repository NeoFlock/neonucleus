#include "../neonucleus.h"
#include "screen.h"

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

    if(gpu->currentScreen != NULL) {
        nn_destroyScreen(gpu->currentScreen);
    }
    nn_screen *screen = nn_getComponentUserdata(c);

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

}

void nni_gpu_get(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {

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
    nn_maxResolution(gpu->currentScreen, &w, &h);
    nn_return(computer, nn_values_integer(w));
    nn_return(computer, nn_values_integer(h));
}

void nni_gpu_setResolution(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    if(gpu->currentScreen == NULL) return;
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
}

nn_component *nn_addGPU(nn_computer *computer, nn_address address, int slot, nn_gpuControl *control) {
    nn_componentTable *gpuTable = nn_queryUserdata(nn_getUniverse(computer), "NN:GPU");
    nni_gpu *gpu = nni_newGPU(control);
    if(gpu == NULL) {
        return NULL;
    }
    return nn_newComponent(computer, address, slot, gpuTable, gpu);
}
