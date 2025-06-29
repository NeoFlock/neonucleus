#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "neonucleus.h"
#include "testLuaArch.h"
#include <raylib.h>

Color ne_processColor(unsigned int color) {
    color <<= 8;
    color |= 0xFF;
    return GetColor(color);
}

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

char **ne_fs_list(nn_component *component, ne_fs *fs, const char *path, size_t *len) {
    const char *p = ne_fs_diskPath(component, path);
    if(p[0] == '/') p++;

    FilePathList files = LoadDirectoryFiles(p);
    *len = files.count;

    char **buf = nn_malloc(sizeof(char *) * files.count);
    for(size_t i = 0; i < files.count; i++) {
        buf[i] = nn_strdup(GetFileName(files.paths[i]));
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
        .seek = NULL,
    };
    nn_addFileSystem(computer, "OpenOS", 1, &genericFS);

    nn_screen *s = nn_newScreen(80, 32, 16, 16, 256);
    nn_addKeyboard(s, "shitty keyboard");
    nn_mountKeyboard(computer, "shitty keyboard", 2);
    nn_addScreen(computer, "Main Screen", 2, s);

    nn_gpuControl gpuCtrl = {
        .maxWidth = 240,
        .maxHeight = 80,
        .maxDepth = 16,

        .totalVRAM = 32*1024,
        .vramByteChangeCost = 0,
        .vramByteChangeEnergy = 0,
        .vramByteChangeHeat = 0,
        .vramByteChangeLatency = 0,

        .pixelChangeCost = 0,
        .pixelChangeEnergy = 0,
        .pixelChangeHeat = 0,
        .pixelChangeLatency = 0,
        
        .pixelResetCost = 0,
        .pixelResetEnergy = 0,
        .pixelResetHeat = 0,
        .pixelResetLatency = 0,

        .colorChangeLatency = 0,
        .colorChangeCost = 0,
        .colorChangeEnergy = 0,
        .colorChangeHeat = 0,
    };

    nn_addGPU(computer, "RTX 6090", 3, &gpuCtrl);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 600, "emulator");

    Font unscii = LoadFont("unscii-16-full.ttf");

    double lastTime = nn_realTime();

    static int release_check_list[256];
    memset(release_check_list, 0, sizeof(int)*256);
    uint8_t release_check_ptr;

    while(true) {
        if(WindowShouldClose()) break;

        while (true) { // TODO: find out if we can check if the keycode and unicode are for the same key event or not
            int keycode = GetKeyPressed();
            int unicode = GetCharPressed();

            if (keycode == 0 && unicode == 0) {
                break;
            }

            if (keycode != 0) {
                release_check_list[release_check_ptr++] = keycode;
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
            int key = release_check_list[i];
            if (key != 0) {
                if (IsKeyReleased(key)) {
                    // omg
                    nn_value values[5];
                    values[0] = nn_values_cstring("key_up");
                    values[1] = nn_values_cstring("shitty keyboard");
                    values[2] = nn_values_integer(0); // we can't really know, unless we store it, which i am way too lazy to do.
                    values[3] = nn_values_integer(keycode_to_oc(key));
                    values[4] = nn_values_cstring("USER");

                    const char* error = nn_pushSignal(computer, values, 5);

                    if (error != NULL) {
                        // well fuck
                        printf("error happened when eventing the keyboarding: %s\n", error);;;;;;
                    }
                }
            }
        }

        double now = nn_realTime();
        double dt = now - lastTime;
        if(dt == 0) dt = 1.0/60;
        lastTime = now;
        
        double heat = nn_getTemperature(computer);
        double roomHeat = nn_getRoomTemperature(computer);

        double tx = 0.1;

        // remove some heat per second
        nn_removeHeat(computer, dt * (rand() % 3) * tx * (heat - roomHeat));
        if(nn_isOverheating(computer)) {
            goto render;
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

render:
        BeginDrawing();

        ClearBackground(BLACK);

        int scrW = 1, scrH = 1;
        nn_getResolution(s, &scrW, &scrH);
        int pixelHeight = GetScreenHeight() / scrH;
        float spacing = (float)pixelHeight/10;
        int pixelWidth = MeasureTextEx(unscii, "A", pixelHeight, spacing).x;

        for(size_t x = 0; x < scrW; x++) {
            for(size_t y = 0; y < scrH; y++) {
                nn_scrchr_t p = nn_getPixel(s, x, y);

                // fuck palettes
                Color fgColor = ne_processColor(p.fg);
                Color bgColor = ne_processColor(p.bg);
                DrawRectangle(x * pixelWidth, y * pixelHeight, pixelWidth, pixelHeight, bgColor);
                DrawTextCodepoint(unscii, p.codepoint, (Vector2) {x * pixelWidth, y * pixelHeight}, pixelHeight - 5, fgColor);
            }
        }
        
        Color heatColor = GREEN;
        if(heat > 60) heatColor = YELLOW;
        if(heat > 80) heatColor = RED;
        DrawText(TextFormat("Heat: %lf\n", heat), 10, GetScreenHeight() - 30, 20, heatColor);

        EndDrawing();
    }

    // destroy
    nn_deleteComputer(computer);
    nn_unsafeDeleteUniverse(universe);
    CloseWindow();
    return 0;
}
