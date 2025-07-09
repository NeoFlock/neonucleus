#ifndef NEONUCLEUS_COMPONENT_H
#define NEONUCLEUS_COMPONENT_H

#include "neonucleus.h"
#include "computer.h"

typedef struct nn_method {
    char *name;
    nn_componentMethod *method;
    void *userdata;
    char *doc;
    nn_bool_t direct;
} nn_method;

typedef struct nn_componentTable {
    char *name;
    nn_Alloc alloc;
    void *userdata;
    nn_componentConstructor *constructor;
    nn_componentDestructor *destructor;
    nn_method methods[NN_MAX_METHODS];
    size_t methodCount;
} nn_componentTable;

typedef struct nn_component {
    nn_address address;
    int slot;
    float indirectBufferProgress;
    nn_componentTable *table;
    void *statePtr;
    nn_computer *computer;
} nn_component;

#endif
