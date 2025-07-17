#include "neonucleus.h"
#include "component.h"

nn_componentTable *nn_newComponentTable(nn_Alloc *alloc, const char *typeName, void *userdata, nn_componentConstructor *constructor, nn_componentDestructor *destructor) {
    nn_componentTable *table = nn_alloc(alloc, sizeof(nn_componentTable));
    if(table == NULL) {
        return NULL;
    }
    table->name = nn_strdup(alloc, typeName);
    if(table->name == NULL) {
        nn_dealloc(alloc, table, sizeof(nn_componentTable));
        return NULL;
    }
    table->userdata = userdata;
    table->constructor = constructor;
    table->destructor = destructor;
    table->methodCount = 0;
    table->alloc = *alloc;
    return table;
}

void nn_destroyComponentTable(nn_componentTable *table) {
    nn_Alloc alloc = table->alloc;
    nn_deallocStr(&alloc, table->name);
    for(nn_size_t i = 0; i < table->methodCount; i++) {
        nn_method_t method = table->methods[i];
        nn_deallocStr(&alloc, method.name);
        nn_deallocStr(&alloc, method.doc);
    }
    nn_dealloc(&alloc, table, sizeof(nn_componentTable));
}

nn_method_t *nn_defineMethod(nn_componentTable *table, const char *methodName, nn_componentMethod *methodFunc, const char *methodDoc) {
    if(table->methodCount == NN_MAX_METHODS) return NULL;
    nn_method_t method;
    method.method = methodFunc;
    method.name = nn_strdup(&table->alloc, methodName);
    if(method.name == NULL) return NULL;
    method.direct = true;
    method.doc = nn_strdup(&table->alloc, methodDoc);
    if(method.doc == NULL) {
        nn_deallocStr(&table->alloc, method.name);
        return NULL;
    }
    method.userdata = NULL;
    method.condition = NULL;
    table->methods[table->methodCount] = method;
    nn_method_t *ptr = table->methods + table->methodCount;
    table->methodCount++;
    return ptr;
}

void nn_method_setDirect(nn_method_t *method, nn_bool_t direct) {
    method->direct = direct;
}

void nn_method_setUserdata(nn_method_t *method, void *userdata) {
    method->userdata = userdata;
}

void nn_method_setCondition(nn_method_t *method, nn_componentMethodCondition_t *condition) {
    method->condition = condition;
}

const char *nn_getTableMethod(nn_componentTable *table, nn_size_t idx, nn_bool_t *outDirect) {
    if(idx >= table->methodCount) return NULL;
    nn_method_t method = table->methods[idx];
    if(outDirect != NULL) *outDirect = method.direct;
    return method.name;
}

const char *nn_methodDoc(nn_componentTable *table, const char *methodName) {
    for(nn_size_t i = 0; i < table->methodCount; i++) {
        if(nn_strcmp(table->methods[i].name, methodName) == 0) {
            return table->methods[i].doc;
        }
    }
    return NULL;
}

static nn_bool_t nni_checkMethodEnabled(nn_method_t method, void *statePtr) {
        if(method.condition == NULL) return true;
        return method.condition(statePtr, method.userdata);
}

nn_bool_t nn_isMethodEnabled(nn_component *component, const char *methodName) {
    nn_componentTable *table = component->table;
    for(nn_size_t i = 0; i < table->methodCount; i++) {
        if(nn_strcmp(table->methods[i].name, methodName) == 0) {
            nn_method_t method = table->methods[i];
            return nni_checkMethodEnabled(method, component->statePtr);
        }
    }
    return false;
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

nn_bool_t nn_invokeComponentMethod(nn_component *component, const char *name) {
    nn_componentTable *table = component->table;
    for(nn_size_t i = 0; i < table->methodCount; i++) {
        nn_method_t method = table->methods[i];
        if(nn_strcmp(method.name, name) == 0) {
            nn_callCost(component->computer, NN_CALL_COST);
            if(!method.direct) {
                nn_triggerIndirect(component->computer);
            }
            if(!nni_checkMethodEnabled(method, component->statePtr)) {
                return false; // pretend it's gone
            }
            method.method(component->statePtr, method.userdata, component, component->computer);
            return true;
        }
    }

    // no such method
    return false;
}

void nn_simulateBufferedIndirect(nn_component *component, double amount, double amountPerTick) {
    double maximum = 100.0;
    double x = amount * maximum / amountPerTick;
    component->indirectBufferProgress += x;
    if(component->indirectBufferProgress >= maximum) {
        component->indirectBufferProgress = 0;
        nn_triggerIndirect(component->computer);
    }
}
