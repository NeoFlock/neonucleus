#include "neonucleus.h"
#include <string.h>
#include "component.h"

nn_componentTable *nn_newComponentTable(const char *typeName, void *userdata, nn_componentConstructor *constructor, nn_componentDestructor *destructor) {
    nn_componentTable *table = nn_malloc(sizeof(nn_componentTable));
    table->name = nn_strdup(typeName);
    table->userdata = userdata;
    table->constructor = constructor;
    table->destructor = destructor;
    table->methodCount = 0;
    return table;
}

void nn_destroyComponentTable(nn_componentTable *table) {
    nn_free(table->name);
    for(size_t i = 0; i < table->methodCount; i++) {
        nn_method method = table->methods[i];
        nn_free(method.name);
        nn_free(method.doc);
    }
    nn_free(table);
}

void nn_defineMethod(nn_componentTable *table, const char *methodName, bool direct, nn_componentMethod *methodFunc, void *methodUserdata, const char *methodDoc) {
    if(table->methodCount == NN_MAX_METHODS) return;
    nn_method method;
    method.method = methodFunc;
    method.name = nn_strdup(methodName);
    if(method.name == NULL) return;
    method.direct = direct;
    method.doc = nn_strdup(methodDoc);
    if(method.doc == NULL) {
        nn_free(method.name);
        return;
    }
    method.userdata = methodUserdata;
    table->methods[table->methodCount] = method;
    table->methodCount++;
}

const char *nn_getTableMethod(nn_componentTable *table, size_t idx, bool *outDirect) {
    if(idx >= table->methodCount) return NULL;
    nn_method method = table->methods[idx];
    if(outDirect != NULL) *outDirect = method.direct;
    return method.name;
}

const char *nn_methodDoc(nn_componentTable *table, const char *methodName) {
    for(size_t i = 0; i < table->methodCount; i++) {
        if(strcmp(table->methods[i].name, methodName) == 0) {
            return table->methods[i].doc;
        }
    }
    return NULL;
}

nn_computer *nn_getComputerOfComponent(nn_component *component) {
    return component->computer;
}

nn_address nn_getComponentAddress(nn_component *component) {
    return component->address;
}

int nn_getComponentSlot(nn_component *component) {
    return component->slot;
}

nn_componentTable *nn_getComponentTable(nn_component *component) {
    return component->table;
}

const char *nn_getComponentType(nn_componentTable *table) {
    return table->name;
}

void *nn_getComponentUserdata(nn_component *component) {
    return component->statePtr;
}

bool nn_invokeComponentMethod(nn_component *component, const char *name) {
    nn_componentTable *table = component->table;
    for(size_t i = 0; i < table->methodCount; i++) {
        nn_method method = table->methods[i];
        if(strcmp(method.name, name) == 0) {
            nn_callCost(component->computer, NN_CALL_COST);
            if(!method.direct) {
                nn_busySleep(NN_INDIRECT_CALL_LATENCY);
            }
            method.method(component->statePtr, method.userdata, component, component->computer);
            return true;
        }
    }

    // no such method
    return false;
}
