#include "neonucleus.h"
#include "ncomplib.h"
#include <stdlib.h>
#include <string.h>

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

// Read me all my rights
#elif defined(NN_WINDOWS)

#include <windows.h>
#include <sys/stat.h>
#include <direct.h>

typedef struct ncl_WinDir {
	HANDLE handle;
	WIN32_FIND_DATAA findData;
	bool isFirst;
} ncl_WinDir;

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
		// fseek takes (file, offset, whence), not (file, whence, offset).
		// The original code had these two arguments swapped, which caused
		// every seek to go to the wrong position or fail entirely.
		/*
			* We want to: seek 100 bytes from the beginning (SEEK_SET = 0)
			* Call: fseek(f, 0, 100)
			* offset=0, whence=100
			* Result: whence=100 is invalid, fseek returns -1, the function returns false

			* We want to: seek 0 from the current position (SEEK_CUR = 1)
			* Call: fseek(f, 1, 0)
			* offset=1, whence=SEEK_SET(0)
			* Result: jumps to position 1 from the beginning of the file instead of staying at the same position

			* We want to: seek 0 from the beginning (SEEK_SET = 0)
			* Call: fseek(f, 0, 0)
			* offset=0, whence=SEEK_SET(0)
			* Result: works correctly by chance

			* We want to: seek 2 from the beginning (SEEK_SET = 0)
			* Call: fseek(f, 0, 2)
			* offset=0, whence=SEEK_END(2)
			* Result: Goes to the end of the file instead of position 2
		*/
		// Original fseek signature: int fseek(FILE *stream, long offset, int whence) remains,
		// yet the 				   : `if(fseek(f, whence, request->seek.off) < 0) return false;` 
		// ...variant was wrong
		if(fseek(f, request->seek.off, whence) < 0) return false;

		request->seek.off = ftell(f);
		return true;
	}
	if(request->action == NCL_VFS_REMOVE) {
		// On Windows, remove() cannot delete directories.
		// We need to check if the path is a directory and use _rmdir instead.
#ifdef NN_WINDOWS
		DWORD attrs = GetFileAttributesA(request->remove);
		if(attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
			return _rmdir(request->remove) == 0;
		}
#endif
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
		while(1) {
			DIR *d = request->readdir.dir;
			struct dirent *ent = readdir(d);
			if(ent == NULL) {
				request->readdir.name = NULL;
			} else if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
				continue;
			} else {
				strncpy(request->readdir.name, ent->d_name, NN_MAX_PATH-1);
			}
			return true;
		}
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
		stat->size = stat->isDirectory ? 0 : s.st_size;
		stat->lastModified = s.st_mtime;
		return true;
	}
	if(request->action == NCL_VFS_MKDIR) {
		return mkdir(request->mkdir, 0777) == 0;
	}
#elif defined(NN_WINDOWS)
	if(request->action == NCL_VFS_OPENDIR) {
		// FindFirstFileA needs a glob pattern, so we append "\\*" to the path.
		// We allocate the wrapper struct on the heap because FindFirstFileA
		// fills in the first result immediately and we need to remember that.
		ncl_WinDir *dir = malloc(sizeof(ncl_WinDir));
		if(dir == NULL) return false;
		char searchPath[NN_MAX_PATH + 4];
		snprintf(searchPath, sizeof(searchPath), "%s\\*", request->opendir.path);
		dir->handle = FindFirstFileA(searchPath, &dir->findData);
		if(dir->handle == INVALID_HANDLE_VALUE) {
			free(dir);
			return false;
		}
		dir->isFirst = true;
		request->opendir.dir = dir;
		return true;
	}
	if(request->action == NCL_VFS_CLOSEDIR) {
		ncl_WinDir *dir = request->closedir;
		FindClose(dir->handle);
		free(dir);
		return true;
	}
	if(request->action == NCL_VFS_READDIR) {
		// Mirrors the POSIX readdir loop: skip "." and "..",
		// copy name into the caller buffer, signal end with NULL.
		ncl_WinDir *dir = request->readdir.dir;
		while(1) {
			if(!dir->isFirst) {
				if(!FindNextFileA(dir->handle, &dir->findData)) {
					request->readdir.name = NULL;
					return true;
				}
			}
			dir->isFirst = false;
			if(strcmp(dir->findData.cFileName, ".") == 0 ||
			   strcmp(dir->findData.cFileName, "..") == 0) {
				continue;
			}
			strncpy(request->readdir.name, dir->findData.cFileName, NN_MAX_PATH - 1);
			request->readdir.name[NN_MAX_PATH - 1] = '\0';
			return true;
		}
	}
	if(request->action == NCL_VFS_STAT) {
		// Windows does not have st_blocks, so we approximate disk size
		// as the file size itself. This is less accurate than the POSIX
		// version but avoids needing the full Win32 file information API.
		struct _stat s;
		if(_stat(request->stat.path, &s) != 0) {
			request->stat.path = NULL;
			return false;
		}
		ncl_Stat *st = request->stat.stat;
		st->isDirectory = (s.st_mode & _S_IFDIR) != 0;
		st->diskSize = s.st_size;
		st->size = st->isDirectory ? 0 : s.st_size;
		st->lastModified = s.st_mtime;
		return true;
	}
	if(request->action == NCL_VFS_MKDIR) {
		// _mkdir on Windows does not take a permissions argument.
		return _mkdir(request->mkdir) == 0;
	}
#endif
	return false;
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
	.fileCost = NCL_FILECOST_DEFAULT,
};

ncl_VFS ncl_installerFS = (ncl_VFS) {
	.state = NULL,
	.handler = ncl_defaultHandler,
#ifdef NN_WINDOWS
	.pathsep = '\\',
#else
	.pathsep = '/',
#endif
	.fileCost = NCL_FILECOST_INSTALL,
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

bool ncl_readfile(ncl_VFS vfs, void *file, char *buf, size_t *len) {
	ncl_VFSRequest req;
	req.state = vfs.state;
	req.action = NCL_VFS_READ;
	req.read.file = file;
	req.read.buf = buf;
	req.read.len = *len;
	if(!vfs.handler(&req)) return false;
	if(req.read.buf == NULL) return false;
	*len = req.read.len;
	return true;
}

bool ncl_writefile(ncl_VFS vfs, void *file, const char *data, size_t len) {
	ncl_VFSRequest req;
	req.state = vfs.state;
	req.action = NCL_VFS_WRITE;
	req.write.file = file;
	req.write.buf = data;
	req.write.len = len;
	return vfs.handler(&req);
}

bool ncl_seekfile(ncl_VFS vfs, void *file, nn_FSWhence whence, int *off) {
	ncl_VFSRequest req;
	req.state = vfs.state;
	req.action = NCL_VFS_SEEK;
	req.seek.file = file;
	req.seek.whence = whence;
	req.seek.off = *off;
	if(!vfs.handler(&req)) return false;
	*off = req.seek.off;
	return true;
}

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
	size_t spaceUsed = vfs.fileCost + s.size;
	if(!s.isDirectory) return spaceUsed;
	void *dir = ncl_opendir(vfs, path);
	if(dir == NULL) return spaceUsed;
	char name[NN_MAX_PATH];
	while(ncl_readdir(vfs, dir, name)) {
		char subpath[NN_MAX_PATH];
		snprintf(subpath, sizeof(subpath), "%s%c%s", path, vfs.pathsep, name);
		spaceUsed += ncl_spaceUsedIn(vfs, subpath);
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
		char subpath[NN_MAX_PATH];
		snprintf(subpath, sizeof(subpath), "%s%c%s", path, vfs.pathsep, name);
		spaceUsed += ncl_spaceUsedBy(vfs, subpath);
	}
	ncl_closedir(vfs, dir);
	return spaceUsed;
}

bool ncl_exists(ncl_VFS vfs, const char *path) {
	ncl_Stat s;
	return ncl_stat(vfs, path, &s);
}

bool ncl_remove(ncl_VFS vfs, const char *path) {
	ncl_VFSRequest req;
	req.state = vfs.state;
	req.action = NCL_VFS_REMOVE;
	req.remove = path;
	return vfs.handler(&req);
}

bool ncl_removeRecursive(ncl_VFS vfs, const char *path) {
	ncl_Stat s;
	if(!ncl_stat(vfs, path, &s)) return false;
	if(s.isDirectory) {
		void *dir = ncl_opendir(vfs, path);
		char name[NN_MAX_PATH];
		while(ncl_readdir(vfs, dir, name)) {
			char subpath[NN_MAX_PATH];
			snprintf(subpath, sizeof(subpath), "%s%c%s", path, vfs.pathsep, name);
			ncl_removeRecursive(vfs, subpath);
		}
		ncl_closedir(vfs, dir);
	}
	return ncl_remove(vfs, path);
}

bool ncl_mkdir(ncl_VFS vfs, const char *path) {
	ncl_VFSRequest req;
	req.state = vfs.state;
	req.action = NCL_VFS_MKDIR;
	req.mkdir = path;
	return vfs.handler(&req);
}

bool ncl_mkdirRecursive(ncl_VFS vfs, const char *path) {
	ncl_Stat s;
	if(ncl_stat(vfs, path, &s)) {
		return s.isDirectory;
	}
	char buf[NN_MAX_PATH];
	// use snprintf instead of strncpy cuz NULL terminator
	snprintf(buf, NN_MAX_PATH, "%s", path);
	char *sep = strrchr(buf, vfs.pathsep);
	if(sep == NULL) {
		return ncl_mkdir(vfs, path);
	}
	*sep = '\0';
	if(!ncl_mkdirRecursive(vfs, buf)) return false;
	return ncl_mkdir(vfs, path);
}

static bool ncl_copydir(ncl_VFS vfs, const char *from, const char *to) {
	bool created = ncl_mkdir(vfs, to);
	if(!created) return false;
	void *dir = ncl_opendir(vfs, from);
	if(dir == NULL) goto fail;

	char name[NN_MAX_PATH];
	while(ncl_readdir(vfs, dir, name)) {
		char subpath[NN_MAX_PATH];
		snprintf(subpath, NN_MAX_PATH, "%s%c%s", from, vfs.pathsep, name);
		char subdest[NN_MAX_PATH];
		snprintf(subdest, NN_MAX_PATH, "%s%c%s", to, vfs.pathsep, name);
		printf("%s -> %s\n", subpath, subdest);
		if(!ncl_copyto(vfs, subpath, subdest)) goto fail;
	}

	ncl_closedir(vfs, dir);
	return true;
fail:
	if(dir != NULL) ncl_closedir(vfs, dir);
	// erase all evidence
	//ncl_removeRecursive(vfs, to);
	return false;
}

static bool ncl_copyfile(ncl_VFS vfs, const char *from, const char *to) {
	// copy some files!
	void *src = NULL;
	void *dest = NULL;
	src = ncl_openfile(vfs, from, "r");
	if(src == NULL) goto fail;
	dest = ncl_openfile(vfs, to, "w");
	if(dest == NULL) goto fail;

	char buf[NN_MAX_READ];
	size_t len = NN_MAX_READ;
	while(ncl_readfile(vfs, src, buf, &len)) {
		if(!ncl_writefile(vfs, dest, buf, len)) goto fail;
		len = NN_MAX_READ;
	}
	ncl_closefile(vfs, src);
	ncl_closefile(vfs, dest);

	return true;
fail:
	if(src != NULL) ncl_closefile(vfs, src);
	if(dest != NULL) ncl_closefile(vfs, dest);
	//ncl_remove(vfs, to);
	return false;
}

static bool ncl_isIllegalCopy(const char *from, const char *to) {
	// check if to starts with from, or from starts with to
	if(strncmp(from, to, strlen(from)) == 0) return true;
	if(strncmp(to, from, strlen(to)) == 0) return true;
	return false;
}

bool ncl_copyto(ncl_VFS vfs, const char *from, const char *to) {
	if(strcmp(from, to) == 0) return true;
	if(ncl_isIllegalCopy(from, to)) return false;
	// already exists
	if(ncl_exists(vfs, to)) return false;

	ncl_Stat s;
	// missing
	if(!ncl_stat(vfs, from, &s)) return false;

	if(s.isDirectory) return ncl_copydir(vfs, from, to);
	return ncl_copyfile(vfs, from, to);
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
	nn_Lock *lock;
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
	size_t keyboardCount;
	char *keyboards[NCL_MAX_KEYBOARD];
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

static ncl_ScreenPixel *ncl_vramPtr(ncl_VRAMBuf *buf, int x, int y) {
	x--;
	y--;
	if(x < 0 || y < 0 || x >= buf->width || y >= buf->height) return NULL;
	return &buf->pixels[x + y * buf->width];
}

static ncl_ScreenPixel ncl_vramGet(ncl_VRAMBuf *buf, int x, int y) {
	ncl_ScreenPixel *ptr = ncl_vramPtr(buf, x, y);
	if(ptr != NULL) return *ptr;
	return (ncl_ScreenPixel) {
		.codepoint = ' ',
		.storedFg = 0xFFFFFF,
		.storedBg = 0x000000,
		.realFg = 0xFFFFFF,
		.realBg = 0x000000,
	};
}

static void ncl_vramSet(ncl_VRAMBuf *buf, int x, int y, ncl_ScreenPixel pixel) {
	ncl_ScreenPixel *ptr = ncl_vramPtr(buf, x, y);
	if(ptr != NULL) *ptr = pixel;
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
	size_t usage;
	bool isReadonly;
	// all the arrays
	void *fds[NN_MAX_OPENFILES];
	void *dirs[NN_MAX_OPENFILES];
	char label[NN_MAX_LABEL];
	size_t labellen;
} ncl_FSState;

typedef struct ncl_DriveState {
	nn_Context *ctx;
	nn_Lock *lock;
	nn_Drive conf;
	ncl_VFS vfs;
	bool isReadonly;
	size_t usage;
	size_t lastSector;
	char *path;
	char label[NN_MAX_LABEL];
	size_t labellen;
} ncl_DriveState;

typedef struct ncl_EEState {
	nn_Context *ctx;
	nn_Lock *lock;
	nn_EEPROM conf;
	ncl_VFS vfs;
	bool isReadonly;
	size_t usage;
	char *codepath;
	// stored data buffer
	char *data;
	// the data length
	size_t datalen;
	char label[NN_MAX_LABEL];
	size_t labellen;
} ncl_EEState;

static void ncl_fixPath(ncl_FSState *fs, const char *path, char buf[NN_MAX_PATH]) {
	snprintf(buf, NN_MAX_PATH, "%s%c%s", fs->path, fs->vfs.pathsep, path);
	for(size_t i = 0; buf[i]; i++) {
		if(buf[i] == '/') buf[i] = fs->vfs.pathsep;
	}
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

// -1 on too many
static int ncl_findFileDesc(void *fds[NN_MAX_OPENFILES]) {
	for(int i = 0; i < NN_MAX_OPENFILES; i++) {
		if(fds[i] == NULL) return i;
	}
	return -1;
}

static void *ncl_getFile(void *fds[NN_MAX_OPENFILES], int fd) {
	if(fd < 0 || fd > NN_MAX_OPENFILES) return NULL;
	return fds[fd];
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
		nn_destroyLock(ctx, state->lock);
		nn_strfree(ctx, state->path);
		nn_free(ctx, state, sizeof(*state));
		return NN_OK;
	}
	if(req->action == NN_FS_SPACEUSED) {
		nn_lock(ctx, state->lock);
		state->usage++;
		req->spaceUsed = ncl_fsGetUsage(state);
		nn_unlock(ctx, state->lock);
		return NN_OK;
	}
	if(req->action == NN_FS_GETLABEL) {
		nn_lock(ctx, state->lock);
		state->usage++;
		size_t len = state->labellen;
		if(len > req->getlabel.len) len = req->getlabel.len;
		memcpy(req->getlabel.buf, state->label, len);
		req->getlabel.len = len;
		nn_unlock(ctx, state->lock);
		return NN_OK;
	}
	if(req->action == NN_FS_SETLABEL) {
		nn_lock(ctx, state->lock);
		if(state->isReadonly) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "is readonly");
			return NN_EBADCALL;
		}
		state->usage++;
		size_t len = req->setlabel.len;
		if(len > NN_MAX_LABEL) len = NN_MAX_LABEL;
		memcpy(state->label, req->setlabel.buf, len);
		state->labellen = len;
		nn_unlock(ctx, state->lock);
		return NN_OK;
	}
	if(req->action == NN_FS_ISRO) {
		req->isReadonly = state->isReadonly;
		return NN_OK;
	}
	if(req->action == NN_FS_OPEN) {
		nn_lock(ctx, state->lock);
		state->usage++;
		int fd = ncl_findFileDesc(state->fds);
		if(fd < 0) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "too many files");
			return NN_EBADCALL;
		}
		const char *mode = req->open.mode;
		if(mode[0] != 'r' && state->isReadonly) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "is readonly");
			return NN_EBADCALL;
		}
		char path[NN_MAX_PATH];
		ncl_fixPath(state, req->open.path, path);
		size_t spaceRemaining = state->conf.spaceTotal - ncl_fsGetUsage(state);
		if(mode[0] == 'w' && !ncl_exists(state->vfs, path) && spaceRemaining < state->vfs.fileCost) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "out of space");
			return NN_EBADCALL;
		}
		void *file = ncl_openfile(state->vfs, path, mode);
		if(file == NULL) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, req->open.path);
			return NN_EBADCALL;
		}
		state->fds[fd] = file;
		req->fd = fd;
		if(mode[0] == 'w') {
			// file cleared
			state->spaceUsed = 0;
			state->realSpaceUsed = 0;
		}
		nn_unlock(ctx, state->lock);
		return NN_OK;
	}
	if(req->action == NN_FS_CLOSE) {
		nn_lock(ctx, state->lock);
		int fd = req->fd;
		void *file = ncl_getFile(state->fds, fd);
		if(file == NULL) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		state->fds[fd] = NULL;
		state->spaceUsed = 0;
		state->realSpaceUsed = 0;
		volatile ncl_VFS vfs = state->vfs;
		nn_unlock(ctx, state->lock);
		// out of lock for the most minimal of performance
		ncl_closefile(vfs, file);
		return NN_OK;
	}
	if(req->action == NN_FS_READ) {
		nn_lock(ctx, state->lock);
		state->usage++;
		void *file = ncl_getFile(state->fds, req->fd);
		if(file == NULL) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		if(!ncl_readfile(state->vfs, file, req->read.buf, &req->read.len)) {
			req->read.buf = NULL;
		}
		nn_unlock(ctx, state->lock);
		return NN_OK;
	}
	if(req->action == NN_FS_WRITE) {
		nn_lock(ctx, state->lock);
		state->usage++;
		void *file = ncl_getFile(state->fds, req->fd);
		if(file == NULL) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		size_t spaceRemaining = state->conf.spaceTotal - ncl_fsGetUsage(state);
		// inaccurate...
		if(spaceRemaining < req->write.len) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "out of space");
			return NN_EBADCALL;
		}
		bool ok = ncl_writefile(state->vfs, file, req->write.buf, req->write.len);
		state->spaceUsed = 0;
		state->realSpaceUsed = 0;
		nn_unlock(ctx, state->lock);
		if(ok) return NN_OK;
		nn_setError(C, "write failed");
		return NN_EBADCALL;
	}
	if(req->action == NN_FS_SEEK) {
		nn_lock(ctx, state->lock);
		void *file = ncl_getFile(state->fds, req->fd);
		if(file == NULL) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		bool ok = ncl_seekfile(state->vfs, file, req->seek.whence, &req->seek.off);
		nn_unlock(ctx, state->lock);
		if(ok) return NN_OK;
		nn_setError(C, "seek failed");
		return NN_EBADCALL;
	}
	if(req->action == NN_FS_OPENDIR) {
		nn_lock(ctx, state->lock);
		state->usage++;
		int fd = ncl_findFileDesc(state->dirs);
		if(fd < 0) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "too many directories listed simultaneously");
			return NN_EBADCALL;
		}
		char path[NN_MAX_PATH];
		ncl_fixPath(state, req->open.path, path);
		void *dir = ncl_opendir(state->vfs, path);
		if(dir == NULL) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, req->opendir);
			return NN_EBADCALL;
		}
		state->dirs[fd] = dir;
		req->fd = fd;
		nn_unlock(ctx, state->lock);
		return NN_OK;
	}
	if(req->action == NN_FS_CLOSEDIR) {
		nn_lock(ctx, state->lock);
		int fd = req->fd;
		void *file = ncl_getFile(state->dirs, fd);
		if(file == NULL) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		state->dirs[fd] = NULL;
		volatile ncl_VFS vfs = state->vfs;
		nn_unlock(ctx, state->lock);
		// out of lock for the most minimal of performance
		ncl_closedir(vfs, file);
		return NN_OK;
	}
	if(req->action == NN_FS_READDIR) {
		nn_lock(ctx, state->lock);
		int fd = req->fd;
		void *dir = ncl_getFile(state->dirs, fd);
		if(dir == NULL) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "bad file descriptor");
			return NN_EBADCALL;
		}
		char name[NN_MAX_PATH];
		if(!ncl_readdir(state->vfs, dir, name)) {
			nn_unlock(ctx, state->lock);
			req->readdir.buf = NULL;
			return NN_OK;
		}
		char path[NN_MAX_PATH];
		snprintf(path, NN_MAX_PATH, "%s%c%s%c%s", state->path, state->vfs.pathsep, req->readdir.dirpath, state->vfs.pathsep, name);
		ncl_Stat s;
		if(!ncl_stat(state->vfs, path, &s)) s.isDirectory = false;
		if(s.isDirectory) snprintf(req->readdir.buf, req->readdir.len, "%s/", name);
		else snprintf(req->readdir.buf, req->readdir.len, "%s", name);
		req->readdir.len = strlen(req->readdir.buf);
		nn_unlock(ctx, state->lock);
		return NN_OK;
	}
	if(req->action == NN_FS_STAT) {
		nn_lock(ctx, state->lock);
		state->usage++;
		char path[NN_MAX_PATH];
		ncl_fixPath(state, req->stat.path, path);
		ncl_Stat s;
		if(!ncl_stat(state->vfs, path, &s)) {
			nn_unlock(ctx, state->lock);
			req->stat.path = NULL;
			return NN_OK;
		}
		req->stat.isDirectory = s.isDirectory;
		req->stat.size = s.size;
		req->stat.lastModified = s.lastModified;
		nn_unlock(ctx, state->lock);
		return NN_OK;
	}
	if(req->action == NN_FS_MKDIR) {
		nn_lock(ctx, state->lock);
		state->usage++;
		char path[NN_MAX_PATH];
		ncl_fixPath(state, req->mkdir, path);
		ncl_Stat s;
		if(ncl_stat(state->vfs, path, &s)) {
			nn_unlock(ctx, state->lock);
			if(s.isDirectory) {
				return NN_OK;
			}
			nn_setError(C, "not a directory");
			return NN_EBADCALL;
		}
		if(!ncl_mkdirRecursive(state->vfs, path)) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "operation failed");
			return NN_EBADCALL;
		}
		size_t newSpaceUsed = ncl_spaceUsedIn(state->vfs, state->path);
		if(newSpaceUsed > state->conf.spaceTotal) {
			ncl_removeRecursive(state->vfs, path);
			nn_unlock(ctx, state->lock);
			nn_setError(C, "out of space");
			return NN_EBADCALL;
		}
		state->spaceUsed = newSpaceUsed;
		state->realSpaceUsed = 0;
		nn_unlock(ctx, state->lock);
		return NN_OK;
	}
	if(req->action == NN_FS_RENAME) {
		if(req->rename.from[0] == '\0') {
			nn_setError(C, "root is forbidden");
			return NN_EBADCALL;
		}
		if(req->rename.to != NULL && req->rename.to[0] == '\0') {
			nn_setError(C, "root is forbidden");
			return NN_EBADCALL;
		}
		nn_lock(ctx, state->lock);
		state->usage++;
		char from[NN_MAX_PATH];
		ncl_fixPath(state, req->rename.from, from);
		if(req->rename.to == NULL) {
			bool ok = ncl_removeRecursive(state->vfs, from);
			nn_unlock(ctx, state->lock);
			if(!ok) {
				nn_setError(C, "operation failed");
				return NN_EBADCALL;
			}
			return NN_OK;
		}
		char to[NN_MAX_PATH];
		ncl_fixPath(state, req->rename.to, to);
		// copy a to a is illegal btw
		if(ncl_isIllegalCopy(from, to)) {
			nn_unlock(ctx, state->lock);
			nn_setError(C, "illegal copy operation");
			return NN_EBADCALL;
		}
		// matches tmpfs behavior
		if(ncl_exists(state->vfs, to)) {
			ncl_removeRecursive(state->vfs, to);
		}
		bool ok = ncl_copyto(state->vfs, from, to);
		if(ok) {
			ncl_removeRecursive(state->vfs, from);
		}
		state->spaceUsed = 0;
		state->realSpaceUsed = 0;
		nn_unlock(ctx, state->lock);
		if(!ok) {
			nn_setError(C, "operation failed");
			return NN_EBADCALL;
		}
		return NN_OK;
	}

	if(C) nn_setError(C, "not implemented yet");
	return NN_EBADCALL;
}

nn_Component *ncl_createFilesystem(nn_Universe *universe, const char *address, const char *path, const nn_Filesystem *fs, bool isReadonly) {
	nn_Context *ctx = nn_getUniverseContext(universe);

	ncl_FSState *state = nn_alloc(ctx, sizeof(*state));
	if(state == NULL) return NULL;
	state->ctx = ctx;
	state->lock = nn_createLock(ctx);
	if(state->lock == NULL) {
		nn_free(ctx, state, sizeof(*state));
		return NULL;
	}
	state->path = nn_strdup(ctx, path);
	if(state->path == NULL) {
		nn_destroyLock(ctx, state->lock);
		nn_free(ctx, state, sizeof(*state));
		return NULL;
	}
	state->vfs = ncl_defaultFS;
	state->usage = 0;
	state->isReadonly = isReadonly;
	state->conf = *fs;
	state->labellen = 0;
	state->realSpaceUsed = 0;
	state->spaceUsed = 0;
	for(size_t i = 0; i < NN_MAX_OPENFILES; i++) {
		state->fds[i] = NULL;
		state->dirs[i] = NULL;
	}
	nn_Component *c = nn_createFilesystem(universe, address, fs, state, ncl_fsHandler);
	if(c == NULL) {
		nn_strfree(ctx, state->path);
		nn_destroyLock(ctx, state->lock);
		nn_free(ctx, state, sizeof(*state));
		return NULL;
	}
	// TODO: handle OOM case
	nn_setComponentTypeID(c, NCL_FS);
	return c;
}

nn_Component *ncl_createTmpFS(nn_Universe *universe, const nn_Filesystem *fs);

nn_Component *ncl_createDrive(nn_Universe *universe, const char *address, const char *path, const nn_Drive *drive, bool isReadonly);
nn_Component *ncl_createTmpDrive(nn_Universe *universe, const nn_EEPROM *eeprom, const char *data, size_t datalen);
nn_Component *ncl_createEEPROM(nn_Universe *universe, const char *address, const char *path, bool isReadonly);
nn_Component *ncl_createTmpEEPROM(nn_Universe *universe, const nn_EEPROM *eeprom, const char *code, size_t codelen);

size_t ncl_getEEPROMData(nn_Component *component, char *buf);
void ncl_setEEPROMData(nn_Component *component, const char *data, size_t len);

ncl_VFS ncl_getVFS(nn_Component *component);

ncl_VFS ncl_setVFS(nn_Component *component, ncl_VFS vfs);

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

static nn_Exit ncl_screenHandler(nn_ScreenRequest *req) {
    nn_Context *ctx = req->ctx;
    nn_Computer *C = req->computer;
    ncl_ScreenState *st = req->state;

    if(req->action == NN_SCREEN_DROP) {
        for(size_t i = 0; i < st->keyboardCount; i++)
            nn_strfree(ctx, st->keyboards[i]);
        nn_destroyLock(ctx, st->lock);
        nn_free(ctx, st->pixels,
            sizeof(ncl_ScreenPixel)
            * st->conf.maxWidth * st->conf.maxHeight);
        nn_free(ctx, st->palette,
            sizeof(int) * st->conf.paletteColors);
        nn_free(ctx, st->resolvedPalette,
            sizeof(int) * st->conf.paletteColors);
        nn_free(ctx, st, sizeof(*st));
        return NN_OK;
    }
    if(req->action == NN_SCREEN_ISON) {
        nn_lock(ctx, st->lock);
        req->power.isOn =
            (st->flags & NCL_SCREEN_ON) != 0;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    if(req->action == NN_SCREEN_TURNON) {
        nn_lock(ctx, st->lock);
        bool was = (st->flags & NCL_SCREEN_ON) != 0;
        st->flags |= NCL_SCREEN_ON;
        req->power.wasOn = !was;
        req->power.isOn = true;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    if(req->action == NN_SCREEN_TURNOFF) {
        nn_lock(ctx, st->lock);
        bool was = (st->flags & NCL_SCREEN_ON) != 0;
        st->flags &= ~NCL_SCREEN_ON;
        req->power.wasOn = was;
        req->power.isOn = false;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    if(req->action == NN_SCREEN_GETASPECTRATIO) {
        // single-block screen
        req->aspect.w = 1;
        req->aspect.h = 1;
        return NN_OK;
    }
    if(req->action == NN_SCREEN_GETKEYBOARDS) {
        nn_lock(ctx, st->lock);
        for(size_t i = 0; i < st->keyboardCount; i++)
            nn_pushstring(C, st->keyboards[i]);
        req->kbCount = st->keyboardCount;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    if(req->action == NN_SCREEN_SETPRECISE) {
        nn_lock(ctx, st->lock);
        if(req->flag)
            st->flags |= NCL_SCREEN_PRECISE;
        else
            st->flags &= ~NCL_SCREEN_PRECISE;
        req->flag =
            (st->flags & NCL_SCREEN_PRECISE) != 0;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    if(req->action == NN_SCREEN_ISPRECISE) {
        nn_lock(ctx, st->lock);
        req->flag =
            (st->flags & NCL_SCREEN_PRECISE) != 0;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    if(req->action == NN_SCREEN_SETTOUCHINVERTED) {
        nn_lock(ctx, st->lock);
        if(req->flag)
            st->flags |= NCL_SCREEN_TOUCHINVERTED;
        else
            st->flags &= ~NCL_SCREEN_TOUCHINVERTED;
        req->flag =
            (st->flags & NCL_SCREEN_TOUCHINVERTED) != 0;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    if(req->action == NN_SCREEN_ISTOUCHINVERTED) {
        nn_lock(ctx, st->lock);
        req->flag =
            (st->flags & NCL_SCREEN_TOUCHINVERTED) != 0;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }

    if(C) nn_setError(C, "ncl-screen: bad action");
    return NN_EBADCALL;
}

nn_Component *ncl_createScreen(nn_Universe *universe, const char *address, const nn_ScreenConfig *config) {
	nn_Context *ctx = nn_getUniverseContext(universe);
	ncl_ScreenState *screen = NULL;
	ncl_ScreenPixel *pixels = NULL;
	int *palette = NULL;
	int *resolvedPalette = NULL;
	nn_Component *c = NULL;
	nn_Lock *lock = NULL;

	screen = nn_alloc(ctx, sizeof(ncl_ScreenState));
	if(screen == NULL) goto fail;

	lock = nn_createLock(ctx);
	if(lock == NULL) goto fail;

	pixels = nn_alloc(ctx, sizeof(ncl_ScreenPixel) * config->maxWidth * config->maxHeight);
	if(pixels == NULL) goto fail;

	palette = nn_alloc(ctx, sizeof(int) * config->paletteColors);
	if(palette == NULL) goto fail;
	memcpy(palette, config->defaultPalette, sizeof(int) * config->paletteColors);
	
	resolvedPalette = nn_alloc(ctx, sizeof(int) * config->paletteColors);
	if(resolvedPalette == NULL) goto fail;

	screen->conf = *config;
	screen->ctx = ctx;
	screen->lock = lock;
	screen->width = config->maxWidth;
	screen->height = config->maxHeight;
	screen->palette = palette;
	screen->resolvedPalette = resolvedPalette;
	screen->pixels = pixels;
	screen->flags = 0;
	screen->depth = config->maxDepth;
	screen->viewportWidth = screen->width;
	screen->viewportHeight = screen->height;
	screen->keyboardCount = 0;

	ncl_resetScreen(screen);

	c = nn_createScreen(universe, address, config, screen, ncl_screenHandler);
	if(c == NULL) goto fail;

	if(nn_setComponentTypeID(c, NCL_SCREEN)) goto fail;

	return c;

fail:;
	if(c != NULL) {
		nn_dropComponent(c);
		return NULL;
	}
	if(lock != NULL) nn_destroyLock(ctx, lock);
	nn_free(ctx, screen, sizeof(*screen));
	nn_free(ctx, palette, sizeof(int) * config->paletteColors);
	nn_free(ctx, resolvedPalette, sizeof(int) * config->paletteColors);
	nn_free(ctx, pixels, sizeof(ncl_ScreenPixel) * config->maxWidth * config->maxHeight);
	return NULL;
}

static ncl_ScreenState *ncl_getBoundScreen(ncl_GPUState *gpu, nn_Computer *C) {
	if(gpu->screenAddress == NULL) return NULL;
	nn_Component *c = nn_getComponent(C, gpu->screenAddress);
	if(c == NULL) return NULL;
	return nn_getComponentState(c);
}

/*
static void ncl_getGPULimits(ncl_GPUState *gpu, nn_Computer *C, int *maxWidth, int *maxHeight, char *maxDepth) {
	int w = gpu->conf.maxWidth, h = gpu->conf.maxHeight;
	char d = gpu->conf.maxDepth;

	ncl_ScreenState *screen = ncl_getBoundScreen(gpu, C);

	if(screen != NULL) {
		if(w > screen->conf.maxWidth) w = screen->conf.maxWidth;
		if(h > screen->conf.maxHeight) h = screen->conf.maxHeight;
		if(d > screen->conf.maxDepth) d = screen->conf.maxDepth;
	}

	*maxWidth = w;
	*maxHeight = h;
	*maxDepth = d;
}
*/

static void ncl_getGPULimitsWithScreen(
    ncl_GPUState *gpu, ncl_ScreenState *screen,
    int *maxWidth, int *maxHeight, char *maxDepth)
{
    int w = gpu->conf.maxWidth;
    int h = gpu->conf.maxHeight;
    char d = gpu->conf.maxDepth;
    if(screen != NULL) {
        if(w > screen->conf.maxWidth)
            w = screen->conf.maxWidth;
        if(h > screen->conf.maxHeight)
            h = screen->conf.maxHeight;
        if(d > screen->conf.maxDepth)
            d = screen->conf.maxDepth;
    }
    *maxWidth = w;
    *maxHeight = h;
    *maxDepth = d;
}

// helper: get the target buffer for the active index.
// Returns NULL + sets error on failure.
// If index==0 and screen is non-NULL, caller must use
// the screen pixel functions instead.
static ncl_VRAMBuf *ncl_getVRAMBuf(
    ncl_GPUState *st, int idx, nn_Computer *C)
{
    if(idx <= 0 || idx >= NCL_MAX_VRAMBUF) {
        if(C) nn_setError(C, "invalid buffer index");
        return NULL;
    }
    ncl_VRAMBuf *b = st->vram[idx];
    if(b == NULL && C)
        nn_setError(C, "no such buffer");
    return b;
}

static nn_Exit ncl_gpuHandler(nn_GPURequest *req) {
    nn_Context *ctx = req->ctx;
    nn_Computer *C = req->computer;
    ncl_GPUState *st = req->state;

    if(req->action == NN_GPU_DROP) {
        for(size_t i = 0; i < NCL_MAX_VRAMBUF; i++) {
            if(st->vram[i] != NULL)
                ncl_freeVRAM(ctx, st->vram[i]);
        }
        if(st->screenAddress != NULL)
            nn_strfree(ctx, st->screenAddress);
        nn_destroyLock(ctx, st->lock);
        nn_free(ctx, st, sizeof(*st));
        return NN_OK;
    }

    //  bind 
    if(req->action == NN_GPU_BIND) {
        nn_Component *sc =
            nn_getComponent(C, req->bind.address);
        if(sc == NULL) {
            nn_setError(C, "no such component");
            return NN_EBADCALL;
        }
        const char *tid = nn_getComponentTypeID(sc);
       	if(strcmp(tid, NCL_SCREEN) != 0) {
            nn_setError(C, "not a screen");
            return NN_EBADCALL;
        }
        nn_lock(ctx, st->lock);
        if(st->screenAddress != NULL)
            nn_strfree(ctx, st->screenAddress);
        st->screenAddress =
            nn_strdup(ctx, req->bind.address);
        nn_unlock(ctx, st->lock);

        if(req->bind.reset) {
            ncl_ScreenState *scr =
                nn_getComponentState(sc);
            ncl_lockScreen(scr);
            ncl_resetScreen(scr);
            ncl_unlockScreen(scr);
        }
        return NN_OK;
    }
    //  getScreen 
    if(req->action == NN_GPU_GETSCREEN) {
        nn_lock(ctx, st->lock);
        if(st->screenAddress != NULL) {
            size_t len = strlen(st->screenAddress);
            if(len >= NN_MAX_ADDRESS)
                len = NN_MAX_ADDRESS - 1;
		memcpy(req->screenAddr,
			st->screenAddress, len);
            req->screenAddr[len] = '\0';
        }
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    //  getBackground 
    if(req->action == NN_GPU_GETBG) {
        nn_lock(ctx, st->lock);
        req->color.color = st->currentBg;
        req->color.isPalette = st->isBgPalette;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    //  setBackground 
    if(req->action == NN_GPU_SETBG) {
        nn_lock(ctx, st->lock);
        req->color.oldColor = st->currentBg;
        req->color.wasPalette = st->isBgPalette;
        req->color.oldPaletteIdx =
            st->isBgPalette ? st->currentBg : -1;
        st->currentBg = req->color.color;
        st->isBgPalette = req->color.isPalette;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    //  getForeground 
    if(req->action == NN_GPU_GETFG) {
        nn_lock(ctx, st->lock);
        req->color.color = st->currentFg;
        req->color.isPalette = st->isFgPalette;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    //  setForeground 
    if(req->action == NN_GPU_SETFG) {
        nn_lock(ctx, st->lock);
        req->color.oldColor = st->currentFg;
        req->color.wasPalette = st->isFgPalette;
        req->color.oldPaletteIdx =
            st->isFgPalette ? st->currentFg : -1;
        st->currentFg = req->color.color;
        st->isFgPalette = req->color.isPalette;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    //  getPaletteColor 
    if(req->action == NN_GPU_GETPALETTE) {
        nn_lock(ctx, st->lock);
        ncl_ScreenState *scr =
            ncl_getBoundScreen(st, C);
        nn_unlock(ctx, st->lock);
        if(scr == NULL) {
            nn_setError(C, "no screen");
            return NN_EBADCALL;
        }
        ncl_lockScreen(scr);
        int idx = req->palette.index;
        if(idx < 0 || idx >= scr->conf.paletteColors) {
            ncl_unlockScreen(scr);
            nn_setError(C, "invalid palette index");
            return NN_EBADCALL;
        }
        req->palette.color = scr->palette[idx];
        ncl_unlockScreen(scr);
        return NN_OK;
    }
    //  setPaletteColor 
    if(req->action == NN_GPU_SETPALETTE) {
        nn_lock(ctx, st->lock);
        ncl_ScreenState *scr =
            ncl_getBoundScreen(st, C);
        nn_unlock(ctx, st->lock);
        if(scr == NULL) {
            nn_setError(C, "no screen");
            return NN_EBADCALL;
        }
        ncl_lockScreen(scr);
        int idx = req->palette.index;
        if(idx < 0
           || idx >= scr->conf.editableColors) {
            ncl_unlockScreen(scr);
            nn_setError(C, "invalid palette index");
            return NN_EBADCALL;
        }
        req->palette.oldColor = scr->palette[idx];
        scr->palette[idx] = req->palette.color;
        scr->resolvedPalette[idx] =
            nn_mapDepth(req->palette.color, scr->depth);
        ncl_unlockScreen(scr);
        return NN_OK;
    }
    //  maxDepth 
    if(req->action == NN_GPU_MAXDEPTH) {
        nn_lock(ctx, st->lock);
        char d = st->conf.maxDepth;
        ncl_ScreenState *scr =
            ncl_getBoundScreen(st, C);
        nn_unlock(ctx, st->lock);
        if(scr != NULL) {
            ncl_lockScreen(scr);
            if(scr->conf.maxDepth < d)
                d = scr->conf.maxDepth;
            ncl_unlockScreen(scr);
        }
        req->depth.depth = d;
        return NN_OK;
    }
    //  getDepth 
    if(req->action == NN_GPU_GETDEPTH) {
        nn_lock(ctx, st->lock);
        ncl_ScreenState *scr =
            ncl_getBoundScreen(st, C);
        nn_unlock(ctx, st->lock);
        if(scr == NULL) {
            req->depth.depth = st->conf.maxDepth;
            return NN_OK;
        }
        ncl_lockScreen(scr);
        req->depth.depth = scr->depth;
        ncl_unlockScreen(scr);
        return NN_OK;
    }
    //  setDepth 
    if(req->action == NN_GPU_SETDEPTH) {
        char want = req->depth.depth;
        if(nn_depthName(want) == NULL) {
            nn_setError(C, "unsupported depth");
            return NN_EBADCALL;
        }
        nn_lock(ctx, st->lock);
        int maxD = st->conf.maxDepth;
        ncl_ScreenState *scr =
            ncl_getBoundScreen(st, C);
        nn_unlock(ctx, st->lock);
        if(scr == NULL) {
            nn_setError(C, "no screen");
            return NN_EBADCALL;
        }
        ncl_lockScreen(scr);
        if(scr->conf.maxDepth < maxD)
            maxD = scr->conf.maxDepth;
        if(want > maxD) {
            ncl_unlockScreen(scr);
            nn_setError(C, "unsupported depth");
            return NN_EBADCALL;
        }
        req->depth.oldDepth = scr->depth;
        scr->depth = want;
        ncl_recomputeScreen(scr);
        ncl_unlockScreen(scr);
        return NN_OK;
    }
    //  maxResolution 
	if(req->action == NN_GPU_MAXRES) {
        nn_lock(ctx, st->lock);
        ncl_ScreenState *scr =
            ncl_getBoundScreen(st, C);
        nn_unlock(ctx, st->lock);
        int w, h; char d;
        if(scr != NULL) ncl_lockScreen(scr);
        ncl_getGPULimitsWithScreen(
            st, scr, &w, &h, &d);
        if(scr != NULL) ncl_unlockScreen(scr);
        req->resolution.width = w;
        req->resolution.height = h;
        return NN_OK;
    }
    //  getResolution 
    if(req->action == NN_GPU_GETRES) {
        nn_lock(ctx, st->lock);
        ncl_ScreenState *scr =
            ncl_getBoundScreen(st, C);
        nn_unlock(ctx, st->lock);
        if(scr == NULL) {
            nn_setError(C, "no screen");
            return NN_EBADCALL;
        }
        ncl_lockScreen(scr);
        req->resolution.width = scr->width;
        req->resolution.height = scr->height;
        ncl_unlockScreen(scr);
        return NN_OK;
    }
    //  setResolution 
	if(req->action == NN_GPU_SETRES) {
        int w = req->resolution.width;
        int h = req->resolution.height;
        nn_lock(ctx, st->lock);
        ncl_ScreenState *scr =
        ncl_getBoundScreen(st, C);
        nn_unlock(ctx, st->lock);
        int maxW, maxH; char maxD;
        if(scr != NULL) ncl_lockScreen(scr);
        ncl_getGPULimitsWithScreen(
            st, scr, &maxW, &maxH, &maxD);
        if(w < 1 || h < 1
           || w > maxW || h > maxH) {
            if(scr != NULL) ncl_unlockScreen(scr);
            nn_setError(C, "unsupported resolution");
            return NN_EBADCALL;
        }
        if(scr == NULL) {
            nn_setError(C, "no screen");
            return NN_EBADCALL;
        }
        scr->width = w;
        scr->height = h;
        if(scr->viewportWidth > w)
            scr->viewportWidth = w;
        if(scr->viewportHeight > h)
            scr->viewportHeight = h;
        ncl_unlockScreen(scr);
        return NN_OK;
    }
    //  getViewport 
    if(req->action == NN_GPU_GETVIEWPORT) {
        nn_lock(ctx, st->lock);
        ncl_ScreenState *scr =
            ncl_getBoundScreen(st, C);
        nn_unlock(ctx, st->lock);
        if(scr == NULL) {
            nn_setError(C, "no screen");
            return NN_EBADCALL;
        }
        ncl_lockScreen(scr);
        req->resolution.width = scr->viewportWidth;
        req->resolution.height = scr->viewportHeight;
        ncl_unlockScreen(scr);
        return NN_OK;
    }
    //  setViewport 
    if(req->action == NN_GPU_SETVIEWPORT) {
        int w = req->resolution.width;
        int h = req->resolution.height;
        nn_lock(ctx, st->lock);
        ncl_ScreenState *scr =
            ncl_getBoundScreen(st, C);
        nn_unlock(ctx, st->lock);
        if(scr == NULL) {
            nn_setError(C, "no screen");
            return NN_EBADCALL;
        }
        ncl_lockScreen(scr);
        if(w < 1 || h < 1
           || w > scr->width || h > scr->height) {
            ncl_unlockScreen(scr);
            nn_setError(C,
                "viewport exceeds resolution");
            return NN_EBADCALL;
        }
        scr->viewportWidth = w;
        scr->viewportHeight = h;
        ncl_unlockScreen(scr);
        return NN_OK;
    }
    //  get 
    if(req->action == NN_GPU_GET) {
        nn_lock(ctx, st->lock);
        int active = st->activeBuffer;
        ncl_ScreenState *scr =
            (active == 0) ?
            ncl_getBoundScreen(st, C) : NULL;
        nn_unlock(ctx, st->lock);

        ncl_ScreenPixel px;
        if(active == 0) {
            if(scr == NULL) {
                nn_setError(C, "no screen");
                return NN_EBADCALL;
            }
            ncl_lockScreen(scr);
            px = ncl_getRealScreenPixel(
                scr, req->get.x, req->get.y);
            ncl_unlockScreen(scr);
        } else {
            nn_lock(ctx, st->lock);
            ncl_VRAMBuf *b =
                ncl_getVRAMBuf(st, active, C);
            if(b == NULL) {
                nn_unlock(ctx, st->lock);
                return NN_EBADCALL;
            }
            px = ncl_vramGet(
                b, req->get.x, req->get.y);
            nn_unlock(ctx, st->lock);
        }
        req->get.codepoint = px.codepoint;
        req->get.fg = px.storedFg;
        req->get.bg = px.storedBg;
        req->get.fgIdx = (px.realFg < 0)
            ? px.storedFg : -1;
        req->get.bgIdx = (px.realBg < 0)
            ? px.storedBg : -1;
        return NN_OK;
    }
    //  set 
    if(req->action == NN_GPU_SET) {
        nn_lock(ctx, st->lock);
        int fg = st->currentFg;
        int bg = st->currentBg;
        bool fgP = st->isFgPalette;
        bool bgP = st->isBgPalette;
        int active = st->activeBuffer;
        ncl_ScreenState *scr =
            (active == 0) ?
            ncl_getBoundScreen(st, C) : NULL;
        nn_unlock(ctx, st->lock);

        int x = req->set.x, y = req->set.y;
        const char *s = req->set.value;
        size_t len = req->set.len;
        bool vert = req->set.vertical;

        ncl_ScreenPixel px;
        px.storedFg = fg;
        px.storedBg = bg;
        px.realFg = fgP ? -1 : fg;
        px.realBg = bgP ? -1 : bg;

        if(active == 0) {
            if(scr == NULL) {
                nn_setError(C, "no screen");
                return NN_EBADCALL;
            }
            ncl_lockScreen(scr);
            // depth-map direct colors
            if(!fgP) px.realFg =
                nn_mapDepth(fg, scr->depth);
            if(!bgP) px.realBg =
                nn_mapDepth(bg, scr->depth);
            size_t i = 0;
            while(i < len) {
                size_t cw =
                    nn_unicode_validateFirstChar(
                        s + i, len - i);
                if(cw == 0) { cw = 1; px.codepoint =
                    (unsigned char)s[i]; }
                else px.codepoint =
                    nn_unicode_firstCodepoint(s + i);
                ncl_setRealScreenPixel(
                    scr, x, y, px);
                i += cw;
                if(vert) y++; else x++;
            }
            ncl_unlockScreen(scr);
        } else {
            nn_lock(ctx, st->lock);
            ncl_VRAMBuf *b =
                ncl_getVRAMBuf(st, active, C);
            if(b == NULL) {
                nn_unlock(ctx, st->lock);
                return NN_EBADCALL;
            }
            size_t i = 0;
            while(i < len) {
                size_t cw =
                    nn_unicode_validateFirstChar(
                        s + i, len - i);
                if(cw == 0) { cw = 1; px.codepoint =
                    (unsigned char)s[i]; }
                else px.codepoint =
                    nn_unicode_firstCodepoint(s + i);
                ncl_vramSet(b, x, y, px);
                i += cw;
                if(vert) y++; else x++;
            }
            nn_unlock(ctx, st->lock);
        }
        return NN_OK;
    }
    //  copy 
    if(req->action == NN_GPU_COPY) {
        int sx = req->copy.x, sy = req->copy.y;
        int w = req->copy.w, h = req->copy.h;
        int tx = req->copy.tx, ty = req->copy.ty;

        nn_lock(ctx, st->lock);
        int active = st->activeBuffer;
        ncl_ScreenState *scr =
            (active == 0) ?
            ncl_getBoundScreen(st, C) : NULL;
        nn_unlock(ctx, st->lock);

        // overlap-safe iteration order
        int y0 = (ty > 0) ? sy+h-1 : sy;
        int y1 = (ty > 0) ? sy-1 : sy+h;
        int dy = (ty > 0) ? -1 : 1;
        int x0 = (tx > 0) ? sx+w-1 : sx;
        int x1 = (tx > 0) ? sx-1 : sx+w;
        int dx = (tx > 0) ? -1 : 1;

        if(active == 0) {
            if(scr == NULL) {
                nn_setError(C, "no screen");
                return NN_EBADCALL;
            }
            ncl_lockScreen(scr);
            for(int y = y0; y != y1; y += dy)
            for(int x = x0; x != x1; x += dx) {
                ncl_ScreenPixel p =
                    ncl_getRealScreenPixel(
                        scr, x, y);
                ncl_setRealScreenPixel(
                    scr, x+tx, y+ty, p);
            }
            ncl_unlockScreen(scr);
        } else {
            nn_lock(ctx, st->lock);
            ncl_VRAMBuf *b =
                ncl_getVRAMBuf(st, active, C);
            if(b == NULL) {
                nn_unlock(ctx, st->lock);
                return NN_EBADCALL;
            }
            for(int y = y0; y != y1; y += dy)
            for(int x = x0; x != x1; x += dx) {
                ncl_ScreenPixel p =
                    ncl_vramGet(b, x, y);
                ncl_vramSet(b, x+tx, y+ty, p);
            }
            nn_unlock(ctx, st->lock);
        }
        return NN_OK;
    }
    //  fill 
    if(req->action == NN_GPU_FILL) {
        nn_lock(ctx, st->lock);
        int fg = st->currentFg;
        int bg = st->currentBg;
        bool fgP = st->isFgPalette;
        bool bgP = st->isBgPalette;
        int active = st->activeBuffer;
        ncl_ScreenState *scr =
            (active == 0) ?
            ncl_getBoundScreen(st, C) : NULL;
        nn_unlock(ctx, st->lock);

        ncl_ScreenPixel px;
        px.codepoint = req->fill.codepoint;
        px.storedFg = fg;
        px.storedBg = bg;
        px.realFg = fgP ? -1 : fg;
        px.realBg = bgP ? -1 : bg;

        int x0 = req->fill.x, y0 = req->fill.y;
        int w = req->fill.w, h = req->fill.h;

        if(active == 0) {
            if(scr == NULL) {
                nn_setError(C, "no screen");
                return NN_EBADCALL;
            }
            ncl_lockScreen(scr);
            if(!fgP) px.realFg =
                nn_mapDepth(fg, scr->depth);
            if(!bgP) px.realBg =
                nn_mapDepth(bg, scr->depth);
            for(int y = y0; y < y0+h; y++)
            for(int x = x0; x < x0+w; x++)
                ncl_setRealScreenPixel(
                    scr, x, y, px);
            ncl_unlockScreen(scr);
        } else {
            nn_lock(ctx, st->lock);
            ncl_VRAMBuf *b =
                ncl_getVRAMBuf(st, active, C);
            if(b == NULL) {
                nn_unlock(ctx, st->lock);
                return NN_EBADCALL;
            }
            for(int y = y0; y < y0+h; y++)
            for(int x = x0; x < x0+w; x++)
                ncl_vramSet(b, x, y, px);
            nn_unlock(ctx, st->lock);
        }
        return NN_OK;
    }
    //  VRAM: getActiveBuffer 
    if(req->action == NN_GPU_GETACTIVEBUF) {
        nn_lock(ctx, st->lock);
        req->buffer.index = st->activeBuffer;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    //  VRAM: setActiveBuffer 
    if(req->action == NN_GPU_SETACTIVEBUF) {
        int idx = req->buffer.index;
        nn_lock(ctx, st->lock);
        if(idx != 0 && (idx < 0
           || idx >= NCL_MAX_VRAMBUF
           || st->vram[idx] == NULL)) {
            nn_unlock(ctx, st->lock);
            nn_setError(C, "invalid buffer index");
            return NN_EBADCALL;
        }
        st->activeBuffer = idx;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    //  VRAM: buffers 
    if(req->action == NN_GPU_BUFFERS) {
        nn_lock(ctx, st->lock);
        size_t count = 0;
        for(int i = 1; i < NCL_MAX_VRAMBUF; i++) {
            if(st->vram[i] != NULL) {
                nn_pushinteger(C, i);
                count++;
            }
        }
        nn_unlock(ctx, st->lock);
        req->bufCount = count;
        return NN_OK;
    }
    //  VRAM: allocateBuffer 
    if(req->action == NN_GPU_ALLOCBUF) {
        int w = req->allocBuf.w;
        int h = req->allocBuf.h;
        // default to current GPU max if 0
        if(w <= 0) w = st->conf.maxWidth;
        if(h <= 0) h = st->conf.maxHeight;
        size_t cost = (size_t)w * h;
        nn_lock(ctx, st->lock);
        if(cost > st->vramFree) {
            nn_unlock(ctx, st->lock);
            nn_setError(C, "not enough video memory");
            return NN_EBADCALL;
        }
        int slot = -1;
        for(int i = 1; i < NCL_MAX_VRAMBUF; i++) {
            if(st->vram[i] == NULL) {
                slot = i;
                break;
            }
        }
        if(slot < 0) {
            nn_unlock(ctx, st->lock);
            nn_setError(C, "too many buffers");
            return NN_EBADCALL;
        }
        ncl_VRAMBuf *b = ncl_allocVRAM(ctx, w, h);
        if(b == NULL) {
            nn_unlock(ctx, st->lock);
            nn_setError(C, "allocation failed");
            return NN_EBADCALL;
        }
        st->vram[slot] = b;
        st->vramFree -= cost;
        nn_unlock(ctx, st->lock);
        req->allocBuf.index = slot;
        return NN_OK;
    }
    //  VRAM: freeBuffer 
    if(req->action == NN_GPU_FREEBUF) {
        int idx = req->buffer.index;
        if(idx <= 0 || idx >= NCL_MAX_VRAMBUF) {
            nn_setError(C, "invalid buffer index");
            return NN_EBADCALL;
        }
        nn_lock(ctx, st->lock);
        ncl_VRAMBuf *b = st->vram[idx];
        if(b == NULL) {
            nn_unlock(ctx, st->lock);
            nn_setError(C, "no such buffer");
            return NN_EBADCALL;
        }
        st->vramFree += (size_t)b->width * b->height;
        st->vram[idx] = NULL;
        if(st->activeBuffer == idx)
            st->activeBuffer = 0;
        nn_unlock(ctx, st->lock);
        ncl_freeVRAM(ctx, b);
        return NN_OK;
    }
    //  VRAM: freeAllBuffers 
    if(req->action == NN_GPU_FREEALLBUFS) {
        nn_lock(ctx, st->lock);
        for(int i = 1; i < NCL_MAX_VRAMBUF; i++) {
            if(st->vram[i] != NULL) {
                st->vramFree +=
                    (size_t)st->vram[i]->width
                    * st->vram[i]->height;
                ncl_freeVRAM(ctx, st->vram[i]);
                st->vram[i] = NULL;
            }
        }
        st->activeBuffer = 0;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    //  VRAM: freeMemory 
    if(req->action == NN_GPU_FREEMEM) {
        nn_lock(ctx, st->lock);
        req->memory = st->vramFree;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    //  VRAM: getBufferSize 
    if(req->action == NN_GPU_GETBUFSIZE) {
        int idx = req->bufSize.index;
        if(idx == 0) {
            // return screen resolution
            nn_lock(ctx, st->lock);
            ncl_ScreenState *scr =
                ncl_getBoundScreen(st, C);
            nn_unlock(ctx, st->lock);
            if(scr == NULL) {
                req->bufSize.w = 0;
                req->bufSize.h = 0;
                return NN_OK;
            }
            ncl_lockScreen(scr);
            req->bufSize.w = scr->width;
            req->bufSize.h = scr->height;
            ncl_unlockScreen(scr);
            return NN_OK;
        }
        nn_lock(ctx, st->lock);
        ncl_VRAMBuf *b =
            ncl_getVRAMBuf(st, idx, C);
        if(b == NULL) {
            nn_unlock(ctx, st->lock);
            return NN_EBADCALL;
        }
        req->bufSize.w = b->width;
        req->bufSize.h = b->height;
        nn_unlock(ctx, st->lock);
        return NN_OK;
    }
    //  VRAM: bitblt 
    if(req->action == NN_GPU_BITBLT) {
        int dstI = req->bitblt.dst;
        int srcI = req->bitblt.src;
        int col = req->bitblt.col;
        int row = req->bitblt.row;
        int w   = req->bitblt.w;
        int h   = req->bitblt.h;
        int fc  = req->bitblt.fromCol;
        int fr  = req->bitblt.fromRow;

        nn_lock(ctx, st->lock);
        if(srcI == 0 && st->activeBuffer != 0)
            srcI = st->activeBuffer;
        ncl_ScreenState *scr =
            ncl_getBoundScreen(st, C);

        ncl_VRAMBuf *sb = NULL, *db = NULL;
        if(srcI != 0) {
            sb = ncl_getVRAMBuf(st, srcI, C);
            if(sb == NULL) {
                nn_unlock(ctx, st->lock);
                return NN_EBADCALL;
            }
            if(w == 0) w = sb->width;
            if(h == 0) h = sb->height;
        }
        if(dstI != 0) {
            db = ncl_getVRAMBuf(st, dstI, C);
            if(db == NULL) {
                nn_unlock(ctx, st->lock);
                return NN_EBADCALL;
            }
        }
        nn_unlock(ctx, st->lock);

        // Lock both resources once for the
        // entire blit, not per-pixel.
        bool needScreen =
            (srcI == 0 || dstI == 0);
        if(needScreen && scr != NULL)
            ncl_lockScreen(scr);
        if(sb != NULL || db != NULL)
            nn_lock(ctx, st->lock);

        for(int y = 0; y < h; y++) {
            for(int x = 0; x < w; x++) {
                ncl_ScreenPixel p;
                int rx = fc + x, ry = fr + y;
                if(srcI == 0) {
                    if(scr == NULL) continue;
                    p = ncl_getRealScreenPixel(
                        scr, rx, ry);
                } else {
                    p = ncl_vramGet(sb, rx, ry);
                }
                int wx = col + x, wy = row + y;
                if(dstI == 0) {
                    if(scr == NULL) continue;
                    p.realFg = nn_mapDepth(
                        p.storedFg, scr->depth);
                    p.realBg = nn_mapDepth(
                        p.storedBg, scr->depth);
                    ncl_setRealScreenPixel(
                        scr, wx, wy, p);
                } else {
                    ncl_vramSet(db, wx, wy, p);
                }
            }
        }

        if(sb != NULL || db != NULL)
            nn_unlock(ctx, st->lock);
        if(needScreen && scr != NULL)
            ncl_unlockScreen(scr);
        return NN_OK;
    }

    if(C) nn_setError(C,
        "ncl-gpu: not implemented");
    return NN_EBADCALL;
}

nn_Component *ncl_createGPU(
    nn_Universe *universe, const char *address,
    const nn_GPU *gpu)
{
    nn_Context *ctx = nn_getUniverseContext(universe);
    nn_Lock *lock = NULL;
    ncl_GPUState *state = NULL;
    nn_Component *c = NULL;

    lock = nn_createLock(ctx);
    if(lock == NULL) goto fail;

    state = nn_alloc(ctx, sizeof(*state));
    if(state == NULL) goto fail;

    state->ctx = ctx;
    state->lock = lock;
    state->conf = *gpu;
    state->vramFree = gpu->totalVRAM;
    state->screenAddress = NULL;
    state->currentFg = 0xFFFFFF;
    state->currentBg = 0x000000;
    state->activeBuffer = 0;
    state->isFgPalette = false;
    state->isBgPalette = false;
    for(size_t i = 0; i < NCL_MAX_VRAMBUF; i++)
        state->vram[i] = NULL;

    c = nn_createGPU(universe, address,
        gpu, state, ncl_gpuHandler);
    if(c == NULL) goto fail;

    if(nn_setComponentTypeID(c, NCL_GPU)) goto fail;
    return c;

fail:
    if(c != NULL) {
        nn_dropComponent(c);
        return NULL;
    }
    if(lock != NULL) nn_destroyLock(ctx, lock);
    nn_free(ctx, state, sizeof(*state));
    return NULL;
}

void ncl_lockScreen(ncl_ScreenState *state) {
	nn_lock(state->ctx, state->lock);
}

void ncl_unlockScreen(ncl_ScreenState *state) {
	nn_unlock(state->ctx, state->lock);
}

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

void ncl_setScreenPixel(ncl_ScreenState *state, int x, int y, nn_codepoint codepoint, int fg, int bg, bool isFgPalette, bool isBgPalette) {
	ncl_ScreenPixel p = {
		.codepoint = codepoint,
		.storedFg = fg,
		.storedBg = bg,
		.realFg = isFgPalette ? -1 : fg,
		.realBg = isBgPalette ? -1 : bg,
	};
	ncl_setRealScreenPixel(state, x, y, p);
	ncl_recomputeScreen(state);
}

ncl_ScreenFlags ncl_getScreenFlags(const ncl_ScreenState *state) {
	return state->flags;
}

void ncl_setScreenFlags(ncl_ScreenState *state, ncl_ScreenFlags flags) {
	state->flags = flags;
}

char ncl_getScreenDepth(ncl_ScreenState *state) {
	return state->depth;
}

void ncl_setScreenDepth(ncl_ScreenState *state, char depth) {
	state->depth = depth;
}

nn_Exit ncl_mountKeyboard(ncl_ScreenState *state,
    const char *keyboardAddress)
{
    nn_lock(state->ctx, state->lock);
    if(state->keyboardCount >= NCL_MAX_KEYBOARD) {
        nn_unlock(state->ctx, state->lock);
        return NN_ELIMIT;
    }
    char *addr = nn_strdup(
        state->ctx, keyboardAddress);
    if(addr == NULL) {
        nn_unlock(state->ctx, state->lock);
        return NN_ENOMEM;
    }
    state->keyboards[state->keyboardCount++] = addr;
    nn_unlock(state->ctx, state->lock);
    return NN_OK;
}

void ncl_unmountKeyboard(ncl_ScreenState *state,
    const char *keyboardAddress)
{
    nn_lock(state->ctx, state->lock);
    size_t j = 0;
    for(size_t i = 0; i < state->keyboardCount; i++) {
        if(strcmp(state->keyboards[i],
                  keyboardAddress) == 0) {
            nn_strfree(state->ctx,
                state->keyboards[i]);
        } else {
            state->keyboards[j++] =
                state->keyboards[i];
        }
    }
    state->keyboardCount = j;
    nn_unlock(state->ctx, state->lock);
}

bool ncl_hasKeyboard(ncl_ScreenState *state,
    const char *keyboardAddress)
{
    nn_lock(state->ctx, state->lock);
    for(size_t i = 0; i < state->keyboardCount; i++) {
        if(strcmp(state->keyboards[i],
                  keyboardAddress) == 0) {
            nn_unlock(state->ctx, state->lock);
            return true;
        }
    }
    nn_unlock(state->ctx, state->lock);
    return false;
}

const char *ncl_getKeyboard(ncl_ScreenState *state,
    size_t idx)
{
    if(idx >= state->keyboardCount) return NULL;
    return state->keyboards[idx];
}

// general stuff

bool ncl_isNCLID(const char *type) {
	return strncmp(NCL_PREFIX, type, strlen(NCL_PREFIX));
}

bool ncl_isNCLComponent(nn_Component *component) {
	return ncl_isNCLID(nn_getComponentTypeID(component));
}

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
		nn_unlock(ee->ctx, ee->lock);
		return;
	}
	if(strcmp(ty, NCL_SCREEN) == 0) {
		ncl_ScreenState *screen = state;
		nn_lock(screen->ctx, screen->lock);
		stat->screen.conf = &screen->conf;
		stat->screen.depth = screen->depth;
		stat->screen.flags = screen->flags;
		stat->screen.keyboardCount = screen->keyboardCount;
		stat->screen.viewportWidth = screen->viewportWidth;
		stat->screen.viewportHeight = screen->viewportHeight;
		stat->screen.state = screen;
		nn_unlock(screen->ctx, screen->lock);
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
		nn_lock(fs->ctx, fs->lock);
		fs->isReadonly = true;
		fs->usage++;
		nn_unlock(fs->ctx, fs->lock);
		return true;
	}
	if(strcmp(ty, NCL_DRIVE) == 0) {
		ncl_DriveState *drv = state;
		nn_lock(drv->ctx, drv->lock);
		drv->isReadonly = true;
		drv->usage++;
		nn_unlock(drv->ctx, drv->lock);
		return true;
	}
	if(strcmp(ty, NCL_EEPROM) == 0) {
		ncl_EEState *ee = state;
		nn_lock(ee->ctx, ee->lock);
		ee->isReadonly = true;
		ee->usage++;
		nn_unlock(ee->ctx, ee->lock);
		return true;
	}
	return false;
}

// all of these are encoding states

nn_Exit ncl_encodeComponentState(nn_Universe *universe, nn_Component *comp, ncl_EncodedState *state);
void ncl_freeEncodedState(nn_Universe *universe, ncl_EncodedState *state);
nn_Exit ncl_loadComponentState(nn_Component *comp, const ncl_EncodedState *state);

size_t ncl_getLabel(nn_Component *c, char buf[NN_MAX_LABEL]);
size_t ncl_setLabel(nn_Component *c, const char *label, size_t len);
