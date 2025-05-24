#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "neonucleus.h"
#include "testLuaArch.h"
    
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

const char *ne_eeprom_location(nn_address address) {
    static char buffer[256];
    snprintf(buffer, 256, "data/%s", address);
    return buffer;
}

size_t ne_eeprom_get(nn_component *component, void *_, char *buf) {
    FILE *f = fopen(ne_eeprom_location(nn_getComponentAddress(component)), "rb");
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    fread(buf, sizeof(char), len, f);
    fclose(f);
    return len;
}

void ne_eeprom_set(nn_component *component, void *_, const char *buf, size_t len) {
    FILE *f = fopen(ne_eeprom_location(nn_getComponentAddress(component)), "wb");
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

int main() {
    printf("Setting up universe\n");
    nn_universe *universe = nn_newUniverse();
    nn_loadCoreComponentTables(universe);

    nn_architecture *arch = testLuaArch_getArchitecture("src/sandbox.lua");
    assert(arch != NULL && "Loading architecture failed");

    // 1MB of RAM, 16 components max
    nn_computer *computer = nn_newComputer(universe, "testMachine", arch, NULL, 1*1024*1024, 16);
    nn_setEnergyInfo(computer, 5000, 5000);
    nn_addSupportedArchitecture(computer, arch);

    nn_eeprom genericEEPROM = {
        .userdata = NULL,
        .refc = 1,
        .deinit = NULL,
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

    double lastTime = nn_realTime();
    while(true) {
        double now = nn_realTime();
        double dt = now - lastTime;
        if(dt == 0) dt = 1.0/60;
        lastTime = now;

        // remove some heat per second
        nn_removeHeat(computer, dt * (rand() % 12));
        if(nn_isOverheating(computer)) continue;

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

    // destroy
    nn_deleteComputer(computer);
    nn_unsafeDeleteUniverse(universe);
    printf("Emulator is nowhere close to complete\n");
    return 0;
}
