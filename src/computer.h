#ifndef NEONUCLEUS_COMPUTER_H
#define NEONUCLEUS_COMPUTER_H

#include "neonucleus.h"

typedef struct nn_signal {
    size_t len;
    nn_value values[NN_MAX_SIGNAL_VALS];
} nn_signal;

typedef struct nn_computer {
    char state;
    bool allocatedError;
    char *err;
    void *userdata;
    nn_guard *lock;
    nn_component *components;
    size_t componentLen;
    size_t componentCap;
    nn_value args[NN_MAX_ARGS];
    size_t argc;
    nn_value rets[NN_MAX_RETS];
    size_t retc;
    nn_architecture *arch;
    void *archState;
    nn_architecture *nextArch;
    nn_architecture *supportedArch[NN_MAX_ARCHITECTURES];
    size_t supportedArchCount;
    double timeOffset;
    nn_universe *universe;
    char *users[NN_MAX_USERS];
    size_t userCount;
    double energy;
    double maxEnergy;
    nn_signal signals[NN_MAX_SIGNALS];
    size_t signalCount;
    size_t memoryTotal;
    nn_address address;
    nn_address tmpAddress;
    double temperature;
    double temperatureCoefficient;
    double roomTemperature;
    double callCost;
    double callBudget;
} nn_computer;

#endif
