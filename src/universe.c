#include "neonucleus.h"
#include "universe.h"

nn_universe *nn_newUniverse(nn_Context ctx) {
    nn_universe *u = nn_alloc(&ctx.allocator, sizeof(nn_universe));
    if(u == NULL) return u;
    u->ctx = ctx;
    // we leave udata uninitialized because it does not matter
    u->udataLen = 0;
    return u;
}

nn_Alloc *nn_getAllocator(nn_universe *universe) {
    return &universe->ctx.allocator;
}

void nn_unsafeDeleteUniverse(nn_universe *universe) {
    for(nn_size_t i = 0; i < universe->udataLen; i++) {
        nn_deallocStr(&universe->ctx.allocator, universe->udata[i].name);
    }
    nn_dealloc(&universe->ctx.allocator, universe, sizeof(nn_universe));
}

void *nn_queryUserdata(nn_universe *universe, const char *name) {
    for(nn_size_t i = 0; i < universe->udataLen; i++) {
        if(nn_strcmp(universe->udata[i].name, name) == 0) {
            return universe->udata[i].userdata;
        }
    }
    return NULL;
}

void nn_storeUserdata(nn_universe *universe, const char *name, void *data) {
    if(universe->udataLen == NN_MAX_USERDATA) return; // prevent overflow

    nn_size_t idx = universe->udataLen;
    char *allocName = nn_strdup(&universe->ctx.allocator, name);
    if(allocName == NULL) return;

    universe->udata[idx].name = allocName;
    universe->udata[idx].userdata = data;
    universe->udataLen++;
}

double nn_getTime(nn_universe *universe) {
    return universe->ctx.clock.proc(universe->ctx.clock.userdata);
}

void nn_loadCoreComponentTables(nn_universe *universe) {
    nn_loadEepromTable(universe);
    nn_loadFilesystemTable(universe);
    nn_loadDriveTable(universe);
    nn_loadScreenTable(universe);
    nn_loadGraphicsCardTable(universe);
    nn_loadKeyboardTable(universe);
    nn_loadModemTable(universe);
}
