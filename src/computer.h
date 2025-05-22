#ifndef NEONUCLEUS_COMPUTER_H
#define NEONUCLEUS_COMPUTER_H

#include "neonucleus.h"

typedef struct nn_computer {
    char state;
    nn_guard *lock;
    nn_component *components;
    size_t componentLen;
    size_t componentCap;
    nn_value args[NN_MAX_ARGS];
    size_t argc;
    nn_value rets[NN_MAX_RETS];
    size_t retc;
    nn_architecture *arch;
    nn_architecture *nextArch;
    nn_architecture supportedArch[NN_MAX_ARCHITECTURES];
    size_t supportedArchCount;
    double timeOffset;
    nn_universe *universe;
    char *users[NN_MAX_USERS];
    size_t userCount;
} nn_computer;

#endif
