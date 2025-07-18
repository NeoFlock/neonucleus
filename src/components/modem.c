#include "../neonucleus.h"

typedef struct nn_modem {
    nn_Context ctx;
    nn_guard *lock;
    nn_refc refc;
    nn_modemTable table;
    nn_networkControl ctrl;
} nn_modem;

nn_modem *nn_newModem(nn_Context *context, nn_modemTable table, nn_networkControl control) {
    nn_modem *m = nn_alloc(&context->allocator, sizeof(nn_modem));
    if(m == NULL) return m;
    m->ctx = *context;
    m->lock = nn_newGuard(context);
    m->refc = 1;
    m->table = table;
    m->ctrl = control;
    return m;
}

nn_guard *nn_getModemLock(nn_modem *modem) {
    return modem->lock;
}

void nn_retainModem(nn_modem *modem) {
    nn_incRef(&modem->refc);
}

nn_bool_t nn_destroyModem(nn_modem *modem) {
    if(!nn_decRef(&modem->refc)) return false;

    if(modem->table.deinit != NULL) {
        modem->table.deinit(modem->table.userdata);
    }

    nn_Context ctx = modem->ctx;
    nn_deleteGuard(&ctx, modem->lock);
    nn_dealloc(&ctx.allocator, modem, sizeof(nn_modem));
    return true;
}

void nn_modem_destroy(void *_, nn_component *component, nn_modem *modem) {
    nn_destroyModem(modem);
}

void nn_loadModemTable(nn_universe *universe) {
    nn_componentTable *modemTable = nn_newComponentTable(nn_getAllocator(universe), "modem", NULL, NULL, (nn_componentDestructor *)nn_modem_destroy);
    nn_storeUserdata(universe, "NN:MODEM", modemTable);
}

nn_component *nn_addModem(nn_computer *computer, nn_address address, int slot, nn_modem *modem) {
    nn_componentTable *modemTable = nn_queryUserdata(nn_getUniverse(computer), "NN:MODEM");
    return nn_newComponent(computer, address, slot, modemTable, modem);
}
