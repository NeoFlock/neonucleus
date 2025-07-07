#ifndef NEONUCLEUS_UNIVERSE_H
#define NEONUCLEUS_UNIVERSE_H

#include "neonucleus.h"

typedef struct nn_universe_udata {
    char *name;
    void *userdata;
} nn_universe_udata;

typedef struct nn_universe {
    nn_Context ctx;
    nn_universe_udata udata[NN_MAX_USERDATA];
    size_t udataLen;
} nn_universe;

#endif
