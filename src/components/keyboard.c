#include "../neonucleus.h"

void nn_loadKeyboardTable(nn_universe *universe) {
    nn_componentTable *keyboardTable = nn_newComponentTable("keyboard", NULL, NULL, NULL);
    nn_storeUserdata(universe, "NN:KEYBOARD", keyboardTable);
}

nn_component *nn_mountKeyboard(nn_computer *computer, nn_address address, int slot) {
    nn_componentTable *keyboardTable = nn_queryUserdata(nn_getUniverse(computer), "NN:KEYBOARD");
    return nn_newComponent(computer, address, slot, keyboardTable, NULL);
}
