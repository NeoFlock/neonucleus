#ifndef NEONUCLEUS_COMPUTER_H
#define NEONUCLEUS_COMPUTER_H

#include "neonucleus.h"

typedef struct nn_signal {
    nn_size_t len;
    nn_value values[NN_MAX_SIGNAL_VALS];
} nn_signal;

typedef struct nn_resource_t {
	nn_size_t id;
	void *ptr;
	nn_resourceTable_t *table;
} nn_resource_t;

typedef struct nn_computer {
    char state;
    nn_bool_t allocatedError;
    char *err;
    void *userdata;
    nn_guard *lock;
    nn_component *components;
    nn_size_t componentLen;
    nn_size_t componentCap;
    nn_value args[NN_MAX_ARGS];
    nn_size_t argc;
    nn_value rets[NN_MAX_RETS];
    nn_size_t retc;
    nn_architecture *arch; // btw
    void *archState;
    nn_architecture *nextArch;
    nn_architecture *supportedArch[NN_MAX_ARCHITECTURES];
    nn_size_t supportedArchCount;
    double timeOffset;
    nn_universe *universe;
    char *users[NN_MAX_USERS];
    nn_size_t userCount;
    double energy;
    double maxEnergy;
    nn_signal signals[NN_MAX_SIGNALS];
    nn_size_t signalCount;
    nn_size_t memoryTotal;
    nn_address address;
    nn_address tmpAddress;
    double temperature;
    double temperatureCoefficient;
    double roomTemperature;
    double callCost;
    double callBudget;
	nn_size_t rid;
	nn_resource_t resources[NN_MAX_CONCURRENT_RESOURCES];
} nn_computer;

#endif
