// The main file of the test emulator
// This is not a serious emulator intended for practical use,
// it is simply just to test stuff and showcase the API.
// Error handling has been omitted in most places.

#include "neonucleus.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <raylib.h>

nn_Architecture getLuaArch();

#if defined(NN_WINDOWS)
	#define NE_PATHSEP '\\'
	#include <windows.h>
	#error "Windows is not supported yet"
#elif defined(NN_POSIX)
	#define NE_PATHSEP '/'
	#include <dirent.h>
	#include <unistd.h>
	#include <sys/stat.h>

	typedef DIR ne_dir;

	ne_dir *ne_opendir(const char *path) {
		return opendir(path);
	}

	void ne_closedir(ne_dir *dir) {
		closedir(dir);
	}

	bool ne_readdir(ne_dir *dir, char path[NN_MAX_PATH]) {
		struct dirent *ent = readdir(dir);
		if(ent == NULL) return true;
		strncpy(path, ent->d_name, NN_MAX_PATH-1);
		return false;
	}

	bool ne_exists(const char *path) {
		return access(path, F_OK) == 0;
	}
	
	size_t ne_sizeAt(const char *path) {
		struct stat buf;
		if(stat(path, &buf) != 0) return 0;
		if(S_ISDIR(buf.st_mode)) return 0;
		return buf.st_size;
	}
	
	bool ne_isDirectory(const char *path) {
		struct stat buf;
		if(stat(path, &buf) != 0) return false;
		return S_ISDIR(buf.st_mode);
	}
	
	size_t ne_lastModified(const char *path) {
		struct stat buf;
		if(stat(path, &buf) != 0) return 0;
		return buf.st_mtime;
	}
#endif

static const char minBIOS[] = {
#embed "minBIOS.lua"
,'\0'
};

static nn_Exit sandbox_handler(nn_ComponentRequest *req) {
	nn_Computer *c = req->computer;
	switch(req->action) {
	case NN_COMP_INIT:
		return NN_OK;
	case NN_COMP_DEINIT:
		return NN_OK;
	case NN_COMP_CALL:
		if(nn_getstacksize(c) != 1) {
			nn_setError(c, "bad argument count");
			return NN_EBADCALL;
		}
		const char *s = nn_tostring(c, 0);
		puts(s);
		return NN_OK;
	case NN_COMP_ENABLED:
		req->methodEnabled = true; // all methods always enabled
		return NN_OK;
	case NN_COMP_FREETYPE:
		return NN_OK;
	}
	return NN_OK;
}

typedef struct ne_FsState {
	char path[NN_MAX_PATH];
	bool isReadonly;
	FILE *files[NN_MAX_OPENFILES];
	ne_dir *dir;
} ne_FsState;

void ne_fsState_truepath(ne_FsState *state, char truepath[NN_MAX_PATH], const char *path) {
	snprintf(truepath, sizeof(char) * NN_MAX_PATH, "%s%c%s", state->path, NE_PATHSEP, path);
	for(size_t i = 0; truepath[i] != 0; i++) {
		if(truepath[i] == '/') truepath[i] = NE_PATHSEP;
	}
}


nn_Exit ne_fsState_handler(nn_FilesystemRequest *req) {
	nn_Computer *C = req->computer;
	ne_FsState *state = req->instance;
	FILE *f;
	char truepath[NN_MAX_PATH];

	switch(req->action) {
	case NN_FS_DROP:
		for(size_t i = 0; i < NN_MAX_OPENFILES; i++) {
			if(state->files[i] != NULL) fclose(state->files[i]);
		}
		if(state->dir != NULL) {
			ne_closedir(state->dir);
		}
		free(state);
		return NN_OK;
	case NN_FS_SPACEUSED:
		req->size = 0;
		return NN_OK;
	case NN_FS_GETLABEL:
		req->strarg1 = NULL;
		return NN_OK;
	case NN_FS_SETLABEL:
		req->strarg1 = NULL;
		return NN_OK;
	case NN_FS_OPEN:;
		req->fd = NN_MAX_OPENFILES;

		for(size_t i = 0; i < NN_MAX_OPENFILES; i++) {
			if(state->files[i] == NULL) {
				req->fd = i;
				break;
			}
		}

		if(req->fd == NN_MAX_OPENFILES) {
			nn_setError(C, "too many open handles");
			return NN_EBADCALL;
		}

		const char *path = req->strarg1;
		const char *mode = req->strarg2;
		switch(mode[0]) {
			case 'r':
				mode = "rb";
			case 'w':
				mode = "wb";
			case 'a':
				mode = "ab";
			default:
				mode = "rb";
		}
		ne_fsState_truepath(state, truepath, path);

		f = fopen(truepath, mode);
		if(f == NULL) {
			nn_setError(C, strerror(errno));
			return NN_EBADCALL;
		}
		state->files[req->fd] = f;
		return NN_OK;
	case NN_FS_CLOSE:
		if(req->fd < 0 || req->fd >= NN_MAX_OPENFILES) {
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		f = state->files[req->fd];
		if(f == NULL) {
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		fclose(f);
		state->files[req->fd] = NULL;
		return NN_OK;
	case NN_FS_READ:
		if(req->fd < 0 || req->fd >= NN_MAX_OPENFILES) {
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		f = state->files[req->fd];
		if(f == NULL) {
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		if(feof(f)) {
			req->strarg1 = NULL;
		} else {
			req->strarg1len = fread(req->strarg1, sizeof(char), req->strarg1len, f);
		}
		return NN_OK;
	case NN_FS_WRITE:
		if(req->fd < 0 || req->fd >= NN_MAX_OPENFILES) {
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		f = state->files[req->fd];
		if(f == NULL) {
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		fwrite(req->strarg1, sizeof(char), req->strarg1len, f);
		return NN_OK;
	case NN_FS_OPENDIR:
		ne_fsState_truepath(state, truepath, req->strarg1);
		state->dir = ne_opendir(truepath);
		if(state->dir == NULL) {
			nn_setError(C, strerror(errno));
			return NN_EBADCALL;
		}
		return NN_OK;
	case NN_FS_READDIR:;
		char ent[NN_MAX_PATH];
		if(ne_readdir(state->dir, ent)) {
			req->strarg1 = NULL;
			return NN_OK;
		}
		strcpy(req->strarg1, ent);
		req->strarg1len = strlen(ent);
		return NN_OK;
	case NN_FS_CLOSEDIR:
		ne_closedir(state->dir);
		state->dir = NULL;
		return NN_OK;
	case NN_FS_EXISTS:
		ne_fsState_truepath(state, truepath, req->strarg1);
		req->size = ne_exists(truepath) ? 1 : 0;
		return NN_OK;
	case NN_FS_SIZE:
		ne_fsState_truepath(state, truepath, req->strarg1);
		if(!ne_exists(truepath)) {
			nn_setError(C, "no such file or directory");
			return NN_EBADCALL;
		}
		req->size = ne_sizeAt(truepath);
		return NN_OK;
	case NN_FS_LASTMODIFIED:
		ne_fsState_truepath(state, truepath, req->strarg1);
		if(!ne_exists(truepath)) {
			nn_setError(C, "no such file or directory");
			return NN_EBADCALL;
		}
		req->size = ne_lastModified(truepath);
		return NN_OK;
	case NN_FS_ISDIRECTORY:
		ne_fsState_truepath(state, truepath, req->strarg1);
		if(!ne_exists(truepath)) {
			nn_setError(C, "no such file or directory");
			return NN_EBADCALL;
		}
		req->size = ne_isDirectory(truepath) ? 1 : 0;
		return NN_OK;
	default:
		break;
	}
	nn_setError(C, "not implemented");
	return NN_EBADCALL;
}

ne_FsState *ne_newFS(const char *path, bool readonly) {
	ne_FsState *fs = malloc(sizeof(*fs));
	for(size_t i = 0; i < NN_MAX_OPENFILES; i++) {
		fs->files[i] = NULL;
	}
	sprintf(fs->path, "data%c%s", NE_PATHSEP, path);
	fs->isReadonly = readonly;
	return fs;
}

// this struct is quite wasteful and could be made like 10x better
// for performance. But like, this test emulator is ahh anyways
typedef struct ne_Pixel {
	int fg;
	int bg;
	int truefg;
	int truebg;
	nn_codepoint codepoint;
	bool isFgPalette;
	bool isBgPalette;
} ne_Pixel;

typedef struct ne_ScreenBuffer {
	int maxWidth;
	int maxHeight;
	int width;
	int height;
	char depth;
	char maxDepth;
	ne_Pixel *pixels;
	int maxPalette;
	int editableColors;
	int *virtualPalette;
	int *mappedPalette;
	const char *keyboard;
} ne_ScreenBuffer;

bool ne_ocCompatibleColors = true;

void ne_remapScreen(ne_ScreenBuffer *buf) {
	int depth = buf->maxDepth;

	for(int i = 0; i < buf->maxPalette; i++) {
		buf->mappedPalette[i] = nn_mapDepth(buf->virtualPalette[i], depth, ne_ocCompatibleColors);
	}

	for(int y = 0; y < buf->height; y++) {
		for(int x = 0; x < buf->width; x++) {
			ne_Pixel *pixel = &buf->pixels[y * buf->maxWidth + x];
			int virtfg = pixel->fg, virtbg = pixel->bg;
			if(pixel->isFgPalette) virtfg = buf->mappedPalette[virtfg];
			else virtfg = nn_mapDepth(virtfg, depth, ne_ocCompatibleColors);
			if(pixel->isBgPalette) virtbg = buf->mappedPalette[virtfg];
			else virtbg = nn_mapDepth(virtbg, depth, ne_ocCompatibleColors);

			pixel->truefg = virtfg;
			pixel->truebg = virtbg;
		}
	}
}

ne_ScreenBuffer *ne_newScreenBuf(nn_ScreenConfig conf, const char *keyboard) {
	ne_ScreenBuffer *buf = malloc(sizeof(*buf));
	buf->maxWidth = conf.maxWidth;
	buf->maxHeight = conf.maxHeight;
	buf->width = buf->maxWidth;
	buf->height = buf->maxHeight;
	buf->maxDepth = conf.maxDepth;
	buf->depth = buf->maxDepth;
	buf->maxPalette = conf.paletteColors;
	buf->pixels = malloc(sizeof(ne_Pixel) * conf.maxWidth * conf.maxHeight);
	buf->virtualPalette = malloc(sizeof(int) * conf.paletteColors);
	memset(buf->virtualPalette, 0, sizeof(int) * buf->maxPalette);
	buf->mappedPalette = malloc(sizeof(int) * conf.paletteColors);
	buf->keyboard = keyboard;

	int *palette = NULL;
	if(buf->maxDepth == 4) {
		palette = nn_mcpalette4;
	}
	if(buf->maxDepth == 8) {
		palette = nn_ocpalette8;
	}
	if(palette) memcpy(buf->virtualPalette, palette, sizeof(int) * buf->maxPalette);
	memcpy(buf->mappedPalette, buf->virtualPalette, sizeof(int) * buf->maxPalette);

	for(int y = 0; y < buf->height; y++) {
		for(int x = 0; x < buf->width; x++) {
			buf->pixels[y * buf->width + x] = (ne_Pixel) {
				.fg = 0xFFFFFF,
				.bg = 0x000000,
				.isFgPalette = false,
				.isBgPalette = false,
				.codepoint = ' ',
				.truefg = 0xFFFFFF,
				.truebg = 0x000000,
			};
		}
	}
	return buf;
}

void ne_dropScreenBuf(ne_ScreenBuffer *buf) {
	free(buf->pixels);
	free(buf->mappedPalette);
	free(buf->virtualPalette);
	free(buf);
}

ne_Pixel defaultPixel = {
	.codepoint = ' ',
	.fg = 0xFFFFFF,
	.bg = 0x000000,
	.isFgPalette = false,
	.isBgPalette = false,
	.truefg = 0xFFFFFF,
	.truebg = 0x000000,
};

bool ne_inScreenBuf(ne_ScreenBuffer *buf, int x, int y) {
	return x > 0 && y > 0 && x <= buf->width && y <= buf->height;
}

ne_Pixel ne_getPixel(ne_ScreenBuffer *buf, int x, int y) {
	if(!ne_inScreenBuf(buf, x, y)) return defaultPixel;
	x--;
	y--;
	return buf->pixels[y * buf->maxWidth + x];
}

void ne_setPixel(ne_ScreenBuffer *buf, int x, int y, ne_Pixel pixel) {
	if(!ne_inScreenBuf(buf, x, y)) return;
	x--;
	y--;
	buf->pixels[y * buf->maxWidth + x] = pixel;
}

nn_Exit ne_screen_handler(nn_ScreenRequest *req) {
	ne_ScreenBuffer *buf = req->instance;
	switch(req->action) {
	case NN_SCR_DROP:
		// this'd require the GPU is dropped first...
		// its best we just dont bother and free later
		//ne_dropScreenBuf(buf);
		return NN_OK;
	case NN_SCR_GETASPECTRATIO:
		req->w = 1;
		req->h = 1;
		return NN_OK;
	case NN_SCR_GETKEYBOARD:
		if(buf->keyboard == NULL) {
			req->keyboard = NULL;
			return NN_OK;
		}
		size_t keylen = strlen(buf->keyboard);
		if(keylen > req->w) keylen = req->w;
		memcpy(req->keyboard, buf->keyboard, keylen);
		req->w = keylen;
		return NN_OK;
	case NN_SCR_ISON:
		req->w = 1;
		return NN_OK;
	case NN_SCR_TURNON:
		req->w = 1;
		req->h = 1;
		return NN_OK;
	case NN_SCR_TURNOFF:
		req->w = 1;
		req->h = 1;
		return NN_OK;
	case NN_SCR_ISPRECISE:
		req->w = 0;
		return NN_OK;
	case NN_SCR_SETPRECISE:
		req->w = 0;
		return NN_OK;
	case NN_SCR_ISTOUCHINVERTED:
		req->w = 0;
		return NN_OK;
	case NN_SCR_SETTOUCHINVERTED:
		req->w = 0;
		return NN_OK;
	}
	return NN_OK;
}

#define NE_MAX_VRAMBUF 16

typedef struct ne_GPUState {
	ne_ScreenBuffer *screenBuf;
	int currentFg;
	int currentBg;
	bool isFgPalette;
	bool isBgPalette;
	int freeMemory;
	int activeBuffer;
	int scrAddrLen;
	char scrAddr[NN_MAX_ADDRESS];
	ne_ScreenBuffer *vramBufs[NE_MAX_VRAMBUF];
} ne_GPUState;

ne_GPUState *ne_newGPU(nn_GPU gpu) {
	ne_GPUState *state = malloc(sizeof(*state));
	state->screenBuf = NULL;
	state->currentFg = 0xFFFFFF;
	state->currentBg = 0x000000;
	state->isFgPalette = false;
	state->isBgPalette = false;
	state->activeBuffer = 0;
	state->freeMemory = gpu.totalVRAM;
	for(int i = 0; i < NE_MAX_VRAMBUF; i++) {
		state->vramBufs[i] = NULL;
	}
	return state;
}

ne_ScreenBuffer *ne_gpu_currentBuffer(ne_GPUState *state) {
	if(state->activeBuffer == 0) return state->screenBuf;
	return state->vramBufs[state->activeBuffer - 1];
}

nn_Exit ne_gpu_handler(nn_GPURequest *req) {
	nn_Computer *C = req->computer;
	ne_GPUState *state = req->instance;

	int maxWidth = req->gpuConf->maxWidth;
	int maxHeight = req->gpuConf->maxHeight;
	int maxDepth = req->gpuConf->maxDepth;

	ne_ScreenBuffer *activeBuf = ne_gpu_currentBuffer(state);

	if(state->screenBuf != NULL) {
		ne_ScreenBuffer *buf = state->screenBuf;
		if(maxWidth > buf->maxWidth) maxWidth = buf->maxWidth;
		if(maxHeight > buf->maxHeight) maxHeight = buf->maxHeight;
		if(maxDepth > buf->maxDepth) maxDepth = buf->maxDepth;
	}

	int x, y, dx, dy, w, h, fg, bg;
	ne_Pixel p;

	switch(req->action) {
	case NN_GPU_DROP:
		for(int i = 0; i < NE_MAX_VRAMBUF; i++) {
			ne_ScreenBuffer *buf = state->vramBufs[i];
			if(buf != NULL) ne_dropScreenBuf(buf);
		}
		free(state);
		return NN_OK;
	case NN_GPU_BIND:
		state->screenBuf = nn_getComponentUserdata(C, req->text);
		memcpy(state->scrAddr, req->text, req->width);
		state->scrAddrLen = req->width;
		return NN_OK;
	case NN_GPU_UNBIND:
		state->screenBuf = NULL;
		return NN_OK;
	case NN_GPU_GETSCREEN:
		if(state->screenBuf == NULL) {
			req->text = NULL;
			return NN_OK;
		}
		memcpy(req->text, state->scrAddr, state->scrAddrLen);
		req->width = state->scrAddrLen;
		return NN_OK;
	case NN_GPU_GETRESOLUTION:
		if(state->screenBuf == NULL) {
			nn_setError(C, "no screen");
			return NN_EBADCALL;
		}
		req->width = state->screenBuf->width;
		req->height = state->screenBuf->height;
		return NN_OK;
	case NN_GPU_GET:
		if(activeBuf == NULL) {
			nn_setError(C, "no screen");
			return NN_EBADCALL;
		}
		p = ne_getPixel(activeBuf, req->x, req->y);
		fg = p.fg;
		bg = p.bg;
		if(p.isFgPalette) fg = activeBuf->virtualPalette[fg];
		if(p.isBgPalette) bg = activeBuf->virtualPalette[bg];

		req->codepoint = p.codepoint;
		req->width = fg;
		req->height = bg;
		req->dest = p.isFgPalette ? p.fg : -1;
		req->src = p.isBgPalette ? p.bg : -1;
		return NN_OK;
	case NN_GPU_SET:
	case NN_GPU_SETVERTICAL:
		if(activeBuf == NULL) {
			nn_setError(C, "no screen");
			return NN_EBADCALL;
		}
		dx = 1;
		dy = 0;
		if(req->action == NN_GPU_SETVERTICAL) dx = 0, dy = 1;

		x = req->x;
		y = req->y;
		for(int i = 0; i < req->width; i++) {
			if(!ne_inScreenBuf(activeBuf, x, y)) break;
			ne_Pixel p = {
				.fg = state->currentFg,
				.bg = state->currentBg,
				.isFgPalette = state->isFgPalette,
				.isBgPalette = state->isBgPalette,
				.codepoint = req->text[i],
			};
			ne_setPixel(activeBuf, x, y, p);
			x += dx;
			y += dy;
		}
		ne_remapScreen(activeBuf);
		return NN_OK;
	case NN_GPU_FILL:
		if(activeBuf == NULL) {
			nn_setError(C, "no screen");
			return NN_EBADCALL;
		}
		x = req->x;
		y = req->y;
		w = req->width;
		h = req->height;
		if(x < 1) x = 1;
		if(y < 1) y = 1;
		if(w >= activeBuf->width) w = activeBuf->width - 1;
		if(h >= activeBuf->height) h = activeBuf->height - 1;

		p = (ne_Pixel) {
			.fg = state->currentFg,
			.bg = state->currentBg,
			.isFgPalette = state->isFgPalette,
			.isBgPalette = state->isBgPalette,
			.codepoint = req->codepoint,
		};
		for(int oy = 0; oy < h; oy++) {
			for(int ox = 0; ox < w; ox++) {
				ne_setPixel(activeBuf, x + ox, y + oy, p);
			}
		}
		return NN_OK;
	}
	return NN_OK;
}

Color ne_processColor(unsigned int color) {
    color <<= 8;
    color |= 0xFF;
    return GetColor(color);
}

int main() {
	nn_Context ctx;
	nn_initContext(&ctx);
	nn_initPalettes();

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(800, 600, "NeoNucleus Test Emulator");

	// create the universe
	nn_Universe *u = nn_createUniverse(&ctx);

	nn_Architecture arch = getLuaArch();

	nn_Method sandboxMethods[] = {
		{"log", "log(msg: string) - Log to stdout", true},
		{NULL},
	};
	nn_ComponentType *ctype = nn_createComponentType(u, "sandbox", NULL, sandboxMethods, sandbox_handler);

	nn_VEEPROM veeprom = {
		.code = minBIOS,
		.codelen = strlen(minBIOS),
		.data = NULL,
		.datalen = 0,
		.label = NULL,
		.labellen = 0,
		.arch = NULL,
		.isReadonly = false,
	};

	nn_ComponentType *etype = nn_createVEEPROM(u, &nn_defaultEEPROM, &veeprom);
	nn_ComponentType *fstype[5];
	fstype[0] = nn_createFilesystem(u, &nn_defaultFloppy, ne_fsState_handler, NULL);
	for(size_t i = 1; i < 5; i++) {
		fstype[i] = nn_createFilesystem(u, &nn_defaultFilesystems[i-1], ne_fsState_handler, NULL);
	}
	nn_ComponentType *scrtype = nn_createScreen(u, ne_screen_handler, NULL);
	nn_ComponentType *keytype = nn_createKeyboard(u);
	nn_ComponentType *gputype = nn_createGPU(u, &nn_defaultGPUs[3], ne_gpu_handler, NULL);

	nn_Computer *c = nn_createComputer(u, NULL, "computer0", 8 * NN_MiB, 256, 256);
	
	nn_setArchitecture(c, &arch);
	nn_addSupportedArchitecture(c, &arch);

	nn_addComponent(c, ctype, "sandbox", -1, NULL);
	nn_addComponent(c, etype, "eeprom", 0, etype);

	ne_FsState *mainFS = ne_newFS("OpenOS", false);
	nn_addComponent(c, fstype[1], "mainFS", 2, mainFS);

	nn_addComponent(c, keytype, "mainKB", 4, NULL);
	ne_ScreenBuffer *scrbuf = ne_newScreenBuf(nn_defaultScreens[1], "mainKB");
	nn_addComponent(c, scrtype, "mainScreen", -1, scrbuf);

	ne_GPUState *gpu = ne_newGPU(nn_defaultGPUs[3]);
	nn_addComponent(c, gputype, "mainGPU", 3, gpu);

	SetExitKey(KEY_NULL);

	Font font = LoadFont("unscii-16-full.ttf");
	double tickDelay = 0.05;
	double tickClock = 0;

	while(true) {
		if(WindowShouldClose()) break;

		BeginDrawing();
		ClearBackground(BLACK);

		int scrW = scrbuf->width;
		int scrH = scrbuf->height;

		int pixelHeight = GetScreenHeight() / scrH;
		float spacing = (float)pixelHeight/10;
		int pixelWidth = MeasureTextEx(font, "A", pixelHeight, spacing).x;

		int depth = scrbuf->depth;

		int offX = (GetScreenWidth() - scrW * pixelWidth) / 2;
		int offY = (GetScreenHeight() - scrH * pixelHeight) / 2;

		for(int y = 0; y < scrH; y++) {
			for(int x = 0; x < scrW; x++) {
				ne_Pixel p = ne_getPixel(scrbuf, x+1, y+1);

				Color fgColor = ne_processColor(p.truefg);
				Color bgColor = ne_processColor(p.truebg);

				DrawRectangle(x * pixelWidth + offX, y * pixelHeight + offY, pixelWidth, pixelHeight, bgColor);
				DrawTextCodepoint(font, p.codepoint, (Vector2) {x * pixelWidth + offX, y * pixelHeight + offY}, pixelHeight - 5, fgColor);
			}
		}
		EndDrawing();

		tickClock -= GetFrameTime();

		if(tickClock <= 0) {
			tickClock = tickDelay;

			nn_Exit e = nn_tick(c);
			if(e != NN_OK) {
				nn_setErrorFromExit(c, e);
				printf("error: %s\n", nn_getError(c));
				goto cleanup;
			}

			nn_ComputerState state = nn_getComputerState(c);
			if(state == NN_POWEROFF) break;
			if(state == NN_CRASHED) {
				printf("error: %s\n", nn_getError(c));
				goto cleanup;
			}

			if(state == NN_CHARCH) {
				printf("new arch: %s\n", nn_getDesiredArchitecture(c).name);
				goto cleanup;
			}
			if(state == NN_BLACKOUT) {
				printf("out of energy\n");
				goto cleanup;
			}
			if(state == NN_RESTART) {
				printf("restart requested\n");
				goto cleanup;
			}
		}
	}

cleanup:;
	nn_destroyComputer(c);
	nn_destroyComponentType(ctype);
	nn_destroyComponentType(etype);
	nn_destroyComponentType(scrtype);
	nn_destroyComponentType(keytype);
	nn_destroyComponentType(gputype);
	for(size_t i = 0; i < 5; i++) nn_destroyComponentType(fstype[i]);
	ne_dropScreenBuf(scrbuf);
	// rip the universe
	nn_destroyUniverse(u);
	UnloadFont(font);
	CloseWindow();
	return 0;
}
