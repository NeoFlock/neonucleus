#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "neonucleus.h"
#include "testLuaArch.h"

void emulator_debugPrint(void *componentUserdata, void *methodUserdata, nn_component *component, nn_computer *computer) {
    nn_value msg = nn_getArgument(computer, 0);
    const char *m = nn_toCString(msg);
    printf("[DEBUG] %s\n", m);
    nn_return(computer, nn_values_integer(strlen(m)));
}

int main() {
    printf("Setting up universe\n");
    nn_universe *universe = nn_newUniverse();

    nn_architecture *arch = testLuaArch_getArchitecture("src/sandbox.lua");
    assert(arch != NULL && "Loading architecture failed");

    // 1MB of RAM, 16 components max
    nn_computer *computer = nn_newComputer(universe, "testMachine", arch, NULL, 1*1024*1024, 16);
    nn_setEnergyInfo(computer, 5000, 5000);
    nn_addSupportedArchitecture(computer, arch);

    nn_componentTable *t = nn_newComponentTable("debugPrint", NULL, NULL, NULL);
    nn_defineMethod(t, "log", false, emulator_debugPrint, NULL, "logs stuff");

    nn_newComponent(computer, "debugPrint", -1, t, NULL);

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
