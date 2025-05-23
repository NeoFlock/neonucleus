#include <assert.h>
#include <stdio.h>
#include "neonucleus.h"
#include "testLuaArch.h"

int main() {
    printf("Setting up universe\n");
    nn_universe *universe = nn_newUniverse();

    nn_architecture *arch = testLuaArch_getArchitecture("src/sandbox.lua");
    assert(arch != NULL && "Loading architecture failed");

    // 1MB of RAM, 16 components max
    nn_computer *computer = nn_newComputer(universe, "testMachine", arch, NULL, 1*1024*1024, 16);
    while(true) {
        nn_tickComputer(computer);
        const char *e = nn_getError(computer);
        if(e != NULL) {
            printf("Error: %s\n", e);
            break;
        }
        int state = nn_getState(computer);
        if(state == NN_STATE_CLOSING || state == NN_STATE_REPEAT || state == NN_STATE_SWITCH) {
            if(state == NN_STATE_SWITCH) {
                nn_architecture *nextArch = nn_getNextArchitecture(computer);
                printf("Next architecture: %s\n", nextArch->archName);
            }
            break;
        }
    }

    // destroy
    nn_deleteComputer(computer);
    nn_unsafeDeleteUniverse(universe);
    printf("Emulator is nowhere close to complete\n");
    return 0;
}
