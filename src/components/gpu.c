#include "../neonucleus.h"
#include "screen.h"

typedef struct nni_buffer {
	int width;
	int height;
	nn_scrchr_t *data;
} nni_buffer;

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
	int usedVRAM;
	int activeBuffer;
	int *vramIDBuf; // pre-allocated memory
	nni_buffer **buffers; // array of pointers
} nni_gpu;

// utils

nn_scrchr_t nni_gpu_makePixel(nni_gpu *gpu, const char *s) {
    return (nn_scrchr_t) {
        .codepoint = nn_unicode_codepointAt(s, 0),
        .fg = gpu->currentFg,
        .bg = gpu->currentBg,
        .isFgPalette = gpu->isFgPalette,
        .isBgPalette = gpu->isBgPalette,
    };
}

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

nn_size_t nni_vramNeededForSize(int w, int h) {
	return w * h;
}

nn_size_t nni_vramNeededForScreen(nn_screen *screen) {
	if(screen == NULL) return 0;
	int w, h;
	nn_maxResolution(screen, &w, &h);
	return nni_vramNeededForSize(w, h);
}

// VRAM

nni_buffer *nni_vram_newBuffer(nn_Alloc *alloc, int width, int height) {
	int area = width * height;
	nni_buffer *buf = nn_alloc(alloc, sizeof(nni_buffer));
	if(buf == NULL) {
		return NULL;
	}
	buf->width = width;
	buf->height = height;
	buf->data = nn_alloc(alloc, sizeof(nn_scrchr_t) * area);
	for(int i = 0; i < area; i++) {
		buf->data[i] = (nn_scrchr_t) {
			.codepoint = ' ',
			.fg = 0xFFFFFF,
			.bg = 0x000000,
			.isFgPalette = false,
			.isBgPalette = false,
		};
	}
	if(buf->data == NULL) {
		nn_dealloc(alloc, buf, sizeof(nni_buffer));
	}
	return buf;
}

void nni_vram_deinit(nn_Alloc *alloc, nni_buffer *buffer) {
	int area = buffer->width * buffer->height;
	nn_dealloc(alloc, buffer->data, sizeof(nn_scrchr_t) * area);
	nn_dealloc(alloc, buffer, sizeof(nni_buffer));
}

nn_bool_t nni_vram_inBounds(nni_buffer *buffer, int x, int y) {
	return
		x >= 0 &&
		y >= 0 &&
		x < buffer->width &&
		y < buffer->height
		;
}

nn_scrchr_t nni_vram_getPixel(nni_buffer *buffer, int x, int y) {
	if(!nni_vram_inBounds(buffer, x, y)) {
		return (nn_scrchr_t) {
			.codepoint = 0,
			.fg = 0xFFFFFF,
			.bg = 0x000000,
			.isFgPalette = false,
			.isBgPalette = false,
		};
	}
	return buffer->data[x + y * buffer->width];
}

void nni_vram_setPixel(nni_buffer *buffer, int x, int y, nn_scrchr_t pixel) {
	if(!nni_vram_inBounds(buffer, x, y)) return;
	buffer->data[x + y * buffer->width] = pixel;
}

void nni_vram_set(nni_gpu *gpu, int x, int y, const char *s, nn_bool_t vertical) {
	nni_buffer *buffer = gpu->buffers[gpu->activeBuffer - 1];

	nn_size_t cur = 0;
	while(s[cur]) {
		unsigned int cp = nn_unicode_nextCodepointPermissive(s, &cur);
		char encoded[NN_MAXIMUM_UNICODE_BUFFER];
		nn_unicode_codepointToChar(encoded, cp, NULL);
		nni_vram_setPixel(buffer, x, y, nni_gpu_makePixel(gpu, encoded));
		// peak software
		if(vertical) {
			y++;
		} else {
			x++;
		}
	}
}

void nni_vram_fill(nni_gpu *gpu, int x, int y, int w, int h, const char *s) {
	nni_buffer *buffer = gpu->buffers[gpu->activeBuffer - 1];
	// DoS mitigation
	if(x < 0) x = 0;
	if(y < 0) y = 0;
	if(w > buffer->width) w = buffer->width - x;
	if(h > buffer->height) h = buffer->height - y;

	nn_scrchr_t p = nni_gpu_makePixel(gpu, s);

	for(int py = 0; py < h; py++) {
		for(int px = 0; px < w; px++) {
			nni_vram_setPixel(buffer, px, py, p);
		}
	}
}

void nni_vram_copy(nni_gpu *gpu, int x, int y, int w, int h, int tx, int ty, nn_errorbuf_t err) {
	nni_buffer *buffer = gpu->buffers[gpu->activeBuffer - 1];
	// DoS mitigation
	if(x < 0) x = 0;
	if(y < 0) y = 0;
	if(w > buffer->width) w = buffer->width - x;
	if(h > buffer->height) h = buffer->height - y;

	nn_size_t tmpBufSize = sizeof(nn_scrchr_t) * w * h;
	nn_scrchr_t *tmpBuf = nn_alloc(&gpu->alloc, tmpBufSize);
	if(tmpBuf == NULL) {
		nn_error_write(err, "out of memory");
		return;
	}

	// copy
	for(int iy = 0; iy < h; iy++) {
		for(int ix = 0; ix < w; ix++) {
			tmpBuf[ix + iy * w] = nni_vram_getPixel(buffer, x, y);
		}
	}
	
	for(int iy = 0; iy < h; iy++) {
		for(int ix = 0; ix < w; ix++) {
			nn_scrchr_t p = tmpBuf[ix + iy * w];
			nni_vram_setPixel(buffer, x + ix + tx, y + iy + ty, p);
		}
	}

	nn_dealloc(&gpu->alloc, tmpBuf, tmpBufSize);
}

// GPU stuff

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
	gpu->vramIDBuf = nn_alloc(alloc, sizeof(int) * ctrl->maximumBufferCount);
	if(gpu->vramIDBuf == NULL) {
		nn_dealloc(alloc, gpu, sizeof(nni_gpu));
		return NULL;
	}
	// gpu->vramIDBuf can be left uninitialized! Its only tmp storage!
	gpu->buffers = nn_alloc(alloc, sizeof(nn_screen *) * ctrl->maximumBufferCount);
	if(gpu->buffers == NULL) {
		nn_dealloc(alloc, gpu->vramIDBuf, sizeof(int) * ctrl->maximumBufferCount);
		nn_dealloc(alloc, gpu, sizeof(nni_gpu));
		return NULL;
	}
	for(int i = 0; i < ctrl->maximumBufferCount; i++) {
		gpu->buffers[i] = NULL;
	}
	gpu->usedVRAM = 0;
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
	int maximumBufferCount = gpu->ctrl.maximumBufferCount;
	for(int i = 0; i < maximumBufferCount; i++) {
	}
	nn_dealloc(&a, gpu->vramIDBuf, sizeof(int) * maximumBufferCount);
	nn_dealloc(&a, gpu->buffers, sizeof(nn_screen) * maximumBufferCount);
    nn_dealloc(&a, gpu, sizeof(nni_gpu));
}

nn_bool_t nni_gpu_validActiveScreen(nni_gpu *gpu, int activeBuffer) {
	if(activeBuffer < 0 || activeBuffer > gpu->ctrl.maximumBufferCount) return false;
	return true;
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

	nn_screen *oldScreen = gpu->currentScreen;
	nn_size_t oldScreenVRAM = nni_vramNeededForScreen(oldScreen);
    nn_screen *screen = nn_getComponentUserdata(c);
	nn_size_t screenVRAM = nni_vramNeededForScreen(screen);

	if(gpu->usedVRAM - oldScreenVRAM + screenVRAM > gpu->ctrl.totalVRAM) {
		nn_setCError(computer, "out of vram");
		return;
	}

	gpu->usedVRAM -= oldScreenVRAM;
	gpu->usedVRAM += screenVRAM;

    nn_retainScreen(screen);
    if(oldScreen != NULL) nn_destroyScreen(oldScreen);
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
	// TODO: fix OOM here
    gpu->screenAddress = nn_strdup(&gpu->alloc, addr);

    nn_return(computer, nn_values_boolean(true));
}

void nni_gpu_set(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    int x = nn_toInt(nn_getArgument(computer, 0)) - 1;
    int y = nn_toInt(nn_getArgument(computer, 1)) - 1;
    const char *s = nn_toCString(nn_getArgument(computer, 2));
    nn_bool_t isVertical = nn_toBoolean(nn_getArgument(computer, 3));

    if(s == NULL) {
        nn_setCError(computer, "bad argument #3 (string expected in set)");
        return;
    }

	if(gpu->activeBuffer != 0) {
		nni_buffer *buffer = gpu->buffers[gpu->activeBuffer - 1];
		nni_vram_set(gpu, x, y, s, isVertical);
		return;
	}

    if(gpu->currentScreen == NULL) return;

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
    
    nn_return(computer, nn_values_integer(old));
    if(idx != -1) {
        nn_return(computer, nn_values_integer(idx));
    }
}

void nni_gpu_getForeground(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    nn_return(computer, nn_values_integer(gpu->currentFg));   
}

void nni_gpu_fill(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    int x = nn_toInt(nn_getArgument(computer, 0)) - 1;
    int y = nn_toInt(nn_getArgument(computer, 1)) - 1;
    int w = nn_toInt(nn_getArgument(computer, 2));
    int h = nn_toInt(nn_getArgument(computer, 3));
    const char *s = nn_toCString(nn_getArgument(computer, 4));
    if(s == NULL) {
        nn_setCError(computer, "bad argument #5 (character expected)");
        return;
    }
	
	if(gpu->activeBuffer != 0) {
		nni_vram_fill(gpu, x, y, w, h, s);
		return;
	}
	
	if(gpu->currentScreen == NULL) return;

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
    int x = nn_toInt(nn_getArgument(computer, 0)) - 1;
    int y = nn_toInt(nn_getArgument(computer, 1)) - 1;
    int w = nn_toInt(nn_getArgument(computer, 2));
    int h = nn_toInt(nn_getArgument(computer, 3));
    int tx = nn_toInt(nn_getArgument(computer, 4));
    int ty = nn_toInt(nn_getArgument(computer, 5));

	if(gpu->activeBuffer != 0) {
		nn_errorbuf_t err = "";
		nni_vram_copy(gpu, x, y, w, h, tx, ty, err);
		if(!nn_error_isEmpty(err)) {
			nn_setError(computer, err);
		}
		return;
	}
    
	if(gpu->currentScreen == NULL) return;

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

void nni_gpu_totalMemory(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    nn_return_integer(computer, gpu->ctrl.totalVRAM);
}

void nni_gpu_usedMemory(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    nn_return_integer(computer, gpu->usedVRAM);
}

void nni_gpu_freeMemory(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    nn_return_integer(computer, gpu->ctrl.totalVRAM - gpu->usedVRAM);
}

void nni_gpu_getActiveBuffer(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
    nn_return_integer(computer, gpu->activeBuffer);
}

void nni_gpu_setActiveBuffer(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
	int buf = nn_toInt(nn_getArgument(computer, 0));
	if(!nni_gpu_validActiveScreen(gpu, buf)) {
		nn_setCError(computer, "invalid buffer");
		return;
	}

	gpu->activeBuffer = buf;
	nn_return_integer(computer, buf);
}

void nni_gpu_buffers(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
	int bufCount = 0;
	for(int i = 0; i < gpu->ctrl.maximumBufferCount; i++) {
		if(gpu->buffers[i] != NULL) {
			gpu->vramIDBuf[bufCount] = i + 1;
			bufCount++;
		}
	}

	nn_value arr = nn_return_array(computer, bufCount);
	for(int i = 0; i < bufCount; i++) {
		nn_values_set(arr, i, nn_values_integer(gpu->vramIDBuf[i]));
	}
}

void nni_gpu_allocateBuffer(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
	int width = gpu->ctrl.defaultBufferWidth;
	int height = gpu->ctrl.defaultBufferHeight;

	nn_value widthVal = nn_getArgument(computer, 0);
	nn_value heightVal = nn_getArgument(computer, 1);

	if(widthVal.tag != NN_VALUE_NIL) {
		width = nn_toInt(widthVal);
	}
	if(heightVal.tag != NN_VALUE_NIL) {
		height = nn_toInt(heightVal);
	}

	if(width < 0 || height < 0) {
		nn_setCError(computer, "invalid size");
		return;
	}

	nn_size_t vramNeeded = nni_vramNeededForSize(width, height);

	if(gpu->usedVRAM + vramNeeded > gpu->ctrl.totalVRAM) {
		nn_setCError(computer, "out of vram");
		return;
	}

	nn_size_t idx = 0;
	for(nn_size_t i = 0; i < gpu->ctrl.maximumBufferCount; i++) {
		if(gpu->buffers[i] == NULL) {
			idx = i + 1;
			break;
		}
	}

	if(idx == 0) {
		nn_setCError(computer, "too many buffers");
		return;
	}

	nni_buffer *buf = nni_vram_newBuffer(&gpu->alloc, width, height);
	if(buf == NULL) {
		nn_setCError(computer, "out of memory");
		return;
	}
	gpu->buffers[idx - 1] = buf;

	gpu->usedVRAM += vramNeeded;
	
	nn_return_integer(computer, idx);
}

void nni_gpu_freeBuffer(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
	int bufidx = gpu->activeBuffer;
	nn_value bufVal = nn_getArgument(computer, 0);
	if(bufVal.tag != NN_VALUE_NIL) {
		bufidx = nn_toInt(bufVal);
	}

	if(!nni_gpu_validActiveScreen(gpu, bufidx) || bufidx == 0) {
		nn_setCError(computer, "invalid buffer");
		return;
	}

	nni_buffer *buf = gpu->buffers[bufidx - 1];
	if(buf == NULL) {
		nn_setCError(computer, "invalid buffer");
		return;
	}

	int vramUsed = buf->width * buf->height;
	nni_vram_deinit(&gpu->alloc, buf);
	gpu->buffers[bufidx - 1] = NULL;

	if(bufidx == gpu->activeBuffer) gpu->activeBuffer = 0;
	gpu->usedVRAM -= vramUsed;
}

void nni_gpu_freeAllBuffers(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
	gpu->activeBuffer = 0;
	
	for(nn_size_t i = 0; i < gpu->ctrl.maximumBufferCount; i++) {
		if(gpu->buffers[i] != NULL) {
			int vramUsed = gpu->buffers[i]->width * gpu->buffers[i]->height;
			nni_vram_deinit(&gpu->alloc, gpu->buffers[i]);
			gpu->buffers[i] = NULL;
			gpu->usedVRAM -= vramUsed;
		}
	}
}

void nni_gpu_getBufferSize(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
	int bufidx = gpu->activeBuffer;
	nn_value bufVal = nn_getArgument(computer, 0);
	if(bufVal.tag != NN_VALUE_NIL) {
		bufidx = nn_toInt(bufVal);
	}

	if(!nni_gpu_validActiveScreen(gpu, bufidx)) {
		nn_setCError(computer, "invalid buffer");
		return;
	}

	if(bufidx == 0) {
		if(gpu->currentScreen == NULL) {
			nn_setCError(computer, "invalid buffer");
			return;
		}
		int w, h;
		nn_getResolution(gpu->currentScreen, &w, &h);
		nn_return_integer(computer, w);
		nn_return_integer(computer, h);
		return;
	}

	nni_buffer *buf = gpu->buffers[bufidx - 1];
	if(buf == NULL) {
		nn_setCError(computer, "invalid buffer");
		return;
	}

	nn_return_integer(computer, buf->width);
	nn_return_integer(computer, buf->height);
}

void nni_gpu_bitblt(nni_gpu *gpu, void *_, nn_component *component, nn_computer *computer) {
	// I will kill OC creators for this
	int dst = nn_toIntOr(nn_getArgument(computer, 0), 0);
	int x = nn_toIntOr(nn_getArgument(computer, 1), 1);
	int y = nn_toIntOr(nn_getArgument(computer, 2), 1);
	int width = nn_toIntOr(nn_getArgument(computer, 3), 0);
	int height = nn_toIntOr(nn_getArgument(computer, 4), 0);
	int src = nn_toIntOr(nn_getArgument(computer, 5), gpu->activeBuffer);
	int fromCol = nn_toIntOr(nn_getArgument(computer, 6), 1);
	int fromRow = nn_toIntOr(nn_getArgument(computer, 7), 1);

	if(x < 1) x = 1;
	if(y < 1) y = 1;
	if(fromCol < 1) fromCol = 1;
	if(fromRow < 1) fromRow = 1;

	if(!nni_gpu_validActiveScreen(gpu, dst)) {
		nn_setCError(computer, "invalid destination buffer");
		return;
	}
	
	if(!nni_gpu_validActiveScreen(gpu, src)) {
		nn_setCError(computer, "invalid source buffer");
		return;
	}
	
	// such great parsing
	if(dst == 0) {
		if(gpu->currentScreen == NULL) return;
		int w, h;
		nn_getResolution(gpu->currentScreen, &w, &h);
		width = width == 0 ? w : width;
		height = height == 0 ? h : height;
	} else {
		nni_buffer *buffer = gpu->buffers[dst - 1];
		if(buffer == NULL) return;
		width = width == 0 ? buffer->width : width;
		height = height == 0 ? buffer->height : height;
	}

	if(dst == src) {
		nn_setCError(computer, "invalid operation, use copy() instead");
		return;
	}

	// from buffer to screen
	if(dst == 0 && src != 0) {
		nn_screen *screen = gpu->currentScreen;
		nni_buffer *buf = gpu->buffers[src - 1];
		if(buf == NULL) {
			nn_setCError(computer, "invalid source buffer");
			return;
		}
		
		int w, h;
		nn_getResolution(gpu->currentScreen, &w, &h);

		if(width > w) width = w;
		if(height > h) height = h;

		for(int j = 0; j < height; j++) {
			for(int i = 0; i < width; i++) {
				nn_scrchr_t src = nni_vram_getPixel(buf, i + fromCol - 1, j + fromRow - 1);
				nn_setPixel(screen, i + x - 1, j + y - 1, src);
			}
		}
		return;
	}
	// from screen to buffer 
	if(dst != 0 && src == 0) {
		nn_screen *screen = gpu->currentScreen;
		nni_buffer *buf = gpu->buffers[dst - 1];
		if(buf == NULL) {
			nn_setCError(computer, "invalid destination buffer");
			return;
		}
		
		if(width > buf->width) width = buf->width;
		if(height > buf->height) height = buf->height;

		for(int j = 0; j < height; j++) {
			for(int i = 0; i < width; i++) {
				nn_scrchr_t src = nn_getPixel(screen, i + fromCol - 1, j + fromRow - 1);
				nni_vram_setPixel(buf, i + x - 1, j + y - 1, src);
			}
		}
		return;
	}
	// from buffer to buffer
	if(dst != 0 && src != 0) {
		nni_buffer *srcBuf = gpu->buffers[src - 1];
		if(srcBuf == NULL) {
			nn_setCError(computer, "invalid destination buffer");
			return;
		}
		nni_buffer *destBuf = gpu->buffers[dst - 1];
		if(destBuf == NULL) {
			nn_setCError(computer, "invalid destination buffer");
			return;
		}
		
		if(width > destBuf->width) width = destBuf->width;
		if(height > destBuf->height) height = destBuf->height;

		for(int j = 0; j < height; j++) {
			for(int i = 0; i < width; i++) {
				nn_scrchr_t src = nni_vram_getPixel(srcBuf, i + fromCol - 1, j + fromRow - 1);
				nni_vram_setPixel(destBuf, i + x - 1, j + y - 1, src);
			}
		}
		return;
	}
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

	// VRAM buffers
    nn_defineMethod(gpuTable, "totalMemory", (nn_componentMethod *)nni_gpu_totalMemory, "totalMemory(): integer - Returns the VRAM capacity of the card");
    nn_defineMethod(gpuTable, "usedMemory", (nn_componentMethod *)nni_gpu_usedMemory, "usedMemory(): integer - Returns the amount of used VRAM");
    nn_defineMethod(gpuTable, "freeMemory", (nn_componentMethod *)nni_gpu_freeMemory, "freeMemory(): integer - Returns the amount of unused VRAM");
    nn_defineMethod(gpuTable, "buffers", (nn_componentMethod *)nni_gpu_buffers, "buffers(): integer[] - Returns the VRAM buffers allocated (not including the screen)");
    nn_defineMethod(gpuTable, "setActiveBuffer", (nn_componentMethod *)nni_gpu_setActiveBuffer, "setActiveBuffer(buffer: integer): integer - Changes the current buffer");
    nn_defineMethod(gpuTable, "getActiveBuffer", (nn_componentMethod *)nni_gpu_getActiveBuffer, "getActiveBuffer(): integer - Returns the current buffer");
    nn_defineMethod(gpuTable, "allocateBuffer", (nn_componentMethod *)nni_gpu_allocateBuffer, "allocateBuffer([width: integer, height: integer]): integer - Allocates a new VRAM buffer. Default size depends on GPU.");
    nn_defineMethod(gpuTable, "freeBuffer", (nn_componentMethod *)nni_gpu_freeBuffer, "freeBuffer([buffer: integer]): boolean - Frees a buffer. By default, the current buffer. If the current buffer is freed, it will switch back to the screen.");
    nn_defineMethod(gpuTable, "freeAllBuffers", (nn_componentMethod *)nni_gpu_freeAllBuffers, "freeAllBuffers() - Frees every VRAM buffer (if any). Also switches back to the screen.");
    nn_defineMethod(gpuTable, "getBufferSize", (nn_componentMethod *)nni_gpu_getBufferSize, "getBufferSize(buffer: integer): integer, integer - Returns the size of the specified buffer.");
    nn_defineMethod(gpuTable, "bitblt", (nn_componentMethod *)nni_gpu_bitblt, "bitblt([dst: integer, x: integer, y: integer, w: integer, h: integer, src: integer, fromCol: integer, fromRow: integer]) - Copy regions between buffers");
}

nn_component *nn_addGPU(nn_computer *computer, nn_address address, int slot, nn_gpuControl *control) {
    nn_componentTable *gpuTable = nn_queryUserdata(nn_getUniverse(computer), "NN:GPU");
    nni_gpu *gpu = nni_newGPU(nn_getAllocator(nn_getUniverse(computer)), control);
    if(gpu == NULL) {
        return NULL;
    }
    return nn_newComponent(computer, address, slot, gpuTable, gpu);
}
