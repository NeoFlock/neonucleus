#include <stdio.h>
#include "neonucleus.h"

int main() {
    printf("Setting up universe\n");
    nn_universe *universe = nn_newUniverse();

    // we need an arch

    // destroy
    nn_unsafeDeleteUniverse(universe);
    printf("Emulator is nowhere close to complete\n");
    return 0;
}
