#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "neonucleus.h"
#include "testLuaArch.h"
#include <raylib.h>

#ifdef NN_BAREMETAL

#ifdef NN_POSIX

#include <time.h>

static double nni_realTime() {
    struct timespec time;
    if(clock_gettime(CLOCK_MONOTONIC, &time) < 0) return 0; // oh no
    return time.tv_sec + ((double)time.tv_nsec) / 1e9;
}

#else

#include <windows.h>

static double nni_realTime() {
    LARGE_INTEGER frequency = {0};
    if(!QueryPerformanceFrequency(&frequency)) return 0;

    LARGE_INTEGER now = {0};
    if(!QueryPerformanceCounter(&now)) return 0;

    return (double)now.QuadPart / frequency.QuadPart;
}

#endif

static double nni_realTimeClock(void *_) {
    return nni_realTime();
}

nn_Clock nn_libcRealTime() {
    return (nn_Clock) {
        .userdata = NULL,
        .proc = nni_realTimeClock,
    };
}

static void *nn_libcAllocProc(void *_, void *ptr, nn_size_t oldSize, nn_size_t newSize, void *__) {
    if(newSize == 0) {
        //printf("Freed %lu bytes from %p\n", oldSize, ptr);
        free(ptr);
        return NULL;
    } else {
        void *rptr = realloc(ptr, newSize);
        //printf("Allocated %lu bytes for %p\n", newSize - oldSize, rptr);
        return rptr;
    }
}

nn_Alloc nn_libcAllocator() {
    return (nn_Alloc) {
        .userdata = NULL,
        .proc = nn_libcAllocProc,
    };
}

static nn_size_t nni_rand(void *userdata) {
    return rand();
}

nn_Rng nn_libcRng() {
    srand(time(NULL));
    return (nn_Rng) {
        .userdata = NULL,
        .maximum = RAND_MAX,
        .proc = nni_rand,
    };
}

nn_Context nn_libcContext() {
    return (nn_Context) {
        .allocator = nn_libcAllocator(),
        .clock = nn_libcRealTime(),
        .lockManager = nn_noMutex(),
        .rng = nn_libcRng(),
    };
}

#endif

Color ne_processColor(unsigned int color) {
    color <<= 8;
    color |= 0xFF;
    return GetColor(color);
}

nn_eepromControl ne_eeprom_ctrl = {
    .readHeatPerByte = 0.0015,
    .writeHeatPerByte = 0.03,
    .readEnergyCostPerByte = 0.001,
    .writeEnergyCostPerByte = 0.05,
    .bytesReadPerTick = 32768,
    .bytesWrittenPerTick = 4096,
};
    
void ne_eeprom_getLabel(void *_, char *buf, size_t *buflen) {
    *buflen = 0;
}

size_t ne_eeprom_setLabel(void *_, const char *buf, size_t buflen) {
    return 0;
}

const char *ne_location(nn_address address) {
    static char buffer[256];
    snprintf(buffer, 256, "data/%s", address);
    return buffer;
}

size_t ne_eeprom_get(void *addr, char *buf) {
    FILE *f = fopen(ne_location(addr), "rb");
    if (f == NULL) {
        printf("couldn't read eeprom");
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    fread(buf, sizeof(char), len, f);
    fclose(f);
    return len;
}

void ne_eeprom_set(void *addr, const char *buf, size_t len) {
    FILE *f = fopen(ne_location(addr), "wb");
    if (f == NULL) {
        printf("couldn't write eeprom");
        exit(1);
    }
    fwrite(buf, sizeof(char), len, f);
    fclose(f);
}

int ne_eeprom_getData(void *_, char *buf) {
    return 0;
}

void ne_eeprom_setData(void *_, const char *buf, size_t len) {}
    
nn_bool_t ne_eeprom_isReadonly(void *userdata) {
    return false;
}

void ne_eeprom_makeReadonly(void *userdata) {}

#define NE_FS_MAX 128

typedef struct ne_fs {
    FILE *files[NE_FS_MAX];
    size_t fileLen;
} ne_fs;

nn_filesystemControl ne_fs_getControl(nn_component *component, ne_fs *_) {
    return (nn_filesystemControl) {
        .readBytesPerTick = 65536,
        .writeBytesPerTick = 32768,
        .removeFilesPerTick = 16,
        .createFilesPerTick = 16,
        
        .readHeatPerByte = 0.0000015,
        .writeHeatPerByte = 0.000015,
        .removeHeat = 0.035,
        .createHeat = 0.045,

        .readEnergyPerByte = 0.0015,
        .writeEnergyPerByte = 0.0035,
        .removeEnergy = 0.135,
        .createEnergy = 0.325,
    };
}

void ne_fs_getLabel(nn_component *component, void *_, char *buf, size_t *buflen) {
    *buflen = 0;
}

nn_size_t ne_fs_setLabel(nn_component *component, void *_, const char *buf, size_t buflen) {
    return 0;
}

nn_bool_t ne_fs_isReadonly(nn_component *component, void *userdata) {
    return false;
}

size_t ne_fs_spaceUsed(nn_component *component, void *_) {
    return 0; // ultra accurate
}

size_t ne_fs_spaceTotal(nn_component *component, void *_) {
    return 1*1024*1024;
}

const char *ne_fs_diskPath(nn_component *component, const char *path) {
    static char buf[256];
    const char *root = ne_location(nn_getComponentAddress(component));
    if(path[0] == '/') {
        snprintf(buf, 256, "%s%s", root, path);
    } else {
        snprintf(buf, 256, "%s/%s", root, path);
    }

    return buf;
}

size_t ne_fs_open(nn_component *component, ne_fs *fs, const char *path, const char *mode) {
    if(fs->fileLen == NE_FS_MAX) {
        nn_setCError(nn_getComputerOfComponent(component), "too many files");
        return 0;
    }

    const char *trueMode = "rb";
    if(strcmp(mode, "w") == 0) {
        trueMode = "wb";
    }
    if(strcmp(mode, "a") == 0) {
        trueMode = "ab";
    }

    const char *p = ne_fs_diskPath(component, path);
    if(p[0] == '/') p++;
    FILE *f = fopen(p, trueMode);

    if(f == NULL) {
        nn_setCError(nn_getComputerOfComponent(component), strerror(errno));
        return 0;
    }

    for(size_t i = 0; i < fs->fileLen; i++) {
        if(fs->files[i] == NULL) {
            fs->files[i] = f;
            return i;
        }
    }

    size_t i = fs->fileLen++;
    fs->files[i] = f;
    return i;
}

bool ne_fs_close(nn_component *component, ne_fs *fs, int fd) {
    // we pray
    fclose(fs->files[fd]);
    fs->files[fd] = NULL;
    return true;
}

bool ne_fs_write(nn_component *component, ne_fs *fs, int fd, const char *buf, size_t len) {
    FILE *f = fs->files[fd];
    return fwrite(buf, sizeof(char), len, f) > 0;
}

size_t ne_fs_read(nn_component *component, ne_fs *fs, int fd, char *buf, size_t required) {
    FILE *f = fs->files[fd];
    if(feof(f)) return 0;
    return fread(buf, sizeof(char), required, f);
}

size_t ne_fs_seek(nn_component *component, ne_fs *fs, int fd, const char *whence, int off, int *moved) {
    FILE *f = fs->files[fd];
    *moved = 0;
    int w = SEEK_SET;
    if(strcmp(whence, "cur") == 0) {
        w = SEEK_CUR;
    }
    if(strcmp(whence, "end") == 0) {
        w = SEEK_END;
    }
    fseek(f, w, off);
    return ftell(f);
}

char **ne_fs_list(nn_Alloc *alloc, nn_component *component, ne_fs *fs, const char *path, size_t *len) {
    const char *p = ne_fs_diskPath(component, path);
    if(p[0] == '/') p++;

    FilePathList files = LoadDirectoryFiles(p);
    *len = files.count;

    char **buf = nn_alloc(alloc, sizeof(char *) * files.count);
    for(size_t i = 0; i < files.count; i++) {
        buf[i] = nn_strdup(alloc, GetFileName(files.paths[i]));
    }

    UnloadDirectoryFiles(files);

    return buf;
}

size_t ne_fs_size(nn_component *component, ne_fs *fs, const char *path) {
    const char *p = ne_fs_diskPath(component, path);
    if(p[0] == '/') p++;

    if(DirectoryExists(p)) return 0;

    return GetFileLength(p);
}

size_t ne_fs_lastModified(nn_component *component, ne_fs *fs, const char *path) {
    const char *p = ne_fs_diskPath(component, path);
    if(p[0] == '/') p++;

    return GetFileModTime(p);
}

bool ne_fs_isDirectory(nn_component *component, ne_fs *fs, const char *path) {
    const char *p = ne_fs_diskPath(component, path);
    if(p[0] == '/') p++;

    return DirectoryExists(p);
}

bool ne_fs_makeDirectory(nn_component *component, ne_fs *fs, const char *path) {
    const char *p = ne_fs_diskPath(component, path);
    if(p[0] == '/') p++;

    return MakeDirectory(p) == 0;
}

bool ne_fs_exists(nn_component *component, ne_fs *fs, const char *path) {
    const char *p = ne_fs_diskPath(component, path);
    if(p[0] == '/') p++;

    return FileExists(p) || DirectoryExists(p);
}

typedef struct ne_drive {
    FILE *file;
} ne_drive;

void ne_drive_close(nn_component *component, ne_drive *drive) {
    fclose(drive->file);
}
nn_driveControl ne_drive_getControl(nn_component *component, ne_drive *_) {
    return (nn_driveControl){};
}
size_t ne_drive_getPlatterCount(nn_component *component, ne_drive *_) {
    return 1;
}
size_t ne_drive_getSectorSize(nn_component *component, ne_drive *_) {
    return 512;
}
size_t ne_drive_getCapacity(nn_component *component, ne_drive *drive) {
    fseek(drive->file, 0, SEEK_END);
    return ftell(drive->file);
}
void ne_drive_readSector(nn_component *component, ne_drive *drive, int shifted_sector, char *buf) {
    int sector = shifted_sector - 1;
    size_t sectorSize = ne_drive_getSectorSize(component, drive);

    size_t offset = sector * sectorSize;
    fseek(drive->file, offset, SEEK_SET);
    fread(buf, sizeof(char), sectorSize, drive->file);
}
void ne_drive_writeSector(nn_component *component, ne_drive *drive, int shifted_sector, const char *buf) {
    int sector = shifted_sector - 1;
    size_t sectorSize = ne_drive_getSectorSize(component, drive);

    size_t offset = sector * sectorSize;
    fseek(drive->file, offset, SEEK_SET);
    fwrite(buf, sizeof(char), sectorSize, drive->file);

    // this is probably not needed but i believe someone isn't running the deinit
    fflush(drive->file);
}


int keycode_to_oc(int keycode) {
    switch (keycode) {
        case KEY_NULL:
            return 0;
        case KEY_APOSTROPHE:
            return 0x28;
        case KEY_COMMA:
            return 0x33;
        case KEY_MINUS:
            return 0x0C;
        case KEY_PERIOD:
            return 0x34;
        case KEY_SLASH:
            return 0x35;
        case KEY_ZERO:
            return 0x0B;
        case KEY_ONE:
            return 0x02;
        case KEY_TWO:
            return 0x03;
        case KEY_THREE:
            return 0x04;
        case KEY_FOUR:
            return 0x05;
        case KEY_FIVE:
            return 0x06;
        case KEY_SIX:
            return 0x07;
        case KEY_SEVEN:
            return 0x08;
        case KEY_EIGHT:
            return 0x09;
        case KEY_NINE:
            return 0x0A;
        case KEY_SEMICOLON:
            return 0x27;
        case KEY_EQUAL:
            return 0x0D;
        case KEY_A:
            return 0x1E;
        case KEY_B:
            return 0x30;
        case KEY_C:
            return 0x2E;
        case KEY_D:
            return 0x20;
        case KEY_E:
            return 0x12;
        case KEY_F:
            return 0x21;
        case KEY_G:
            return 0x22;
        case KEY_H:
            return 0x23;
        case KEY_I:
            return 0x17;
        case KEY_J:
            return 0x24;
        case KEY_K:
            return 0x25;
        case KEY_L:
            return 0x26;
        case KEY_M:
            return 0x32;
        case KEY_N:
            return 0x31;
        case KEY_O:
            return 0x18;
        case KEY_P:
            return 0x19;
        case KEY_Q:
            return 0x10;
        case KEY_R:
            return 0x13;
        case KEY_S:
            return 0x1F;
        case KEY_T:
            return 0x14;
        case KEY_U:
            return 0x16;
        case KEY_V:
            return 0x2F;
        case KEY_W:
            return 0x11;
        case KEY_X:
            return 0x2D;
        case KEY_Y:
            return 0x15;
        case KEY_Z:
            return 0x2C;
        case KEY_LEFT_BRACKET:
            return 0x1A;
        case KEY_BACKSLASH:
            return 0x2B;
        case KEY_RIGHT_BRACKET:
            return 0x1B;
        case KEY_GRAVE:
            return 0x29;
        case KEY_SPACE:
            return 0x39;
        case KEY_ESCAPE:
            return 0;
        case KEY_ENTER:
            return 0x1C;
        case KEY_TAB:
            return 0x0F;
        case KEY_BACKSPACE:
            return 0x0E;
        case KEY_INSERT:
            return 0xD2;
        case KEY_DELETE:
            return 0xD3;
        case KEY_RIGHT:
            return 0xCD;
        case KEY_LEFT:
            return 0xCB;
        case KEY_DOWN:
            return 0xD0;
        case KEY_UP:
            return 0xC8;
        case KEY_PAGE_UP:
            return 0xC9;
        case KEY_PAGE_DOWN:
            return 0xD1;
        case KEY_HOME:
            return 0xC7;
        case KEY_END:
            return 0xCF;
        case KEY_CAPS_LOCK:
            return 0x3A;
        case KEY_SCROLL_LOCK:
            return 0x46;
        case KEY_NUM_LOCK:
            return 0x45;
        case KEY_PRINT_SCREEN:
            return 0;
        case KEY_PAUSE:
            return 0xC5;
        case KEY_F1:
            return 0x3B;
        case KEY_F2:
            return 0x3C;
        case KEY_F3:
            return 0x3D;
        case KEY_F4:
            return 0x3E;
        case KEY_F5:
            return 0x3F;
        case KEY_F6:
            return 0x40;
        case KEY_F7:
            return 0x41;
        case KEY_F8:
            return 0x42;
        case KEY_F9:
            return 0x43;
        case KEY_F10:
            return 0x44;
        case KEY_F11:
            return 0x57;
        case KEY_F12:
            return 0x58;
        case KEY_LEFT_SHIFT:
            return 0x2A;
        case KEY_LEFT_CONTROL:
            return 0x1D;
        case KEY_LEFT_ALT:
            return 0x38;
        case KEY_LEFT_SUPER:
            return 0;
        case KEY_RIGHT_SHIFT:
            return 0x36;
        case KEY_RIGHT_CONTROL:
            return 0x9D;
        case KEY_RIGHT_ALT:
            return 0xB8;
        case KEY_RIGHT_SUPER:
            return 0;
        case KEY_KB_MENU:
            return 0;
        case KEY_KP_0:
            return 0x52;
        case KEY_KP_1:
            return 0x4F;
        case KEY_KP_2:
            return 0x50;
        case KEY_KP_3:
            return 0x51;
        case KEY_KP_4:
            return 0x4B;
        case KEY_KP_5:
            return 0x4C;
        case KEY_KP_6:
            return 0x4D;
        case KEY_KP_7:
            return 0x47;
        case KEY_KP_8:
            return 0x48;
        case KEY_KP_9:
            return 0x49;
        case KEY_KP_DECIMAL:
            return 0x54;
        case KEY_KP_DIVIDE:
            return 0xB5;
        case KEY_KP_MULTIPLY:
            return 0x37;
        case KEY_KP_SUBTRACT:
            return 0x4A;
        case KEY_KP_ADD:
            return 0x4E;
        case KEY_KP_ENTER:
            return 0x9C;
        case KEY_KP_EQUAL:
            return 0x8D;
        case KEY_BACK:
            return 0;
        case KEY_MENU:
            return 0;
        case KEY_VOLUME_DOWN:
            return 0;
        case KEY_VOLUME_UP:
            return 0;
    }
    return 0;
}

typedef struct ne_premappedPixel {
    int codepoint;
    int mappedDepth;
    int mappedFgFor;
    int mappedFgRes;
    int mappedBgFor;
    int mappedBgRes;
} ne_premappedPixel;

ne_premappedPixel ne_getPremap(ne_premappedPixel *pixels, nn_screen *screen, int x, int y) {
    int maxW, maxH;
    nn_maxResolution(screen, &maxW, &maxH);
    int depth = nn_getDepth(screen);

    int i = y * maxW + x;
    ne_premappedPixel premapped = pixels[i];

    nn_scrchr_t pixel = nn_getPixel(screen, x, y);
    int fg = pixel.fg;
    int bg = pixel.bg;

    if(premapped.mappedDepth != depth) {
        premapped.mappedDepth = depth;
        premapped.mappedFgFor = -1;
        premapped.mappedBgFor = -1;
    }

    bool miss = false;
    if(premapped.mappedFgFor != fg) {
        premapped.mappedFgFor = fg;
        premapped.mappedFgRes = nn_mapDepth(fg, depth);
        miss = true;
    }
    if(premapped.mappedBgFor != bg) {
        premapped.mappedBgFor = bg;
        premapped.mappedBgRes = nn_mapDepth(bg, depth);
        miss = true;
    }
    premapped.codepoint = pixel.codepoint;

    pixels[i] = premapped;
    return premapped;
}

ne_premappedPixel *ne_allocPremap(int width, int height) {
    int len = width * height;
    ne_premappedPixel *pixels = malloc(sizeof(ne_premappedPixel) * len);
    for(int i = 0; i < len; i++) pixels[i] = (ne_premappedPixel) {
        .mappedDepth = -1,
        .mappedFgFor = -1,
        .mappedBgFor = -1,
    };
    return pixels;
}

typedef struct ne_pressedKey {
    int charcode;
    int keycode;
    bool repeat;
} ne_pressedKey;

int main() {
    printf("Setting up universe\n");
    nn_Context ctx = nn_libcContext();
    nn_Alloc alloc = ctx.allocator;
    nn_universe *universe = nn_newUniverse(ctx);
    if(universe == NULL) {
        printf("Failed to create universe\n");
        return 1;
    }
    nn_loadCoreComponentTables(universe);

    nn_architecture *arch = testLuaArch_getArchitecture("src/sandbox.lua");
    assert(arch != NULL && "Loading architecture failed");

    // 1MB of RAM, 16 components max
    nn_computer *computer = nn_newComputer(universe, "testMachine", arch, NULL, 1*1024*1024, 16);
    nn_setEnergyInfo(computer, 5000, 5000);
    //nn_setCallBudget(computer, 18000);
    nn_addSupportedArchitecture(computer, arch);

    nn_eepromTable genericEEPROMTable = {
        .userdata = "luaBios.lua",
        .deinit = NULL,
        .size = 4096,
        .dataSize = 1024,
        .getLabel = ne_eeprom_getLabel,
        .setLabel = ne_eeprom_setLabel,
        .get = ne_eeprom_get,
        .set = ne_eeprom_set,
        .getData = ne_eeprom_getData,
        .setData = ne_eeprom_setData,
        .isReadonly = ne_eeprom_isReadonly,
        .makeReadonly = ne_eeprom_makeReadonly,
    };

    nn_eeprom *genericEEPROM = nn_newEEPROM(&ctx, genericEEPROMTable, ne_eeprom_ctrl);

    nn_addEeprom(computer, "luaBios.lua", 0, genericEEPROM);

    ne_fs fs = {
        .files = {NULL},
        .fileLen = 0,
    };

    nn_filesystem genericFS = {
        .refc = 0,
        .userdata = &fs,
        .deinit = NULL,
        .control = (void *)ne_fs_getControl,
        .getLabel = ne_fs_getLabel,
        .setLabel = ne_fs_setLabel,
        .spaceUsed = ne_fs_spaceUsed,
        .spaceTotal = ne_fs_spaceTotal,
        .isReadOnly = ne_fs_isReadonly,
        .size = (void *)ne_fs_size,
        .remove = NULL,
        .lastModified = (void *)ne_fs_lastModified,
        .rename = NULL,
        .exists = (void *)ne_fs_exists,
        .isDirectory = (void *)ne_fs_isDirectory,
        .makeDirectory = (void *)ne_fs_makeDirectory,
        .list = (void *)ne_fs_list,
        .open = (void *)ne_fs_open,
        .close = (void *)ne_fs_close,
        .write = (void *)ne_fs_write,
        .read = (void *)ne_fs_read,
        .seek = (void *)ne_fs_seek,
    };
    nn_addFileSystem(computer, "OpenOS", 1, &genericFS);

    ne_drive drive = {
        .file = fopen("data/drive.img", "r+")
    };
    assert(drive.file != NULL);

    nn_drive genericDrive = {
        .refc = 0,
        .userdata = &drive,
        .deinit = (void *)ne_drive_close,
        .control = (void *)ne_drive_getControl,
        .getLabel = ne_fs_getLabel,
        .setLabel = ne_fs_setLabel,
        .getPlatterCount = (void *)ne_drive_getPlatterCount,
        .getSectorSize = (void *)ne_drive_getSectorSize,
        .getCapacity = (void *)ne_drive_getCapacity,
        .readSector = (void *)ne_drive_readSector,
        .writeSector = (void *)ne_drive_writeSector,
    };

    nn_addDrive(computer, "drive.img", 4, &genericDrive);

    int maxWidth = 80, maxHeight = 32;

    nn_screen *s = nn_newScreen(&ctx, maxWidth, maxHeight, 24, 16, 256);
    nn_setDepth(s, 4); // looks cool
    nn_addKeyboard(s, "shitty keyboard");
    nn_mountKeyboard(computer, "shitty keyboard", 2);
    nn_addScreen(computer, "Main Screen", 2, s);

    ne_premappedPixel *premap = ne_allocPremap(maxWidth, maxHeight);

    nn_gpuControl gpuCtrl = {
        .totalVRAM = 16*1024,
        .screenCopyPerTick = 8,
        .screenFillPerTick = 16,
        .screenSetsPerTick = 32,
        .screenColorChangesPerTick = 64,

        .heatPerPixelChange = 0.0005,
        .heatPerPixelReset = 0.0001,
        .heatPerVRAMChange = 0.000015,

        .energyPerPixelChange = 0.05,
        .energyPerPixelReset = 0.01,
        .energyPerVRAMChange = 0.0015,
    };

    nn_addGPU(computer, "RTX 6090", 3, &gpuCtrl);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 600, "emulator");

    Font unscii = LoadFont("unscii-16-full.ttf");

    static ne_pressedKey release_check_list[256];
    memset(release_check_list, 0, sizeof(ne_pressedKey)*256);
    uint8_t release_check_ptr;

    SetExitKey(KEY_NULL);

    double idleTime = 0;
    int tps = 20; // mc TPS
    double interval = 1.0/tps;

    while(true) {
        if(WindowShouldClose()) break;
        nn_setEnergyInfo(computer, 5000, 5000);
        
        double dt = GetFrameTime();
        
        double heat = nn_getTemperature(computer);
        double roomHeat = nn_getRoomTemperature(computer);

        double tx = 0.1;
        
        // remove some heat per second
        nn_removeHeat(computer, dt * (rand() % 3) * tx * (heat - roomHeat));
        if(nn_isOverheating(computer)) {
            goto render;
        }
            
        while (true) { // TODO: find out if we can check if the keycode and unicode are for the same key event or not
            int keycode = GetKeyPressed();
            int unicode = GetCharPressed();

            if (keycode == 0 && unicode == 0) {
                break;
            }

            if (keycode != 0) {
                release_check_list[release_check_ptr].keycode = keycode;
                release_check_list[release_check_ptr].charcode = unicode;
                release_check_list[release_check_ptr].repeat = false;
                release_check_ptr++;
            }

            nn_value values[5];

            values[0] = nn_values_cstring("key_down");
            values[1] = nn_values_cstring("shitty keyboard");
            values[2] = nn_values_integer(unicode);
            values[3] = nn_values_integer(keycode_to_oc(keycode));
            values[4] = nn_values_cstring("USER");

            const char* error = nn_pushSignal(computer, values, 5);

            if (error != NULL) {
                // well fuck
                printf("error happened when eventing the keyboarding: %s\n", error);;;;;;
            }
        }

        for (int i = 0; i < 256; i++) {
            ne_pressedKey *key = release_check_list + i;
            if (key->keycode != 0) {
                key->repeat = IsKeyPressedRepeat(key->keycode);
                if (IsKeyReleased(key->keycode)) {
                    // omg
                    nn_value values[5];
                    values[0] = nn_values_cstring("key_up");
                    values[1] = nn_values_cstring("shitty keyboard");
                    values[2] = nn_values_integer(key->charcode);
                    values[3] = nn_values_integer(keycode_to_oc(key->keycode));
                    values[4] = nn_values_cstring("USER");

                    const char* error = nn_pushSignal(computer, values, 5);

                    if (error != NULL) {
                        // well fuck
                        printf("error happened when eventing the keyboarding: %s\n", error);;;;;;
                    }
                    key->keycode = 0;
                }
            }
        }

        idleTime += dt;

        if(idleTime >= interval) {
            idleTime -= interval;
        
            for (int i = 0; i < 256; i++) {
                ne_pressedKey *key = release_check_list + i;
                if (key->keycode != 0) {
                    if (key->repeat) {
                        // omg
                        nn_value values[5];
                        values[0] = nn_values_cstring("key_down");
                        values[1] = nn_values_cstring("shitty keyboard");
                        values[2] = nn_values_integer(key->charcode);
                        values[3] = nn_values_integer(keycode_to_oc(key->keycode));
                        values[4] = nn_values_cstring("USER");

                        const char* error = nn_pushSignal(computer, values, 5);

                        if (error != NULL) {
                            // well fuck
                            printf("error happened when eventing the keyboarding: %s\n", error);;;;;;
                        }
                    }
                }
            }

            int state = nn_tickComputer(computer);
            if(state == NN_STATE_SWITCH) {
                nn_architecture *nextArch = nn_getNextArchitecture(computer);
                printf("Next architecture: %s\n", nextArch->archName);
                break;
            } else if(state == NN_STATE_CLOSING || state == NN_STATE_REPEAT) {
                break;
            } else if(state == NN_STATE_BLACKOUT) {
                printf("blackout\n");
                break;
            }
            const char *e = nn_getError(computer);
            if(e != NULL) {
                printf("Error: %s\n", e);
                break;
            }
        }

render:
        BeginDrawing();

        ClearBackground(BLACK);

        int scrW = 1, scrH = 1;
        nn_getResolution(s, &scrW, &scrH);
        int pixelHeight = GetScreenHeight() / scrH;
        float spacing = (float)pixelHeight/10;
        int pixelWidth = MeasureTextEx(unscii, "A", pixelHeight, spacing).x;

        int depth = nn_getDepth(s);

        int offX = (GetScreenWidth() - scrW * pixelWidth) / 2;
        int offY = (GetScreenHeight() - scrH * pixelHeight) / 2;

        for(size_t x = 0; x < scrW; x++) {
            for(size_t y = 0; y < scrH; y++) {
                ne_premappedPixel p = ne_getPremap(premap, s, x, y);

                // fuck palettes
                Color fgColor = ne_processColor(p.mappedFgRes);
                Color bgColor = ne_processColor(p.mappedBgRes);
                DrawRectangle(x * pixelWidth + offX, y * pixelHeight + offY, pixelWidth, pixelHeight, bgColor);
                DrawTextCodepoint(unscii, p.codepoint, (Vector2) {x * pixelWidth + offX, y * pixelHeight + offY}, pixelHeight - 5, fgColor);
            }
        }
        
        Color heatColor = GREEN;
        if(heat > 60) heatColor = YELLOW;
        if(heat > 80) heatColor = RED;

        size_t memUsage = nn_getComputerMemoryUsed(computer);
        size_t memTotal = nn_getComputerMemoryTotal(computer);

        DrawText(TextFormat("Heat: %.02lf Memory Used: %.2lf%%", heat, (double)memUsage / memTotal * 100), 10, GetScreenHeight() - 30, 20, heatColor);
        DrawFPS(10, 10);

        EndDrawing();
    }

    // destroy
    nn_deleteComputer(computer);
    nn_unsafeDeleteUniverse(universe);
    CloseWindow();
    free(premap);
    return 0;
}
