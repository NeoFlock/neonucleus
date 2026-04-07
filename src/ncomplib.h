#ifndef NN_COMPLIB
#define NN_COMPLIB

#include "neonucleus.h"

#define NCL_PREFIX "ncl-"

#define NCL_EEPROM "ncl-eeprom"
#define NCL_FS "ncl-filesystem"
#define NCL_DRIVE "ncl-drive"
#define NCL_FLASH "ncl-nandflash"
#define NCL_GPU "ncl-gpu"
#define NCL_SCREEN "ncl-screen"

#define NCL_TMPFS "ncl-tmpfs"

// Default file cost.
// This is for a normal HDD/floppy.
#define NCL_FILECOST_DEFAULT 512

// File cost on an installer floppy.
// In OC, those are backed by a file called ZipFileInputStream.
// That class has a different implementation for spaceUsed,
// it is almost identical except it does not add the file cost.
// For that reason, it is recommended to set it to 0, for parity.
#define NCL_FILECOST_INSTALL 0

#define NCL_MAX_VRAMBUF 128
#define NCL_MAX_KEYBOARD 64

// very low-level actions
// some environment have VFSes so
// we support wrapping those

typedef struct ncl_Stat {
	// whether the entry is a directory
	bool isDirectory;
	// the logical size of the entry
	// as in, for files it is how many bytes are in there.
	// For directories, it should be 0.
	// Every entry has a base cost, and thus fear not,
	// it will not lead to infinite disk usage.
	// Instead, make their size representative of the
	// size on disk / number of entries.
	size_t size;
	// the real size.
	// This is for realSpaceUsed, and is a safety mechanism
	// against disk-hogging.
	size_t diskSize;
	// The UNIX timestamp of the last modified date
	// of the entry.
	intptr_t lastModified;
} ncl_Stat;

typedef enum ncl_VFSAction {
	NCL_VFS_OPEN,
	NCL_VFS_CLOSE,
	NCL_VFS_READ,
	NCL_VFS_SEEK,
	NCL_VFS_WRITE,

	NCL_VFS_OPENDIR,
	NCL_VFS_CLOSEDIR,
	NCL_VFS_READDIR,

	// non-recursively remove entry
	NCL_VFS_REMOVE,
	// non-recursively make directory
	NCL_VFS_MKDIR,

	NCL_VFS_STAT,
} ncl_VFSAction;

typedef struct ncl_VFSRequest {
	void *state;
	ncl_VFSAction action;
	union {
		struct {
			const char *path;
			// same r, w and a modes as regular filesystem component
			const char *mode;
			void *file;
		} open;
		struct {
			// set to NULL for EoF
			char *buf;
			size_t len;
			void *file;
		} read;
		struct {
			const char *buf;
			size_t len;
			void *file;
		} write;
		struct {
			nn_FSWhence whence;
			int off;
			void *file;
		} seek;
		void *close;
		struct {
			const char *path;
			void *dir;
		} opendir;
		struct {
			// set to NULL for EoF
			// buffer size is NN_MAX_PATH.
			// Remember to account for terminator
			char *name;
			void *dir;
		} readdir;
		void *closedir;
		const char *remove;
		const char *mkdir;
		struct {
			// set to NULL if missing
			const char *path;
			ncl_Stat *stat;
		} stat;
		struct {
			// path of directory that / represents
			const char *path;
			// get the estimated amount of space
			// used up by an empty entry.
			// Used for enforcing capacity.
			size_t size;
		} entrysize;
	};
} ncl_VFSRequest;

typedef struct ncl_VFS {
	// the internal state
	void *state;
	// the handler.
	// True on success, false on failure.
	bool (*handler)(ncl_VFSRequest *request);
	// the path separator
	char pathsep;
	// the assumed cost of a file in spaceUsedIn.
	size_t fileCost;
} ncl_VFS;

// The default FS.
// Uses a basic implementation using POSIX/Windows FS APIs,
// or erroring on all operations if baremetal.
// Has default file cost (512)
extern ncl_VFS ncl_defaultFS;

// The installer FS.
// Same implementation as the default FS,
// but has a file cost of 0.
// This makes it accurate to ZipFileInputStreams in OC,
// which are used for the loot floppy disks.
// ENSURE ALL FILESYSTEMS WITH 0 FILE COST
// ARE READ-ONLY, OR ELSE YOU CAN BE SPAMMED
// ENDLESSLY WITH GIGABYTES OF DISK HOGGING.
extern ncl_VFS ncl_installerFS;

void *ncl_openfile(ncl_VFS vfs, const char *path, const char *mode);
void ncl_closefile(ncl_VFS vfs, void *file);
// returns false on EoF
bool ncl_readfile(ncl_VFS vfs, void *file, char *buf, size_t *len);
bool ncl_writefile(ncl_VFS vfs, void *file, const char *data, size_t len);
bool ncl_seekfile(ncl_VFS vfs, void *file, nn_FSWhence whence, int *off);
bool ncl_stat(ncl_VFS vfs, const char *path, ncl_Stat *stat);

void *ncl_opendir(ncl_VFS vfs, const char *path);
void ncl_closedir(ncl_VFS vfs, void *dir);
// returns false on EoF
bool ncl_readdir(ncl_VFS vfs, void *dir, char name[NN_MAX_PATH]);
size_t ncl_spaceUsedIn(ncl_VFS vfs, const char *path);
// gets the real space used
size_t ncl_spaceUsedBy(ncl_VFS vfs, const char *path);

bool ncl_exists(ncl_VFS vfs, const char *path);

bool ncl_remove(ncl_VFS vfs, const char *path);
bool ncl_removeRecursive(ncl_VFS vfs, const char *path);

bool ncl_mkdir(ncl_VFS vfs, const char *path);
bool ncl_mkdirRecursive(ncl_VFS vfs, const char *path);

bool ncl_copyto(ncl_VFS vfs, const char *from, const char *to);

typedef struct ncl_EncodedState {
	char *buf;
	size_t len;
} ncl_EncodedState;

nn_Exit ncl_encodeComponentState(nn_Universe *universe, nn_Component *comp, ncl_EncodedState *state);
void ncl_freeEncodedState(nn_Universe *universe, ncl_EncodedState *state);
nn_Exit ncl_loadComponentState(nn_Component *comp, const ncl_EncodedState *state);

size_t ncl_getLabel(nn_Component *c, char buf[NN_MAX_LABEL]);
size_t ncl_setLabel(nn_Component *c, const char *label, size_t len);
size_t ncl_setCLabel(nn_Component *c, const char *label);

nn_Component *ncl_createFilesystem(nn_Universe *universe, const char *address, const char *path, const nn_Filesystem *fs, bool isReadonly);

// Creates a tmpfs.
// This component is mostly treated like a normal filesystem,
// except you obviously cannot bind a vfs to it.
// Do note, it is illegal to mix encoded state between normal filesystem
// and tmpfs.
nn_Component *ncl_createTmpFS(nn_Universe *universe, const char *address, const nn_Filesystem *fs, size_t fileCost, bool isReadonly);

// this drive has its data in RAM.
// However, the data is not encoded in its state.
// Remember to read the entire drive and save it somewhere before dropping it.
nn_Component *ncl_createDrive(nn_Universe *universe, const char *address, const nn_Drive *drive, const char *data, size_t len, bool isReadonly);

// usable like a drive, but is a nandflash component
nn_Component *ncl_createFlash(nn_Universe *universe, const char *address, const nn_NandFlash *flash, const char *data, size_t len, bool isReadonly);

// data is stored interally
nn_Component *ncl_createEEPROM(nn_Universe *universe, const char *address, const nn_EEPROM *eeprom, const char *code, size_t codelen, bool isReadonly);

// Gets the VFS bound to a filesystem or drive.
// Returns the default FS if the component is not recognized.
ncl_VFS ncl_getVFS(nn_Component *component);

// Sets the VFS bound to a filesystem or drive.
// This determines the filesystem the operations are run in.
// Returns the old VFS.
ncl_VFS ncl_setVFS(nn_Component *component, ncl_VFS vfs);

// TODO, stuff we could implement:
// redstone, hologram, oled, ipu, vt, led, tape_drive, cd_drive, serial, colorful_lamp

typedef struct ncl_Pixel {
	// 0xRRGGBB format
	unsigned int fgColor;
	// 0xRRGGBB format
	unsigned int bgColor;
	// the codepoint
	nn_codepoint codepoint;
} ncl_Pixel;

typedef struct ncl_ScreenState ncl_ScreenState;

typedef enum ncl_ScreenFlags {
	NCL_SCREEN_ON = 1<<0,
	NCL_SCREEN_PRECISE = 1<<1,
	NCL_SCREEN_TOUCHINVERTED = 1<<2,
} ncl_ScreenFlags;

nn_Component *ncl_createScreen(nn_Universe *universe, const char *address, const nn_ScreenConfig *config);
nn_Component *ncl_createGPU(nn_Universe *universe, const char *address, const nn_GPU *gpu);

typedef struct ncl_ComponentStat {
	// common ones
	char label[NN_MAX_LABEL];
	size_t labellen;
	// used for indicating usage. If higher than last time, something happened.
	// This can be used to show a light or play a sound.
	size_t usageCounter;
	bool isReadonly;
	// specific properties
	union {
		struct {
			const nn_EEPROM *conf;
			size_t codeUsed;
			size_t dataUsed;
		} eeprom;
		struct {
			const nn_Filesystem *conf;
			size_t spaceUsed;
			size_t realDiskUsage;
			size_t filesOpen;
			const char *path;
		} fs;
		struct {
			const nn_Drive *conf;
			size_t lastSector;
		} drive;
		struct {
			const nn_NandFlash *conf;
			size_t currentWriteCount;
			double wearlevel;
		} flash;
		struct {
			const nn_GPU *conf;
			size_t vramFree;
			size_t bufferCount;
			// can be NULL if there is none
			const char *boundScreen;
		} gpu;
		struct {
			const nn_ScreenConfig *conf;
			ncl_ScreenState *state;
			ncl_ScreenFlags flags;
			int viewportWidth;
			int viewportHeight;
			char depth;
			size_t keyboardCount;
		} screen;
	};
} ncl_ComponentStat;

bool ncl_isNCLID(const char *type);
bool ncl_isNCLComponent(nn_Component *component);
void ncl_statComponent(nn_Component *component, ncl_ComponentStat *stat);
// For EEPROMs, filesystems, drives
// Returns whether it was successful or not.
bool ncl_makeReadonly(nn_Component *component);

// Returns the amount of data written.
// The capacity MUST be at least the data size of the EEPROM.
size_t ncl_getEEPROMData(nn_Component *component, char *buf);
void ncl_setEEPROMData(nn_Component *component, const char *data, size_t len);

// Returns the amount of data written.
// The capacity MUST be at least the size of the EEPROM.
size_t ncl_getEEPROMCode(nn_Component *component, char *buf);
void ncl_setEEPROMCode(nn_Component *component, const char *data, size_t len);

size_t ncl_getEEPROMArch(nn_Component *component, char buf[NN_MAX_ARCHNAME]);
void ncl_setEEPROMArch(nn_Component *component, const char *arch, size_t len);

// Reads part of a drive.
// Off is 0-indexed.
size_t ncl_readDrive(nn_Component *component, size_t offset, char *buf, size_t len);
// Writes to part of a drive.
// Off is 0-indexed.
void ncl_writeDrive(nn_Component *component, size_t offset, const char *buf, size_t len);
// Returns the internal memory buffer with the drive data,
// and if len is not NULL, will also write the capacity.
// This is in case you do not want to risk OOM while saving the state.
char *ncl_getDriveBuffer(nn_Component *component, size_t *len);

void ncl_lockScreen(ncl_ScreenState *state);
void ncl_unlockScreen(ncl_ScreenState *state);
void ncl_resetScreen(ncl_ScreenState *state);
void ncl_getScreenResolution(const ncl_ScreenState *state, size_t *width, size_t *height);
void ncl_getScreenViewport(const ncl_ScreenState *state, size_t *width, size_t *height);
ncl_Pixel ncl_getScreenPixel(const ncl_ScreenState *state, int x, int y);
void ncl_setScreenPixel(ncl_ScreenState *state, int x, int y, nn_codepoint codepoint, int fg, int bg, bool isFgPalette, bool isBgPalette);
ncl_ScreenFlags ncl_getScreenFlags(const ncl_ScreenState *state);
void ncl_setScreenFlags(ncl_ScreenState *state, ncl_ScreenFlags flags);
char ncl_getScreenDepth(ncl_ScreenState *state);
void ncl_setScreenDepth(ncl_ScreenState *state, char depth);
nn_Exit ncl_mountKeyboard(ncl_ScreenState *state, const char *keyboardAddress);
void ncl_unmountKeyboard(ncl_ScreenState *state, const char *keyboardAddress);
bool ncl_hasKeyboard(ncl_ScreenState *state, const char *keyboardAddress);
const char *ncl_getKeyboard(ncl_ScreenState *state, size_t idx);
double ncl_getScreenEnergyUsage(ncl_ScreenState *state);
double ncl_getScreenBrightness(ncl_ScreenState *state);
void ncl_setScreenBrightness(ncl_ScreenState *state, double brightness);

#endif
