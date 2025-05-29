#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "neonucleus.h"
#include "testLuaArch.h"
#include <raylib.h>

nn_eepromControl ne_eeprom_getControl(nn_component *component, void *_) {
    return (nn_eepromControl) {
        .randomLatencyMin = 0.001,
        .randomLatencyMax = 0.012,
        .readLatency = 0.03,
        .writeLatency = 0.05,
        .readCost = 3,
        .writeCost = 5,
        .readEnergyCost = 1,
        .writeEnergyCost = 5,
        .writeHeatCost = 0.2,
    };
}
    
size_t ne_eeprom_getSize(nn_component *component, void *_) {
    return 4096;
}

size_t ne_eeprom_getDataSize(nn_component *component, void *_) {
    return 1024;
}

void ne_eeprom_getLabel(nn_component *component, void *_, char *buf, size_t *buflen) {
    *buflen = 0;
}

size_t ne_eeprom_setLabel(nn_component *component, void *_, const char *buf, size_t buflen) {
    return 0;
}

const char *ne_location(nn_address address) {
    static char buffer[256];
    snprintf(buffer, 256, "data/%s", address);
    return buffer;
}

size_t ne_eeprom_get(nn_component *component, void *_, char *buf) {
    FILE *f = fopen(ne_location(nn_getComponentAddress(component)), "rb");
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    fread(buf, sizeof(char), len, f);
    fclose(f);
    return len;
}

void ne_eeprom_set(nn_component *component, void *_, const char *buf, size_t len) {
    FILE *f = fopen(ne_location(nn_getComponentAddress(component)), "wb");
    fwrite(buf, sizeof(char), len, f);
    fclose(f);
}

int ne_eeprom_getData(nn_component *component, void *_, char *buf) {
    return -1;
}

void ne_eeprom_setData(nn_component *component, void *_, const char *buf, size_t len) {}
    
bool ne_eeprom_isReadonly(nn_component *component, void *userdata) {
    return false;
}

void ne_eeprom_makeReadonly(nn_component *component, void *userdata) {}

#define NE_FS_MAX 128

typedef struct ne_fs {
    FILE *files[NE_FS_MAX];
    size_t fileLen;
} ne_fs;

nn_filesystemControl ne_fs_getControl(nn_component *component, ne_fs *_) {
    return (nn_filesystemControl) {
        .pretendChunkSize = 512,
        .pretendRPM = 12,
        .writeHeatPerChunk = 0.01,
        .writeCostPerChunk = 3,
        .writeEnergyCost = 0.015,
        .writeLatencyPerChunk = 0.0003,
        .readEnergyCost = 0.007,
        .readCostPerChunk = 1,
        .readLatencyPerChunk = 0.0001,
        .randomLatencyMin = 0.0005,
        .randomLatencyMax = 0.0019,
        .seekCostPerChunk = 1,
        .motorHeat = 0.03,
        .motorEnergyCost = 0.5,
    };
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

char **ne_fs_list(nn_component *component, ne_fs *fs, const char *path, size_t *len) {
    const char *p = ne_fs_diskPath(component, path);

    FilePathList files = LoadDirectoryFiles(p);
    *len = files.count;

    char **buf = nn_malloc(sizeof(char *) * files.count);
    for(size_t i = 0; i < files.count; i++) {
        buf[i] = nn_strdup(GetFileName(files.paths[i]));
    }

    UnloadDirectoryFiles(files);

    return buf;
}

bool ne_fs_isDirectory(nn_component *component, ne_fs *fs, const char *path) {
    const char *p = ne_fs_diskPath(component, path);

    return DirectoryExists(p);
}

bool ne_fs_makeDirectory(nn_component *component, ne_fs *fs, const char *path) {
    const char *p = ne_fs_diskPath(component, path);

    return MakeDirectory(p) == 0;
}

bool ne_fs_exists(nn_component *component, ne_fs *fs, const char *path) {
    const char *p = ne_fs_diskPath(component, path);

    return FileExists(p) || DirectoryExists(p);
}

int main() {
    printf("Setting up universe\n");
    nn_universe *universe = nn_newUniverse();
    nn_loadCoreComponentTables(universe);

    nn_architecture *arch = testLuaArch_getArchitecture("src/sandbox.lua");
    assert(arch != NULL && "Loading architecture failed");

    // 1MB of RAM, 16 components max
    nn_computer *computer = nn_newComputer(universe, "testMachine", arch, NULL, 1*1024*1024, 16);
    nn_setEnergyInfo(computer, 5000, 5000);
    nn_setCallBudget(computer, 18000);
    nn_addSupportedArchitecture(computer, arch);

    nn_eeprom genericEEPROM = {
        .userdata = NULL,
        .refc = 1,
        .deinit = NULL,
        .control = ne_eeprom_getControl,
        .getSize = ne_eeprom_getSize,
        .getDataSize = ne_eeprom_getDataSize,
        .getLabel = ne_eeprom_getLabel,
        .setLabel = ne_eeprom_setLabel,
        .get = ne_eeprom_get,
        .set = ne_eeprom_set,
        .getData = ne_eeprom_getData,
        .setData = ne_eeprom_setData,
        .isReadonly = ne_eeprom_isReadonly,
        .makeReadonly = ne_eeprom_makeReadonly,
    };

    nn_addEeprom(computer, "luaBios.lua", 0, &genericEEPROM);

    ne_fs fs = {
        .files = {NULL},
        .fileLen = 0,
    };

    nn_filesystem genericFS = {
        .refc = 0,
        .userdata = &fs,
        .deinit = NULL,
        .control = (void *)ne_fs_getControl,
        .getLabel = ne_eeprom_getLabel,
        .setLabel = ne_eeprom_setLabel,
        .spaceUsed = ne_fs_spaceUsed,
        .spaceTotal = ne_fs_spaceTotal,
        .isReadOnly = ne_eeprom_isReadonly,
        .size = NULL,
        .remove = NULL,
        .lastModified = NULL,
        .rename = NULL,
        .exists = (void *)ne_fs_exists,
        .isDirectory = (void *)ne_fs_isDirectory,
        .makeDirectory = (void *)ne_fs_makeDirectory,
        .list = (void *)ne_fs_list,
        .open = (void *)ne_fs_open,
        .close = (void *)ne_fs_close,
        .write = (void *)ne_fs_write,
        .read = (void *)ne_fs_read,
        .seek = NULL,
    };
    nn_addFileSystem(computer, "OpenOS", 1, &genericFS);

    double lastTime = nn_realTime();
    while(true) {
        double now = nn_realTime();
        double dt = now - lastTime;
        if(dt == 0) dt = 1.0/60;
        lastTime = now;

        // remove some heat per second
        nn_removeHeat(computer, dt * (rand() % 12));
        if(nn_isOverheating(computer)) {
            printf("Machine overheating.\n");
            continue;
        }

        int state = nn_tickComputer(computer);
        if(nn_isOverworked(computer)) {
            printf("Machine overworked.\n");
        }
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

    // destroy
    nn_deleteComputer(computer);
    nn_unsafeDeleteUniverse(universe);
    printf("Emulator is nowhere close to complete\n");
    return 0;
}
