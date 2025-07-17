#ifndef NEONUCLEUS_COMPONENT_H
#define NEONUCLEUS_COMPONENT_H

#include "neonucleus.h"
#include "computer.h"

typedef struct nn_method_t {
    char *name;
    nn_componentMethod *method;
    void *userdata;
    char *doc;
    nn_bool_t direct;
    nn_componentMethodCondition_t *condition;
} nn_method_t;

typedef struct nn_componentTable {
    char *name;
    nn_Alloc alloc;
    void *userdata;
    nn_componentConstructor *constructor;
    nn_componentDestructor *destructor;
    nn_method_t methods[NN_MAX_METHODS];
    nn_size_t methodCount;
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
