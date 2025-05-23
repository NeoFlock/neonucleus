#ifndef NEONUCLEUS_H
#define NEONUCLEUS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Based off https://stackoverflow.com/questions/5919996/how-to-detect-reliably-mac-os-x-ios-linux-windows-in-c-preprocessor
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
   //define something for Windows (32-bit and 64-bit, this part is common)
   #ifdef _WIN64
        #define NN_WINDOWS
   #else
        #error "Windows 32-bit is not supported"
   #endif
#elif __APPLE__
    #include <TargetConditionals.h>
    #if TARGET_IPHONE_SIMULATOR
        #error "iPhone Emulators are not supported"
    #elif TARGET_OS_MACCATALYST
        // I guess?
        #define NN_MACOS
    #elif TARGET_OS_IPHONE
        #error "iPhone are not supported"
    #elif TARGET_OS_MAC
        #define NN_MACOS
    #else
        #error "Unknown Apple platform"
    #endif
#elif __ANDROID__
    #error "Android is not supported"
#elif __linux__
    #define NN_LINUX
#endif

#if __unix__ // all unices not caught above
    // Unix
    #define NN_UNIX
    #define NN_POSIX
#elif defined(_POSIX_VERSION)
    // POSIX
    #define NN_POSIX
#endif

// The entire C API, in one header

// Magic limits
// If your component needs more than these, redesign your API.

#define NN_MAX_ARGS 32
#define NN_MAX_RETS 32
#define NN_MAX_METHODS 32
#define NN_MAX_USERS 128
#define NN_MAX_ARCHITECTURES 16
#define NN_MAX_SIGNALS 32
#define NN_MAX_SIGNAL_VALS 32
#define NN_MAX_USERDATA 1024
#define NN_MAX_USER_SIZE 128
#define NN_MAX_SIGNAL_SIZE 8192

typedef struct nn_guard nn_guard;
typedef struct nn_universe nn_universe;
typedef struct nn_computer nn_computer;
typedef struct nn_component nn_component;
typedef struct nn_componentTable nn_componentTable;
typedef struct nn_architecture {
    void *userdata;
    const char *archName;
    void *(*setup)(nn_computer *computer, void *userdata);
    void (*teardown)(nn_computer *computer, void *state, void *userdata);
    size_t (*getMemoryUsage)(nn_computer *computer, void *state, void *userdata);
    void (*tick)(nn_computer *computer, void *state, void *userdata);
    /* Pointer returned should be allocated with nn_malloc or nn_realloc, so it can be freed with nn_free */
    char *(*serialize)(nn_computer *computer, void *state, void *userdata, size_t *len);
    void (*deserialize)(nn_computer *computer, const char *data, size_t len, void *state, void *userdata);
} nn_architecture;
typedef char *nn_address;

// Values for architectures

#define NN_VALUE_INT 0
#define NN_VALUE_NUMBER 1
#define NN_VALUE_BOOL 2
#define NN_VALUE_CSTR 3
#define NN_VALUE_STR 4
#define NN_VALUE_ARRAY 5
#define NN_VALUE_TABLE 6
#define NN_VALUE_NIL 7

typedef struct nn_string {
    char *data;
    size_t len;
    size_t refc;
} nn_string;

typedef struct nn_array {
    struct nn_value *values;
    size_t len;
    size_t refc;
} nn_array;

typedef struct nn_object {
    struct nn_pair *pairs;
    size_t len;
    size_t refc;
} nn_table;

typedef struct nn_value {
    size_t tag;
    union {
        intptr_t integer;
        double number;
        bool boolean;
        const char *cstring;
        nn_string *string;
        nn_array *array;
        nn_table *table;
    };
} nn_value;

typedef struct nn_pair {
    nn_value key;
    nn_value val;
} nn_pair;

// we expose the allocator because of some utilities
void *nn_malloc(size_t size);
void *nn_realloc(void *memory, size_t newSize);
void nn_free(void *memory);

// Utilities, both internal and external
char *nn_strdup(const char *s);
void *nn_memdup(const void *buf, size_t len);

nn_guard *nn_newGuard();
void nn_lock(nn_guard *guard);
void nn_unlock(nn_guard *guard);
void nn_deleteGuard(nn_guard *guard);

double nn_realTime();
double nn_realTimeClock(void *_);

typedef double nn_clock_t(void *_);

nn_universe *nn_newUniverse();
void nn_unsafeDeleteUniverse(nn_universe *universe);
void *nn_queryUserdata(nn_universe *universe, const char *name);
void nn_storeUserdata(nn_universe *universe, const char *name, void *data);
void nn_setClock(nn_universe *universe, nn_clock_t *clock, void *userdata);
double nn_getTime(nn_universe *universe);

nn_computer *nn_newComputer(nn_universe *universe, nn_address address, nn_architecture *arch, void *userdata, size_t memoryLimit, size_t componentLimit);
int nn_tickComputer(nn_computer *computer);
double nn_getUptime(nn_computer *computer);
size_t nn_getComputerMemoryUsed(nn_computer *computer);
size_t nn_getComputerMemoryTotal(nn_computer *computer);
void *nn_getComputerUserData(nn_computer *computer);
void nn_addSupportedArchitecture(nn_computer *computer, nn_architecture *arch);
nn_architecture *nn_getSupportedArchitecture(nn_computer *computer, size_t idx);
nn_architecture *nn_getArchitecture(nn_computer *computer);
nn_architecture *nn_getNextArchitecture(nn_computer *computer);
void nn_setNextArchitecture(nn_computer *computer, nn_architecture *arch);
void nn_deleteComputer(nn_computer *computer);
const char *nn_pushSignal(nn_computer *computer, nn_value *values, size_t len);
nn_value nn_fetchSignalValue(nn_computer *computer, size_t index);
size_t nn_signalSize(nn_computer *computer);
void nn_popSignal(nn_computer *computer);
const char *nn_addUser(nn_computer *computer, const char *name);
void nn_deleteUser(nn_computer *computer, const char *name);
const char *nn_indexUser(nn_computer *computer, size_t idx);
bool nn_isUser(nn_computer *computer, const char *name);

/* The memory returned can be freed with nn_free() */
char *nn_serializeProgram(nn_computer *computer, size_t *len);
void nn_deserializeProgram(nn_computer *computer, const char *memory, size_t len);

void nn_lockComputer(nn_computer *computer);
void nn_unlockComputer(nn_computer *computer);

/// This means the computer has not yet started.
/// This is used to determine whether newComponent and removeComponent should emit signals.
#define NN_STATE_SETUP 0

/// This means the computer is running. There is no matching off-state, as the computer is
/// only off when it is deleted.
#define NN_STATE_RUNNING 1

/// This means a call budget exceeded, and the sandbox should make the computer yield.
#define NN_STATE_OVERUSED 2

/// This means a component's invocation could not be done due to a crucial resource being busy.
/// The sandbox should yield, then *invoke the component method again.*
#define NN_STATE_BUSY 3

/// This state occurs when a call to removeEnergy has consumed all the energy left.
/// The sandbox should yield, and the runner should shut down the computer.
/// No error is set, the sandbox can set it if it wanted to.
#define NN_STATE_BLACKOUT 4

/// This state only indicates that the runner should turn off the computer, but not due to a blackout.
/// The runner need not bring it back.
#define NN_STATE_CLOSING 5

/// This state indicates that the runner should turn off the computer, but not due to a blackout.
/// The runner should bring it back.
/// By "bring it back", we mean delete the computer, then recreate the entire state.
#define NN_STATE_REPEAT 6

/// This state indciates that the runner should turn off the computer, to switch architectures.
/// The architecture is returned by getNextArchitecture.
#define NN_STATE_SWITCH 7

int nn_getState(nn_computer *computer);
void nn_setState(nn_computer *computer, int state);

void nn_setEnergyInfo(nn_computer *computer, size_t energy, size_t capacity);
size_t nn_getEnergy(nn_computer *computer);
size_t nn_getMaxEnergy(nn_computer *computer);
void nn_removeEnergy(nn_computer *computer, size_t energy);
void nn_addEnergy(nn_computer *computer, size_t amount);

// NULL if there is no error.
const char *nn_getError(nn_computer *computer);
void nn_clearError(nn_computer *computer);
void nn_setError(nn_computer *computer, const char *err);
// this version does NOT allocate a copy of err, thus err should come from the data
// segment or memory with the same lifetime as the computer. This may not be possible
// in garbage-collected languages using this API, and thus should be avoided.
// This can be used by low-level implementations of architectures such that any
// internal out-of-memory errors can be reported. The normal setError would report
// no error if allocating the copy failed, and would clear any previous error.
void nn_setCError(nn_computer *computer, const char *err);

// Component stuff

nn_component *nn_newComponent(nn_computer *computer, nn_address address, int slot, nn_componentTable *table, void *userdata);
void nn_setTmpAddress(nn_computer *computer, nn_address tmp);
nn_address nn_getComputerAddress(nn_computer *computer);
nn_address nn_getTmpAddress(nn_computer *computer);
void nn_removeComponent(nn_computer *computer, nn_address address);
void nn_destroyComponent(nn_component *component);
nn_computer *nn_getComputerOfComponent(nn_component *component);
nn_address nn_getComponentAddress(nn_component *component);
int nn_getComponentSlot(nn_component *component);
nn_componentTable *nn_getComponentTable(nn_component *component);
void *nn_getComponentUserdata(nn_component *component);
nn_component *nn_findComponent(nn_computer *computer, nn_address address);

// Component VTable stuff

typedef void *nn_componentConstructor(void *userdata);
typedef void *nn_componentDestructor(void *tableUserdata, void *componentUserdata);
typedef void nn_componentMethod(void *componentUserdata, void *methodUserdata, nn_component *component, nn_computer *computer);

nn_componentTable *nn_newComponentTable(const char *typeName, void *userdata, nn_componentConstructor *constructor, nn_componentDestructor *destructor);
void nn_destroyComponentTable(nn_componentTable *table);
void nn_defineMethod(nn_componentTable *table, const char *methodName, bool direct, nn_componentMethod *methodFunc, void *methodUserdata, const char *methodDoc);
const char *nn_getTableMethod(nn_componentTable *table, size_t idx, bool *outDirect);
const char *nn_methodDoc(nn_componentTable *table, const char *methodName);

// Component calling

/* Returns false if the method does not exist */
bool nn_invokeComponentMethod(nn_component *component, const char *name);
void nn_resetCall(nn_computer *computer);
void nn_addArgument(nn_computer *computer, nn_value arg);
void nn_return(nn_computer *computer, nn_value val);
nn_value nn_getArgument(nn_computer *computer, size_t idx);
nn_value nn_getReturn(nn_computer *computer, size_t idx);
size_t nn_getArgumentCount(nn_computer *computer);
size_t nn_getReturnCount(nn_computer *computer);

// Value stuff

nn_value nn_values_nil();
nn_value nn_values_integer(intptr_t integer);
nn_value nn_values_number(double num);
nn_value nn_values_boolean(bool boolean);
nn_value nn_values_cstring(const char *string);
nn_value nn_values_string(const char *string, size_t len);
nn_value nn_values_array(size_t len);
nn_value nn_values_table(size_t pairCount);

size_t nn_values_getType(nn_value val);
nn_value nn_values_retain(nn_value val);
void nn_values_drop(nn_value val);

void nn_values_set(nn_value arr, size_t idx, nn_value val);
nn_value nn_values_get(nn_value arr, size_t idx);

void nn_values_setPair(nn_value obj, size_t idx, nn_value key, nn_value val);
nn_pair nn_values_getPair(nn_value obj, size_t idx);

intptr_t nn_toInt(nn_value val);
double nn_toNumber(nn_value val);
bool nn_toBoolean(nn_value val);
const char *nn_toCString(nn_value val);
const char *nn_toString(nn_value val, size_t *len);

/*
 * Computes the "packet size" of the values, using the same algorithm as OC.
 * This is used by pushSignal to check the size
 */
size_t nn_measurePacketSize(nn_value *vals, size_t len);

#endif
