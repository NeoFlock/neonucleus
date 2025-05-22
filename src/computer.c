#include "computer.h"
#include "component.h"
#include "neonucleus.h"
#include <string.h>

nn_computer *nn_newComputer(nn_universe *universe, nn_address address, nn_architecture *arch, void *userdata, size_t memoryLimit, size_t componentLimit);
void nn_tickComputer(nn_computer *computer);
double nn_getUptime(nn_computer *computer);
size_t nn_getComputerMemoryUsed(nn_computer *computer);
size_t nn_getComputerMemoryTotal(nn_computer *computer);
void *nn_getComputerUserData(nn_computer *computer);
void nn_addSupportedArchitecture(nn_computer *computer, nn_architecture *arch);
nn_architecture *nn_getArchitecture(nn_computer *computer);
nn_architecture *nn_getNextArchitecture(nn_computer *computer);
void nn_setNextArchitecture(nn_computer *computer, nn_architecture *arch);
void nn_deleteComputer(nn_computer *computer);
void nn_pushSignal(nn_computer *computer, nn_value *values, size_t len);
nn_value nn_fetchSignalValue(nn_computer *computer, size_t index);
void nn_popSignal(nn_computer *computer);
void nn_addUser(nn_computer *computer, const char *name);
void nn_deleteUser(nn_computer *computer, const char *name);
const char *nn_indexUser(nn_computer *computer, size_t idx);
bool nn_isUser(nn_computer *computer, const char *name);

int nn_getState(nn_computer *computer) {
    return computer->state;
}

void nn_setState(nn_computer *computer, int state) {
    computer->state = state;
}

void nn_setEnergyInfo(nn_computer *computer, size_t energy, size_t capacity) {
    computer->energy = energy;
    computer->maxEnergy = capacity;
}

size_t nn_getEnergy(nn_computer *computer) {
    return computer->energy;
}

void nn_removeEnergy(nn_computer *computer, size_t energy) {
    if(computer->energy < energy) {
        // blackout
        computer->energy = 0;
        computer->state = NN_STATE_BLACKOUT;
        return;
    }
    computer->energy -= energy;
}

void nn_addEnergy(nn_computer *computer, size_t amount) {
    if(computer->maxEnergy - computer->energy < amount) {
        computer->energy = computer->maxEnergy;
        return;
    }
    computer->energy += amount;
}

const char *nn_getError(nn_computer *computer) {
    return computer->err;
}

void nn_clearError(nn_computer *computer) {
    if(computer->allocatedError) {
        nn_free(computer->err);
    }
    computer->err = NULL;
    computer->allocatedError = false;
}

void nn_setError(nn_computer *computer, const char *err) {
    nn_clearError(computer);
    char *copy = nn_strdup(err);
    if(copy == NULL) {
        nn_setCError(computer, "out of memory");
        return;
    }
    computer->err = copy;
    computer->allocatedError = true;
}

void nn_setCError(nn_computer *computer, const char *err) {
    nn_clearError(computer);
    // we pinky promise this is safe
    computer->err = (char *)err;
    computer->allocatedError = false;
}

nn_component *nn_newComponent(nn_computer *computer, nn_address address, int slot, nn_componentTable *table, void *userdata);

void nn_removeComponent(nn_computer *computer, nn_address address) {
    for(size_t i = 0; i < computer->componentLen; i++) {
        if(strcmp(computer->components[i].address, address) == 0) {
            nn_destroyComponent(computer->components + i);
        }
    }
}

void nn_destroyComponent(nn_component *component) {
    nn_free(component->address);
    if(component->table->destructor != NULL) {
        component->table->destructor(component->table->userdata, component->statePtr);
    }
    component->address = NULL; // marks component as freed
}

nn_component *nn_findComponent(nn_computer *computer, nn_address address) {
    for(size_t i = 0; i < computer->componentLen; i++) {
        if(strcmp(computer->components[i].address, address) == 0) {
            return computer->components + i;
        }
    }
    return NULL;
}

void nn_resetCall(nn_computer *computer) {
    for(size_t i = 0; i < computer->argc; i++) {
        nn_values_drop(computer->args[i]);
    }
    
    for(size_t i = 0; i < computer->retc; i++) {
        nn_values_drop(computer->rets[i]);
    }

    computer->argc = 0;
    computer->retc = 0;
}

void nn_addArgument(nn_computer *computer, nn_value arg) {
    if(computer->argc == NN_MAX_ARGS) return;
    computer->args[computer->argc] = arg;
    computer->argc++;
}

void nn_return(nn_computer *computer, nn_value val) {
    if(computer->retc == NN_MAX_RETS) return;
    computer->rets[computer->retc] = val;
    computer->retc++;
}

nn_value nn_getArgument(nn_computer *computer, size_t idx) {
    if(idx >= computer->argc) return nn_values_nil();
    return computer->args[idx];
}

nn_value nn_getReturn(nn_computer *computer, size_t idx) {
    if(idx >= computer->retc) return nn_values_nil();
    return computer->rets[idx];
}

size_t nn_getArgumentCount(nn_computer *computer) {
    return computer->argc;
}

size_t nn_getReturnCount(nn_computer *computer) {
    return computer->retc;
}
