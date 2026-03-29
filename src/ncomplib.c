#include "neonucleus.h"
#include "ncomplib.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static bool ncl_defaultHandler(ncl_VFSRequest *request);

#ifdef NN_BAREMETAL
bool ncl_defaultHandler(ncl_VFSRequest *request) {
	return false; // all fails
}
#else
#include <stdio.h>

#ifdef NN_POSIX

#include <dirent.h>
#include <sys/stat.h>

#elif defined(NN_WINDOWS)
#error "Windows is not supported yet"
#endif

bool ncl_defaultHandler(ncl_VFSRequest *request) {
	if(request->action == NCL_VFS_OPEN) {
		char mode[3] = "rb";
		mode[0] = request->open.mode[0];
		FILE *f = fopen(request->open.path, mode);
		request->open.file = f;
		return f != NULL;
	}
	if(request->action == NCL_VFS_CLOSE) {
		fclose(request->close);
		return true;
	}
	if(request->action == NCL_VFS_READ) {
		FILE *f = request->read.file;
		if(feof(f)) {
			request->read.buf = NULL;
			return true;
		}
		request->read.len = fread(request->read.buf, sizeof(char), request->read.len, f);
		return true;
	}
	if(request->action == NCL_VFS_WRITE) {
		FILE *f = request->write.file;
		size_t written = fwrite(request->write.buf, sizeof(char), request->write.len, f);
		return written == request->write.len;
	}
	if(request->action == NCL_VFS_SEEK) {
		FILE *f = request->seek.file;
		nn_FSWhence wanted = request->seek.whence;
		int whence = SEEK_SET;
		if(wanted == NN_SEEK_SET) whence = SEEK_SET;
		if(wanted == NN_SEEK_CUR) whence = SEEK_CUR;
		if(wanted == NN_SEEK_END) whence = SEEK_END;
		if(fseek(f, whence, request->seek.off) < 0) return false;
		request->seek.off = ftell(f);
		return true;
	}
	if(request->action == NCL_VFS_REMOVE) {
		return remove(request->remove) == 0;
	}
#ifdef NN_POSIX
	if(request->action == NCL_VFS_OPENDIR) {
		DIR *d = opendir(request->opendir.path);
		request->opendir.dir = d;
		return d != NULL;
	}
	if(request->action == NCL_VFS_CLOSEDIR) {
		DIR *d = request->closedir;
		closedir(d);
		return true;
	}
	if(request->action == NCL_VFS_READDIR) {
		DIR *d = request->readdir.dir;
		struct dirent *ent = readdir(d);
		if(ent == NULL) {
			request->readdir.name = NULL;
		} else {
			strncpy(request->readdir.name, ent->d_name, NN_MAX_PATH-1);
		}
		return true;
	}
	if(request->action == NCL_VFS_STAT) {
		struct stat s;
		if(stat(request->stat.path, &s) != 0) {
			request->stat.path = NULL;
			return false;
		}
		ncl_Stat *stat = request->stat.stat;
		stat->isDirectory = S_ISDIR(s.st_mode);
		stat->diskSize = s.st_blocks * 512;
		stat->size = 0;
		if(!stat->isDirectory) {
			FILE *f = fopen(request->stat.path, "r");
			if(f == NULL) {
				// horribly off but don't care atp
				stat->size = s.st_size;
			} else {
				fseek(f, SEEK_END, 0);
				stat->size = ftell(f);
				fclose(f);
			}
		}
		stat->lastModified = s.st_mtime;
		return true;
	}
#endif
	return false; // not supported
}
#endif

ncl_VFS ncl_defaultFS = (ncl_VFS) {
	.state = NULL,
	.handler = ncl_defaultHandler,
#ifdef NN_WINDOWS
	.pathsep = '\\',
#else
	.pathsep = '/',
#endif
	.fileCost = 512,
};

void *ncl_openfile(ncl_VFS vfs, const char *path, const char *mode) {
	ncl_VFSRequest req;
	req.state = vfs.state;
	req.action = NCL_VFS_OPEN;
	req.open.path = path;
	req.open.mode = mode;
	if(!vfs.handler(&req)) return NULL;
	return req.open.file;
}

void ncl_closefile(ncl_VFS vfs, void *file) {
	ncl_VFSRequest req;
	req.state = vfs.state;
	req.action = NCL_VFS_CLOSE;
	req.close = file;
	vfs.handler(&req);
}

bool ncl_readfile(ncl_VFS vfs, void *file, char *buf, size_t *len);
bool ncl_writefile(ncl_VFS vfs, void *file, const char *data, size_t len);
bool ncl_seekfile(ncl_VFS vfs, void *file, nn_FSWhence whence, int *off);
bool ncl_stat(ncl_VFS vfs, const char *path, ncl_Stat *stat) {
	ncl_VFSRequest req;
	req.state = vfs.state;
	req.action = NCL_VFS_STAT;
	req.stat.path = path;
	req.stat.stat = stat;
	if(!vfs.handler(&req)) return false;
	if(req.stat.path == NULL) return false;
	return true;
}

void *ncl_opendir(ncl_VFS vfs, const char *path) {
	ncl_VFSRequest req;
	req.state = vfs.state;
	req.action = NCL_VFS_OPENDIR;
	req.opendir.path = path;
	if(!vfs.handler(&req)) return NULL;
	return req.opendir.dir;
}

void ncl_closedir(ncl_VFS vfs, void *dir) {
	ncl_VFSRequest req;
	req.state = vfs.state;
	req.action = NCL_VFS_CLOSEDIR;
	req.closedir = dir;
	vfs.handler(&req);
}

bool ncl_readdir(ncl_VFS vfs, void *dir, char name[NN_MAX_PATH]) {
	ncl_VFSRequest req;
	req.state = vfs.state;
	req.action = NCL_VFS_READDIR;
	req.readdir.dir = dir;
	req.readdir.name = name;
	if(!vfs.handler(&req)) return false;
	if(req.readdir.name == NULL) return false;
	return true;
}

size_t ncl_spaceUsedIn(ncl_VFS vfs, const char *path) {
	ncl_Stat s;
	if(!ncl_stat(vfs, path, &s)) return 0;
	if(!s.isDirectory) return vfs.fileCost + s.size;
	size_t spaceUsed = vfs.fileCost;
	void *dir = ncl_opendir(vfs, path);
	if(dir == NULL) return spaceUsed;
	char name[NN_MAX_PATH];
	while(ncl_readdir(vfs, dir, name)) {
		if(strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
			char subpath[NN_MAX_PATH];
			snprintf(subpath, sizeof(subpath), "%s%c%s", path, vfs.pathsep, name);
			spaceUsed += ncl_spaceUsedIn(vfs, subpath);
		}
	}
	ncl_closedir(vfs, dir);
	return spaceUsed;
}

size_t ncl_spaceUsedBy(ncl_VFS vfs, const char *path) {
	ncl_Stat s;
	if(!ncl_stat(vfs, path, &s)) return 0;
	size_t spaceUsed = s.diskSize;
	if(!s.isDirectory) return s.diskSize;
	void *dir = ncl_opendir(vfs, path);
	if(dir == NULL) return spaceUsed;
	char name[NN_MAX_PATH];
	while(ncl_readdir(vfs, dir, name)) {
		if(strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
			char subpath[NN_MAX_PATH];
			snprintf(subpath, sizeof(subpath), "%s%c%s", path, vfs.pathsep, name);
			spaceUsed += ncl_spaceUsedBy(vfs, subpath);
		}
	}
	ncl_closedir(vfs, dir);
	return spaceUsed;
}

typedef struct ncl_ScreenPixel {
	nn_codepoint codepoint;
	int storedFg;
	int storedBg;
	// if negative, its in palette
	int realFg;
	// if negative, its in palette
	int realBg;
} ncl_ScreenPixel;

typedef struct ncl_ScreenState {
	nn_Context *ctx;
	nn_ScreenConfig conf;
	int width;
	int height;
	int viewportWidth;
	int viewportHeight;
	char depth;
	int *palette;
	int *resolvedPalette;
	ncl_ScreenPixel *pixels;
	ncl_ScreenFlags flags;
} ncl_ScreenState;

typedef struct nn_VRAMBuf {
	int width;
	int height;
	ncl_ScreenPixel pixels[];
} ncl_VRAMBuf;

typedef struct ncl_GPUState {
	nn_Context *ctx;
	nn_Lock *lock;
	nn_GPU conf;
	size_t vramFree;
	ncl_VRAMBuf *vram[NCL_MAX_VRAMBUF];
	char *screenAddress;
	int currentFg;
	int currentBg;
	int activeBuffer;
	bool isFgPalette;
	bool isBgPalette;
} ncl_GPUState;

static void ncl_freeVRAM(nn_Context *ctx, ncl_VRAMBuf *buf) {
	nn_free(ctx, buf, sizeof(ncl_VRAMBuf) + sizeof(ncl_ScreenPixel) * buf->width * buf->height);
}

static ncl_VRAMBuf *ncl_allocVRAM(nn_Context *ctx, int width, int height) {
	ncl_VRAMBuf *buf = nn_alloc(ctx, sizeof(ncl_VRAMBuf) + sizeof(ncl_ScreenPixel) * width * height);
	if(buf == NULL) return NULL;
	buf->width = width;
	buf->height = height;
	for(int i = 0; i < width*height; i++) {
		buf->pixels[i] = (ncl_ScreenPixel) {
			.codepoint = ' ',
			.storedFg = 0xFFFFFF,
			.storedBg = 0x000000,
			.realFg = 0xFFFFFF,
			.realBg = 0x000000,
		};
	}
	return buf;
}

typedef struct ncl_FSState {
	nn_Context *ctx;
	nn_Lock *lock;
	nn_Filesystem conf;
	char *path;
	ncl_VFS vfs;
	// if 0, needs to be recomputed
	size_t spaceUsed;
	// if 0, needs to be recomputed
	size_t realSpaceUsed;
	atomic_size_t usage;
	bool isReadonly;
	// all the arrays
	FILE *fds[NN_MAX_OPENFILES];
	void *dirs[NN_MAX_OPENFILES];
	char label[NN_MAX_LABEL];
	size_t labellen;
} ncl_FSState;

typedef struct ncl_DriveState {
	nn_Context *ctx;
	nn_Lock *lock;
	nn_Drive conf;
	bool isReadonly;
	atomic_size_t usage;
	size_t lastSector;
	char *path;
	FILE *file;
	char label[NN_MAX_LABEL];
	size_t labellen;
} ncl_DriveState;

typedef struct ncl_EEState {
	nn_Context *ctx;
	nn_Lock *lock;
	nn_EEPROM conf;
	bool isReadonly;
	atomic_size_t usage;
	char *codepath;
	char *datapath;
	char label[NN_MAX_LABEL];
	size_t labellen;
} ncl_EEState;

static void ncl_fixPath(ncl_VFS vfs, const char *path, char buf[NN_MAX_PATH]) {
	size_t len = 0;
	while(path[len]) {
		if(len == NN_MAX_PATH) break;
		if(path[len] == '/') buf[len] = vfs.pathsep;
		else buf[len] = path[len];
		len++;
	}
	buf[len] = '\0';
}

// assumes locked
static size_t ncl_fsGetUsage(ncl_FSState *fs) {
	if(fs->spaceUsed == 0) {
		fs->spaceUsed = ncl_spaceUsedIn(fs->vfs, fs->path);
	}
	if(fs->spaceUsed > fs->conf.spaceTotal) fs->spaceUsed = fs->conf.spaceTotal;
	return fs->spaceUsed;
}

// assumes locked
static size_t ncl_fsGetRealUsage(ncl_FSState *fs) {
	if(fs->realSpaceUsed == 0) {
		fs->realSpaceUsed = ncl_spaceUsedBy(fs->vfs, fs->path);
	}
	return fs->realSpaceUsed;
}

static nn_Exit ncl_fsHandler(nn_FSRequest *req) {
	ncl_FSState *state = req->state;
	nn_Context *ctx = req->ctx;
	nn_Computer *C = req->computer;
	const nn_Filesystem *fs = req->fs;

	if(req->action == NN_FS_DROP) {
		for(size_t i = 0; i < NN_MAX_OPENFILES; i++) {
			if(state->fds[i] != NULL) ncl_closefile(state->vfs, state->fds[i]);
			if(state->dirs[i] != NULL) ncl_closedir(state->vfs, state->dirs[i]);
		}
		nn_strfree(ctx, state->path);
		nn_free(ctx, state, sizeof(*state));
		return NN_OK;
	}
	if(req->action == NN_FS_SPACEUSED) {
		nn_lock(ctx, state->lock);
		req->spaceUsed = ncl_fsGetUsage(state);
		nn_unlock(ctx, state->lock);
		return NN_OK;
	}

	if(C) nn_setError(C, "not implemented yet");
	return NN_EBADCALL;
}

nn_Component *ncl_createFilesystem(nn_Universe *universe, const char *address, const char *path, const nn_Filesystem *fs, bool isReadonly);
nn_Component *ncl_createDrive(nn_Universe *universe, const char *address, const char *path, const nn_Drive *drive, bool isReadonly);
nn_Component *ncl_createEEPROM(nn_Universe *universe, const char *address, const char *codepath, const char *datapath, bool isReadonly);

static ncl_ScreenPixel ncl_getRealScreenPixel(const ncl_ScreenState *state, int x, int y) {
	if(x < 1 || y < 1 || x >= state->width || y >= state->height) {
		return (ncl_ScreenPixel) {
			.codepoint = ' ',
			.storedFg = 0xFFFFFF,
			.storedBg = 0x000000,
			.realFg = 0xFFFFFF,
			.realBg = 0x000000,
		};
	}

	// make it 0-indexed
	x--;
	y--;

	return state->pixels[x + y * state->conf.maxWidth];
}

static ncl_ScreenPixel *ncl_getRealScreenPixelPointer(const ncl_ScreenState *state, int x, int y) {
	if(x < 1 || y < 1 || x >= state->width || y >= state->height) {
		return NULL;
	}

	// make it 0-indexed
	x--;
	y--;

	return &state->pixels[x + y * state->conf.maxWidth];
}

static void ncl_setRealScreenPixel(ncl_ScreenState *state, int x, int y, ncl_ScreenPixel pixel) {
	if(x < 1 || y < 1 || x >= state->width || y >= state->height) return;
	x--;
	y--;

	state->pixels[x + y * state->conf.maxWidth] = pixel;
}

static void ncl_recomputeScreen(const ncl_ScreenState *state) {
	for(int y = 1; y <= state->height; y++) {
		for(int x = 1; x <= state->width; x++) {
			ncl_ScreenPixel *pixel = ncl_getRealScreenPixelPointer(state, x, y);
			if(pixel == NULL) continue;
			if(pixel->realFg >= 0) {
				pixel->realFg = nn_mapDepth(pixel->storedFg, state->depth);
			}
			if(pixel->realBg >= 0) {
				pixel->realBg = nn_mapDepth(pixel->storedBg, state->depth);
			}
		}
	}

	for(int i = 0; i < state->conf.paletteColors; i++) {
		state->resolvedPalette[i] = nn_mapDepth(state->palette[i], state->depth);
	}
}

nn_Component *ncl_createScreen(nn_Universe *universe, const char *address, const nn_ScreenConfig *config);
nn_Component *ncl_createGPU(nn_Universe *universe, const char *address, const nn_GPU *gpu);

void ncl_resetScreen(ncl_ScreenState *state) {
	state->width = state->conf.maxWidth;
	state->height = state->conf.maxHeight;
	state->viewportWidth = state->conf.maxWidth;
	state->viewportHeight = state->conf.maxHeight;

	for(int y = 1; y <= state->height; y++) {
		for(int x = 1; x <= state->width; x++) {
			ncl_setRealScreenPixel(state, x, y, (ncl_ScreenPixel) {
				.codepoint = ' ',
				.storedFg = 0xFFFFFF,
				.storedBg = 0x000000,
				.realFg = 0xFFFFFF,
				.realBg = 0x000000,
			});
		}
	}

	memcpy(state->palette, state->conf.defaultPalette, sizeof(int) * state->conf.paletteColors);
	ncl_recomputeScreen(state);
}

void ncl_getScreenResolution(const ncl_ScreenState *state, size_t *width, size_t *height) {
	*width = state->width;
	*height = state->height;
}

void ncl_getScreenViewport(const ncl_ScreenState *state, size_t *width, size_t *height) {
	*width = state->viewportWidth;
	*height = state->viewportHeight;
}

ncl_Pixel ncl_getScreenPixel(const ncl_ScreenState *state, int x, int y) {
	ncl_ScreenPixel p = ncl_getRealScreenPixel(state, x, y);
	return (ncl_Pixel) {
		.codepoint = p.codepoint,
		.fgColor = p.realFg < 0 ? state->resolvedPalette[p.storedFg] : p.realFg,
		.bgColor = p.realBg < 0 ? state->resolvedPalette[p.storedBg] : p.realBg,
	};
}

ncl_ScreenFlags ncl_getScreenFlags(const ncl_ScreenState *state) {
	return state->flags;
}

void ncl_setScreenFlags(ncl_ScreenState *state, ncl_ScreenFlags flags) {
	state->flags = flags;
}

// general stuff

void ncl_statComponent(nn_Component *component, ncl_ComponentStat *stat) {
	stat->labellen = 0;
	stat->isReadonly = false;
	const char *ty = nn_getComponentTypeID(component);
	void *state = nn_getComponentState(component);
	if(strcmp(ty, NCL_FS) == 0) {
		ncl_FSState *fs = state;
		nn_lock(fs->ctx, fs->lock);
		stat->isReadonly = fs->isReadonly;
		stat->usageCounter = fs->usage;
		stat->labellen = fs->labellen;
		memcpy(stat->label, fs->label, stat->labellen);
		stat->fs.spaceUsed = ncl_fsGetUsage(fs);
		stat->fs.realDiskUsage = ncl_fsGetRealUsage(fs);
		stat->fs.conf = &fs->conf;
		stat->fs.path = fs->path;
		stat->fs.filesOpen = 0;
		for(size_t i = 0; i < NN_MAX_OPENFILES; i++) {
			if(fs->fds[i] != NULL) stat->fs.filesOpen++;
		}
		nn_unlock(fs->ctx, fs->lock);
		return;
	}
	if(strcmp(ty, NCL_DRIVE) == 0) {
		ncl_DriveState *drv = state;
		nn_lock(drv->ctx, drv->lock);
		stat->isReadonly = drv->isReadonly;
		stat->usageCounter = drv->usage;
		stat->labellen = drv->labellen;
		memcpy(stat->label, drv->label, stat->labellen);
		stat->drive.path = drv->path;
		stat->drive.lastSector = drv->lastSector;
		stat->drive.conf = &drv->conf;
		nn_unlock(drv->ctx, drv->lock);
		return;
	}
	if(strcmp(ty, NCL_EEPROM) == 0) {
		ncl_EEState *ee = state;
		nn_lock(ee->ctx, ee->lock);
		stat->isReadonly = ee->isReadonly;
		stat->usageCounter = ee->usage;
		stat->labellen = ee->labellen;
		memcpy(stat->label, ee->label, stat->labellen);
		stat->eeprom.conf = &ee->conf;
		stat->eeprom.codepath = ee->codepath;
		stat->eeprom.datapath = ee->datapath;
		nn_unlock(ee->ctx, ee->lock);
		return;
	}
}

// For EEPROMs, filesystems, drives
// Returns whether it was successful or not.
bool ncl_makeReadonly(nn_Component *component) {
	const char *ty = nn_getComponentTypeID(component);
	void *state = nn_getComponentState(component);
	if(strcmp(ty, NCL_FS) == 0) {
		ncl_FSState *fs = state;
		fs->isReadonly = true;
		fs->usage++;
		return true;
	}
	if(strcmp(ty, NCL_DRIVE) == 0) {
		ncl_DriveState *drv = state;
		drv->isReadonly = true;
		drv->usage++;
		return true;
	}
	if(strcmp(ty, NCL_EEPROM) == 0) {
		ncl_EEState *ee = state;
		ee->isReadonly = true;
		ee->usage++;
		return true;
	}
	return false;
}

// all of these are encoding states

nn_Exit ncl_encodeComponentState(nn_Universe *universe, nn_Component *comp, ncl_EncodedState *state);
void ncl_freeEncodedState(nn_Universe *universe, ncl_EncodedState *state);
nn_Exit ncl_loadComponentState(nn_Component *comp, const ncl_EncodedState *state);

