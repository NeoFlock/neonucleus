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

int main() {
	nn_Context ctx;
	nn_initContext(&ctx);

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

	nn_Computer *c = nn_createComputer(u, NULL, "computer0", 8 * NN_MiB, 256, 256);
	
	nn_setArchitecture(c, &arch);
	nn_addSupportedArchitecture(c, &arch);

	nn_addComponent(c, ctype, "sandbox", -1, NULL);
	nn_addComponent(c, etype, "eeprom", 0, etype);

	ne_FsState *mainFS = ne_newFS("OpenOS", false);
	nn_addComponent(c, fstype[1], "mainFS", 2, mainFS);
	
	while(true) {
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

cleanup:;
	nn_destroyComputer(c);
	nn_destroyComponentType(ctype);
	nn_destroyComponentType(etype);
	for(size_t i = 0; i < 5; i++) nn_destroyComponentType(fstype[i]);
	// rip the universe
	nn_destroyUniverse(u);
	return 0;
}
