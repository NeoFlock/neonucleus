#ifndef NEONUCLEUS_COMPUTER_H
#define NEONUCLEUS_COMPUTER_H

#include "neonucleus.h"

typedef struct nn_computer {
    nn_component *components;
    size_t componentLen;
    size_t componentCap;
    nn_value args[NN_MAX_ARGS];
    size_t argc;
    nn_value rets[NN_MAX_RETS];
    size_t retc;
} nn_computer;

#endif
