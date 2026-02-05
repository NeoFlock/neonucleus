#ifndef NEONUCLEUS_H
#define NEONUCLEUS_H

#ifdef __cplusplus
extern "C" {
#endif

// every C standard header we depend on, conveniently put here
#include <stddef.h> // for NULL,
#include <stdint.h> // for intptr_t
#include <stdbool.h> // for true, false and bool

// Internally we need stdatomic.h and, if NN_BAREMETAL is not defined, stdlib.h and time.h

// The entire NeoNucleus API, in one header file

// Internal limits or constants

#define NN_KiB (1024)
#define NN_MiB (1024 * NN_KiB)
#define NN_GiB (1024 * NN_MiB)
#define NN_TiB (1024 * NN_TiB)

// the alignment an allocation should have
#define NN_ALLOC_ALIGN 16
// the maximum amount of items the callstack can have.
#define NN_MAX_STACK 256
// the maximum size a path is allowed to have
#define NN_MAX_PATH 256
// the maximum amount of bytes which can be read from a file.
// You are given a buffer you are meant to fill at least partially, this is simply the limit of that buffer's size.
#define NN_MAX_READ 65536
// maximum size of a wakeup message
#define NN_MAX_WAKEUPMSG 2048
// the maximum amount of file descriptors that can be open simultaneously
#define NN_MAX_OPENFILES 128
// the maximum amount of userdata that can be sent simultaneously.
#define NN_MAX_USERDATA 64
// maximum size of a signal, computed the same as modem packet costs.
#define NN_MAX_SIGNALSIZE 8192
// maximum amount of signals.
#define NN_MAX_SIGNALS 128
// the maximum value of a port. Ports start at 1.
#define NN_MAX_PORT 65535
// the magic port number to close all ports
#define NN_CLOSEPORTS 0
// maximum amount of architectures one machine can support.
#define NN_MAX_ARCHITECTURES 32
// maximum amount of keyboards a screen can have
#define NN_MAX_KEYBOARDS 64
// the port used by tunnel cards. This port is invalid for modems.
#define NN_TUNNEL_PORT 0

// the maximum size of a UTF-8 character
#define NN_MAXIMUM_UNICODE_BUFFER 4

// the maximum size of a component error message. If the error is bigger than this,
// it is truncated.
#define NN_MAX_ERROR_SIZE 1024

// The type of a the function used as the allocator.
// The expected behavior is as follows:
// alloc(state, NULL, 0, newSize) -> malloc(newSize)
// alloc(state, memory, oldSize, 0) -> free(memory)
// alloc(state, memory, oldSize, newSize) -> realloc(memory, newSize)
// 
// NeoNucleus will ensure oldSize is what the application requested on the last allocation.
// This is useful for allocators which may not do extensive bookkeeping.
// In the case of Out Of Memory, you are expected to return NULL.
typedef void *nn_AllocProc(void *state, void *memory, size_t oldSize, size_t newSize);

// Meant to return the time, in seconds, since some epoch.
typedef double nn_TimeProc(void *state);

typedef size_t nn_RngProc(void *state);

typedef enum nn_LockAction {
	// create the mutex 
	NN_LOCK_CREATE,
	// destroy the mutex
	NN_LOCK_DESTROY,
	// lock the mutex
	NN_LOCK_LOCK,
	// unlock the mutex
	NN_LOCK_UNLOCK,
} nn_LockAction;

typedef struct nn_LockRequest {
	// mutate it for NN_LOCK_INIT
	void *lock;
	nn_LockAction action;
} nn_LockRequest;

// intended for a plain mutex.
// This is used for synchronization. OpenComputers achieves synchronization
// between the worker threads by sending them as requests to a central thread (indirect methods).
// In NeoNucleus, we simply use a lock. This technically makes all methods direct, however
// we consider methods to be indirect if they require locks.
typedef void nn_LockProc(void *state, nn_LockRequest *req);

// The *context* NeoNucleus is operating in.
// This determines:
// - How memory is allocated
// - How random numbers are generated
// - What the current time is
typedef struct nn_Context {
	void *state;
	nn_AllocProc *alloc;
	nn_TimeProc *time;
	// the maximum value the RNG can produce.
	// The RNG is assumed to generate [0, rngMaximum]. INCLUSIVE.
	// When a [0, 1) range is needed, the value is divided by (rngMaximum+1),
	// so rngMaximum+1 MUST NOT OVERFLOW.
	size_t rngMaximum;
	nn_RngProc *rng;
	nn_LockProc *lock;
} nn_Context;

// if NN_BAREMETAL is defined when NeoNucleus is compiled,
// this function will fill in a bogus context that does nothing.
// Otherwise, it fills in a context which uses the C standard library.
// This function is only meant to be called once, to get the initial context.
// It does seed the RNG when using the C standard library, so do not
// call it in loops.
void nn_initContext(nn_Context *ctx);

typedef enum nn_Exit {
	// no error
	NN_OK = 0,
	// out of memory.
	NN_ENOMEM,
	// over the limit. For example, adding too many architectures to a machine.
	NN_ELIMIT,
	// internal stack underflow when managing the stack.
	NN_EBELOWSTACK,
	// internal stack overflow when carrying values.
	NN_ENOSTACK,
	// bad invocation, error message stored in computer state
	NN_EBADCALL,
	// bad state, the function was called at the wrong time
	NN_EBADSTATE,
} nn_Exit;

// This stores necessary data between computers
typedef struct nn_Universe nn_Universe;

nn_Universe *nn_createUniverse(nn_Context *ctx);
void nn_destroyUniverse(nn_Universe *universe);

// The actual computer
typedef struct nn_Computer nn_Computer;

typedef enum nn_ComputerState {
	// the machine is running just fine
	NN_RUNNING = 0,
	// machine is initializing. This is the state after createComputer but before the first nn_tick.
	// It is a required state of various initialization functions.
	NN_BOOTUP,
	// the machine has powered off.
	NN_POWEROFF,
	// the machine demands being restarted.
	NN_RESTART,
	// the machine has crashed. RIP
	NN_CRASHED,
	// the machine ran out of energy.
	NN_BLACKOUT,
	// change architecture.
	NN_CHARCH,
} nn_ComputerState;

typedef enum nn_ArchitectureAction {
	// create the local state
	NN_ARCH_INIT,
	// destroy the local state
	NN_ARCH_DEINIT,
	// run 1 tick
	NN_ARCH_TICK,
	// get the free memory
	NN_ARCH_FREEMEM,
} nn_ArchitectureAction;

typedef struct nn_ArchitectureRequest {
	// the state pointer passed through
	void *globalState;
	// the computer which made the request
	nn_Computer *computer;
	// the local state bound to this computer.
	// In NN_ARCH_INIT, this is NULL, and must be set to the new state, or an appropriate exit code returned.
	void *localState;
	// the action requested
	nn_ArchitectureAction action;
	union {
		// in the case of NN_ARCH_FREEMEM, the free memory
		size_t freeMemory;
	};
} nn_ArchitectureRequest;

typedef nn_Exit nn_ArchitectureHandler(nn_ArchitectureRequest *req);

typedef struct nn_Architecture {
	const char *name;
	void *state;
	nn_ArchitectureHandler *handler;
} nn_Architecture;

// The state of a *RUNNING* computer.
// Powered off computers shall not have a state, and as far as NeoNucleus is aware,
// not exist.
// The computer API *is not thread-safe*, so it is recommended that you use an external lock to manage it if you are running
// it in a multi-threaded environment.
// The userdata pointer is meant to store external data required by the environment.
// totalMemory is a hint to the architecture as to how much memory to allow the runner to use. It is in bytes.
// maxComponents and maxDevices determine how many components can be connected simultaneously to this computer, and how much device info can be
// registered on this computer.
nn_Computer *nn_createComputer(nn_Universe *universe, void *userdata, const char *address, size_t totalMemory, size_t maxComponents, size_t maxDevices);
// Destroys the state, effectively shutting down the computer.
void nn_destroyComputer(nn_Computer *computer);
// get the userdata pointer
void *nn_getComputerUserdata(nn_Computer *computer);
const char *nn_getComputerAddress(nn_Computer *computer);

// Sets the computer's architecture.
// The architecture determines everything from how the computer runs, to how it turns off.
// Everything is limited by the architecture.
// The architecture is copied, it can be freed after this is called.
void nn_setArchitecture(nn_Computer *computer, const nn_Architecture *arch);
// Gets the current architecture.
nn_Architecture nn_getArchitecture(nn_Computer *computer);
// Sets the computer's desired architecture.
// The desired architecture indicates, when the computer state is CHARCH, what the new architecture should be.
// This is set even if it is not in the supported architecture list, *you must check if it is in that list first.*
// The architecture is copied, it can be freed after this is called.
void nn_setDesiredArchitecture(nn_Computer *computer, const nn_Architecture *arch);
// Gets the desired architecture. This is the architecture the computer should use after changing architectures.
nn_Architecture nn_getDesiredArchitecture(nn_Computer *computer);
// Adds a new supported architecture, which indicates to the code running on this computer that it is possible to switch to that architecture.
// The architecture is copied, it can be freed after this is called.
nn_Exit nn_addSupportedArchitecture(nn_Computer *computer, const nn_Architecture *arch);
// Returns the array of supported architectures, as well as the length.
const nn_Architecture *nn_getSupportedArchitecture(nn_Computer *computer, size_t *len);
// Helper function for searching for an architecture using a computer which supports it and the architecture name.
// If the architecture is not found, it returns one with a NULL name.
nn_Architecture nn_findSupportedArchitecture(nn_Computer *computer, const char *name);

// sets the energy capacity of the computer.
void nn_setTotalEnergy(nn_Computer *computer, double maxEnergy);
// gets the energy capacity of the computer
double nn_getTotalEnergy(nn_Computer *computer);
// sets the current amount of energy
void nn_setEnergy(nn_Computer *computer, double energy);
// gets the current amount of energy
double nn_getEnergy(nn_Computer *computer);
// Returns true if there is no more energy left, and a blackout has occured.
bool nn_removeEnergy(nn_Computer *computer, double energy);

size_t nn_getTotalMemory(nn_Computer *computer);
size_t nn_getFreeMemory(nn_Computer *computer);
// gets the current uptime of a computer. When the computer is not running, this value can be anything and loses all meaning.
double nn_getUptime(nn_Computer *computer);

// copies the string into the local error buffer. The error is NULL terminated, but also capped by NN_MAX_ERROR_SIZE
void nn_setError(nn_Computer *computer, const char *s);
// copies the string into the local error buffer. The error is capped by NN_MAX_ERROR_SIZE-1. The -1 is there because the NULL terminator is still inserted at the end.
// Do note that nn_getError() still returns a NULL-terminated string, thus NULL terminators in this error will lead to a shortened error.
void nn_setLError(nn_Computer *computer, const char *s, size_t len);
// Gets a pointer to the local error buffer. This is only meaningful when NN_EBADCALL is returned.
const char *nn_getError(nn_Computer *computer);
// clears the computer's error buffer, making nn_getError return a simple empty string.
void nn_clearError(nn_Computer *computer);

// sets the computer state to the desired state. This is meant for architectures to report things like reboots or shutdowns, DO NOT ABUSE THIS.
void nn_setComputerState(nn_Computer *computer, nn_ComputerState state);
// gets the current computer state
nn_ComputerState nn_getComputerState(nn_Computer *computer);

// runs a tick of the computer. Make sure to check the state as well!
nn_Exit nn_tick(nn_Computer *computer);

typedef struct nn_DeviceInfoEntry {
	const char *name;
	const char *value;
} nn_DeviceInfoEntry;

typedef struct nn_DeviceInfo {
	const char *address;
	const nn_DeviceInfoEntry *entries;
} nn_DeviceInfo;

// adds some device information to the computer. This can also be removed.
// Entries is terminated by a NULL name, and preferrably also NULL value.
// It is perfectly fine to free entries after the call, it is copied.
nn_Exit nn_addDeviceInfo(nn_Computer *computer, const char *address, const nn_DeviceInfoEntry entries[]);
// Removes info assicated with a device
void nn_removeDeviceInfo(nn_Computer *computer, const char *address);
// gets the device info array.
const nn_DeviceInfo *nn_getDeviceInfo(nn_Computer *computer, size_t *len);

typedef struct nn_ComponentMethod {
	const char *name;
	const char *docString;
	bool direct;
} nn_ComponentMethod;

typedef struct nn_ComponentType nn_ComponentType;

typedef enum nn_ComponentAction {
	// create the local state
	NN_COMP_INIT,
	// delete the local state
	NN_COMP_DEINIT,
	// perform a method call
	NN_COMP_CALL,
	// check if a method is enabled
	NN_COMP_ENABLED,
} nn_ComponentAction;

typedef struct nn_ComponentRequest {
	// the userdata of the component type. This may be an associated VM, for example.
	void *typeUserdata;
	// the userdata of the component, passed in addComponent. This may be an associated resource, for example.
	void *compUserdata;
	// the local state of the component. NN_COMP_INIT should initialize this pointer.
	void *state;
	nn_Computer *computer;
	// address of the component
	const char *compAddress;
	// the action requested
	nn_ComponentAction action;
	// for NN_COMP_CALL, it is the method called.
	// for NN_COMP_ENABLED, it is the method being checked.
	const char *methodCalled;
	union {
		// for NN_COMP_CALL, it is the amount of return values.
		size_t returnCount;
		// for NN_COMP_ENABLED, it is whether the method is enabled.
		bool methodEnabled;
	};
} nn_ComponentRequest;

typedef nn_Exit nn_ComponentHandler(nn_ComponentRequest *req);

// Creates a new component type. It is safe to free name and methods afterwards.
nn_ComponentType *nn_createComponentType(nn_Universe *universe, const char *name, void *userdata, const nn_ComponentMethod methods[], nn_ComponentHandler *handler);
// NOTE: do not destroy this before destroying any components using it, or any computers with components using it.
// The component type is still used one last time for the destructor of the components.
void nn_destroyComponentType(nn_ComponentType *ctype);

// adds a component. Outside of the initialization state (aka after the first tick), it also emits the signal for component added.
// You MUST NOT destroy the component type while a component using that type still exists.
// You can free the address after the call just fine.
nn_Exit nn_addComponent(nn_Computer *computer, nn_ComponentType *ctype, const char *address, int slot, void *userdata);
// Checks if a component of that address exists.
bool nn_hasComponent(nn_Computer *computer, const char *address);
// Checks if the component has that method.
// This not only checks if the method exists in the component type,
// but also checks if the method is enabled for the component instance.
bool nn_hasMethod(nn_Computer *computer, const char *address, const char *method);
// removes a component. Outside of the initialization state (aka after the first tick), it also emits the signal for component removed.
nn_Exit nn_removeComponent(nn_Computer *computer, const char *address);
// Gets the name of a type of a component.
const char *nn_getComponentType(nn_Computer *computer, const char *address);
// Gets the slot of a component.
int nn_getComponentSlot(nn_Computer *computer, const char *address);
// Returns the array of component methods. This can be used for doc strings or just listing methods.
const nn_ComponentMethod *nn_getComponentMethods(nn_Computer *computer, const char *address, size_t *len);

// this uses the call stack.
// Component calls must not call other components, it just doesn't work.
// The lack of an argument count is because the entire call stack is assumed to be the arguments.
nn_Exit nn_call(nn_Computer *computer, const char *address, const char *method);

// call stack operations.
// The type system and API are inspired by Lua, as Lua remains the most popular architecture for OpenComputers.
// This does support other languages, however it may make some APIs clunky due to the usage of tables and 1-based indexing.
// Internally, reference counting is used to manage the memory automatically. The API is designed such that strong reference cycles
// cannot occur.

// returns if there is enough space for [amount] values
bool nn_checkstack(nn_Computer *computer, size_t amount);

// pushes a null on the call stack
nn_Exit nn_pushnull(nn_Computer *computer);
// pushes a boolean on the call stack
nn_Exit nn_pushbool(nn_Computer *computer, bool truthy);
// pushes a number on the call stack
nn_Exit nn_pushnumber(nn_Computer *computer, double num);
// pushes a NULL-terminated string on the call stack. The string is copied, so you can free it afterwards without worry.
nn_Exit nn_pushstring(nn_Computer *computer, const char *str);
// pushes a string on the call stack. The string is copied, so you can free it afterwards without worry. The copy will have a NULL terminator inserted
// at the end for APIs which need it, but the length is also stored.
nn_Exit nn_pushlstring(nn_Computer *computer, const char *str, size_t len);
// pushes a computer userdata to the stack. This is indicative of a resource, such as an HTTP request.
nn_Exit nn_pushuserdata(nn_Computer *computer, size_t userdataIdx);
// pushes a table meant to be an array. [len] is the length of the array. The keys are numbers and 1-indexed, just like in Lua.
// The values are popped, then the array is pushed.
nn_Exit nn_pusharraytable(nn_Computer *computer, size_t len);
// pushes a table. [len] is the amount of pairs. Keys should not be duplicated, as they are not de-duplicated.
// The stack should have a sequence of K1,V1,K2,V2,etc., len pairs long.
// The pairs are popped, then the array is pushed.
nn_Exit nn_pushtable(nn_Computer *computer, size_t len);

// stack management

// pops the top value off the stack
nn_Exit nn_pop(nn_Computer *computer);
// pops the top N values off the stack
nn_Exit nn_popn(nn_Computer *computer, size_t n);
// pushes the top value onto the stack, effectively duplicating the top value.
nn_Exit nn_dupe(nn_Computer *computer);
// pushes the top N values onto the stack, effectively duplicating the top N values.
nn_Exit nn_dupen(nn_Computer *computer, size_t n);

// get the current amount of values on the call stack.
// For component calls, calling this at the start effectively gives you the argument count.
size_t nn_getstacksize(nn_Computer *computer);
// Removes all values from the stack.
// It is recommended to do this when initiating a component call or
// after returning from errors, as the call stack may have
// random junk on it.
void nn_clearstack(nn_Computer *computer);

// type check! The API may misbehave if types do not match, so always type-check!

// Returns whether the value at [idx] is a null.
// [idx] starts at 0.
bool nn_isnull(nn_Computer *computer, size_t idx);
// Returns whether the value at [idx] is a boolean.
// [idx] starts at 0.
bool nn_isboolean(nn_Computer *computer, size_t idx);
// Returns whether the value at [idx] is a number.
// [idx] starts at 0.
bool nn_isnumber(nn_Computer *computer, size_t idx);
// Returns whether the value at [idx] is a string.
// [idx] starts at 0.
bool nn_isstring(nn_Computer *computer, size_t idx);
// Returns whether the value at [idx] is a userdata.
// [idx] starts at 0.
bool nn_isuserdata(nn_Computer *computer, size_t idx);
// Returns whether the value at [idx] is a table.
// [idx] starts at 0.
bool nn_istable(nn_Computer *computer, size_t idx);

// NOTE: behavior of the nn_to*() functions and nn_dumptable() when the values have the wrong types or at out of bounds indexes is undefined.

// Returns the boolean value at [idx].
bool nn_toboolean(nn_Computer *computer, size_t idx);
// Returns the number value at [idx].
double nn_tonumber(nn_Computer *computer, size_t idx);
// Returns the string value at [idx].
const char *nn_tostring(nn_Computer *computer, size_t idx);
// Returns the string value and its length at [idx].
const char *nn_tolstring(nn_Computer *computer, size_t idx, size_t *len);
// Returns the userdata index at [idx].
size_t nn_touserdata(nn_Computer *computer, size_t idx);
// Takes a table value and pushes onto the stack the key-value pairs, as well as writes how many there were in [len].
// It pushes them as K1,V1,K2,V2,K3,V3,etc., just like went in to pushtable. And yes, the keys are not de-duplicated.
nn_Exit nn_dumptable(nn_Computer *computer, size_t idx, size_t *len);

// computes the cost of the top [values] values using the same algorithm as
// the modem.
// It will return -1 if the values are invalid.
int nn_countValueCost(nn_Computer *computer, size_t values);

// Returns the amount of signals stored
size_t nn_countSignals(nn_Computer *computer);
// Pops [valueCount] values from the call stack and pushes them as a signal.
nn_Exit nn_pushSignal(nn_Computer *computer, size_t valueCount);
// Removes the first signal and pushes the values onto the call stack, while setting valueCount to the amount of values in the signal.
// If there is no signal, it returns EBADSTATE
nn_Exit nn_popSignal(nn_Computer *computer, size_t *valueCount);

// The high-level API of the built-in components.
// These components still make no assumptions about the OS, and still require handlers to connect them to the outside work.

// Initializes the component library for a universe. This just defines the component tables and stores them in the universe.
// Using the built-in components without calling this will cause insane levels of undefined behavior and may even cause time travel
// and singularities forming inside your computer.
nn_Exit nn_initComponentsLibrary(nn_Universe *universe);

// TODO: screen, gpu, filesystem, eeprom and the rest of the universe

#ifdef __cplusplus
}
#endif

#endif
