#ifndef NEONUCLEUS_H
#define NEONUCLEUS_H

#ifdef __cplusplus
extern "C" {
#endif

// Platform checking support, to help out users.
// Used internally as well.
// Based off https://stackoverflow.com/questions/5919996/how-to-detect-reliably-mac-os-x-ios-linux-windows-in-c-preprocessor
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
   //define something for Windows (32-bit and 64-bit, this part is common)
	#define NN_WINDOWS
#elif __APPLE__
    #define NN_MACOS
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
// the maximum size a path is allowed to have, including the NULL terminator!
#define NN_MAX_PATH 256
// the maximum amount of bytes which can be read from a file.
// You are given a buffer you are meant to fill at least partially, this is simply the limit of that buffer's size.
#define NN_MAX_READ 65536
// the maximum size of a label
#define NN_MAX_LABEL 256
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
// maximum size of the architecture name EEPROMs can store
#define NN_MAX_ARCHNAME 64
// maximum size of an address.
// This only matters in places where an address is returned through a component, as it is the amount of space to allocate for the response.
// Past this there would be a truncation which would invalidate the address.
// However, 256 is unrealistically long, as UUIDv4 only needs 36.
// Please, do not go above this.
#define NN_MAX_ADDRESS 256
// the port used by tunnel cards. This port is invalid for modems.
#define NN_TUNNEL_PORT 0
// maximum amount of users a computer can have
#define NN_MAX_USERS 64
// maximum length of a username
#define NN_MAX_USERNAME 128

// the maximum size of a UTF-8 character
#define NN_MAXIMUM_UNICODE_BUFFER 4

// the maximum size of a component error message. If the error is bigger than this,
// it is truncated.
#define NN_MAX_ERROR_SIZE 1024

// unicode (UTF-8) support library

typedef unsigned int nn_codepoint;

bool nn_unicode_validate(const char *s, size_t len);
// validates only the *first* codepoint in the NULL-terminated string.
// This returns the length in bytes of the codepoint, with 0 meaning
// invalid.
size_t nn_unicode_validateFirstChar(const char *s, size_t len);

// returns the amount of unicode codepoints in the UTF-8 string.
// Undefined behavior for invalid UTF-8, make sure to validate it if needed.
size_t nn_unicode_len(const char *s, size_t len);
// returns the amount of unicode codepoints in the UTF-8 string.
// If s is invalid UTF-8, all invalid bytes are considered a 1-byte codepoint.
size_t nn_unicode_lenPermissive(const char *s, size_t len);

// Writes the codepoints of s into codepoints.
// Undefined behavior for invalid UTF-8, make sure to validate it if needed.
// The codepoints buffer must be big enough to store the string, use nn_unicode_len()
// to get the required buffer length.
void nn_unicode_codepoints(const char *s, size_t len, nn_codepoint *codepoints);
// Writes the codepoints of s into codepoints.
// If s is invalid UTF-8, all invalid bytes are considered a 1-byte codepoint.
// The codepoints buffer must be big enough to store the string, use nn_unicode_lenPermissive()
// to get the required buffer length.
void nn_unicode_codepointsPermissive(const char *s, size_t len, nn_codepoint *codepoints);

// Returns the first codepoint from a UTF-8 string.
// If s is invalid UTF-8, the behavior is undefined.
nn_codepoint nn_unicode_firstCodepoint(const char *s);
// Returns the size, in bytes, required by UTF-8 for a codepoint.
size_t nn_unicode_codepointSize(nn_codepoint codepoint);
// Writes the UTF-8 bytes for a given codepoint into buffer.
// It does NOT write a NULL terminator, but it does return the length.
size_t nn_unicode_codepointToChar(char buffer[NN_MAXIMUM_UNICODE_BUFFER], nn_codepoint codepoint);
// the width, on a screen, for a codepoint.
// This matters for emojies.
size_t nn_unicode_charWidth(nn_codepoint codepoint);
// The width, on a screen, for an entire string.
// The behavior is undefined for 
size_t nn_unicode_wlen(const char *s, size_t len);
size_t nn_unicode_wlenPermissive(const char *s, size_t len);

// Returns the amount of bytes needed to store the UTF-8 encoded text.
// The behavior on invalid codepoints is undefined.
size_t nn_unicode_countBytes(nn_codepoint *codepoints, size_t len);
// Writes the UTF-8 encoded text.
// DOES NOT WRITE A NULL TERMINATOR.
// s must be big enough to store the string, use nn_unicode_bytelen()
// to allocate the correct amount of space.
// The behavior on invalid codepoints is undefined.
void nn_unicode_writeBytes(char *s, nn_codepoint *codepoints, size_t len);

// Returns the uppercase version of the codepoint
nn_codepoint nn_unicode_upper(nn_codepoint codepoint);
// Returns the lowercase version of the codepoint
nn_codepoint nn_unicode_lower(nn_codepoint codepoint);

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

// Intended for a plain mutex.
// This is used for synchronization. OpenComputers achieves synchronization
// between the worker threads by sending them as requests to a central thread (indirect methods).
// In NeoNucleus, the function pointer is invoked on the calling thead. This technically makes all methods direct,
// however methods which are meant to be slow may become indirect, as indirect methods consume the entire call budget.
// Do note that locks are only used in "full" component implementations, such as the volatile storage devices.
// The interfaces do not do any automatic synchronization via locks, all synchronization is assumed
// to be handled in the implementer of the interface, because only you know how to best synchronize
// it with the outside world.
typedef void nn_LockProc(void *state, nn_LockRequest *req);

// The *context* NeoNucleus is operating in.
// This determines:
// - How memory is allocated
// - How random numbers are generated and what the range is
// - What the current time is
// - How locks work
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
nn_Universe *nn_getComputerUniverse(nn_Computer *computer);
nn_Context *nn_getUniverseContext(nn_Universe *universe);
nn_Context *nn_getComputerContext(nn_Computer *computer);

// address is copied.
// It can be NULL if you wish to have no tmp address.
// It can fail due to out-of-memory errors.
nn_Exit nn_setTmpAddress(nn_Computer *computer, const char *address);
// can return NULL if none was set
const char *nn_getTmpAddress(nn_Computer *computer);

// Registers a user to the computer.
nn_Exit nn_addUser(nn_Computer *computer, const char *user);
// Unregisters a user from the computer.
// If they were never there, nothing is removed and all is fine.
// It returns if the user was originally there.
bool nn_removeUser(nn_Computer *computer, const char *user);
// NULL for out-of-bound users
// Can be used to iterate all users.
const char *nn_getUser(nn_Computer *computer, size_t idx);
// Helper function.
// Always returns true if 0 users are registered.
// If users are registered, it will only return true if the specified
// user is registered.
// This can be used for checking signals.
bool nn_hasUser(nn_Computer *computer, const char *user);

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
const nn_Architecture *nn_getSupportedArchitectures(nn_Computer *computer, size_t *len);
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
// set a default error message from an exit.
// Does nothing for EBADCALL.
void nn_setErrorFromExit(nn_Computer *computer, nn_Exit exit);
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
// This automatically resets the component budgets and call budget.
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

typedef enum nn_MethodFlags {
	// calling will consume the entire call budget
	NN_INDIRECT = 0,
	// calling will only consume 1 call from the call budget
	NN_DIRECT = (1<<0),
	// this indicates this method wraps a *field*
	// getter means calling it with no arguments will return the current value,
	NN_GETTER = (1<<1),
	// this indicates this method wraps a *field*
	// setter means calling it with 1 argument will try to set the value.
	NN_SETTER = (1<<2),
} nn_MethodFlags;

#define NN_FIELD_MASK (NN_GETTER | NN_SETTER)

typedef struct nn_Method {
	const char *name;
	const char *docString;
	nn_MethodFlags flags;
} nn_Method;

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
	// delete the type userdata
	NN_COMP_FREETYPE,
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
nn_ComponentType *nn_createComponentType(nn_Universe *universe, const char *name, void *userdata, const nn_Method methods[], nn_ComponentHandler *handler);
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
const nn_Method *nn_getComponentMethods(nn_Computer *computer, const char *address, size_t *len);
// get the address at a certain index.
// It'll return NULL for out of bounds indexes.
// This can be used to iterate over all components.
const char *nn_getComponentAddress(nn_Computer *computer, size_t idx);
// Returns the doc-string associated with a method.
const char *nn_getComponentDoc(nn_Computer *computer, const char *address, const char *method);
void *nn_getComponentUserdata(nn_Computer *computer, const char *address);

// this uses the call stack.
// Component calls must not call other components, it just doesn't work.
// The lack of an argument count is because the entire call stack is assumed to be the arguments.
// In the case of NN_EBUSY, you should call it again with the same arguments later.
nn_Exit nn_call(nn_Computer *computer, const char *address, const char *method);

// Sets the call budget.
// The default is 1,000.
void nn_setCallBudget(nn_Computer *computer, size_t budget);

// gets the total call budget
size_t nn_getCallBudget(nn_Computer *computer);

// returns the remaining call budget
size_t nn_callBudgetRemaining(nn_Computer *computer);

// automatically called by nn_tick()
void nn_resetCallBudget(nn_Computer *computer);

// returns whether there is no more call budget left.
// At this point, the architecture should exit with a yield.
bool nn_componentsOverused(nn_Computer *computer);

void nn_resetComponentBudgets(nn_Computer *computer);

// Uses 1/perTick to the component budget.
// Upon a full component budget being used for that component, it returns true.
// nn_componentsOverused() will also return true.
// This indicates the architecture should yield, to throttle the computer for overuse.
bool nn_costComponent(nn_Computer *computer, const char *address, double perTick);
// Uses amount/perTick to the component budget.
// Upon a full component budget being used for that component, it returns true.
// nn_componentsOverused() will also return true.
// This indicates the architecture should yield, to throttle the computer for overuse.
bool nn_costComponentN(nn_Computer *computer, const char *address, double amount, double perTick);

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
// casts [num] to a double and pushes it on the call stack
nn_Exit nn_pushinteger(nn_Computer *computer, intptr_t num);
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

// pushes the value at idx.
nn_Exit nn_dupeat(nn_Computer *computer, size_t idx);

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
// Returns whether the value at [idx] is a number AND
// the number can safely be cast to an intptr_t.
bool nn_isinteger(nn_Computer *computer, size_t idx);
// Returns whether the value at [idx] is a string.
// [idx] starts at 0.
bool nn_isstring(nn_Computer *computer, size_t idx);
// Returns whether the value at [idx] is a userdata.
// [idx] starts at 0.
bool nn_isuserdata(nn_Computer *computer, size_t idx);
// Returns whether the value at [idx] is a table.
// [idx] starts at 0.
bool nn_istable(nn_Computer *computer, size_t idx);
// Returns the name of the type of the value at that index.
// For out of bounds indexes, "none" is returned.
const char *nn_typenameof(nn_Computer *computer, size_t idx);

// Argument helpers

// Returns true if the argument at that index is not null.
bool nn_checknull(nn_Computer *computer, size_t idx, const char *errMsg);
// Returns true if the argument at that index is not a boolean.
bool nn_checkboolean(nn_Computer *computer, size_t idx, const char *errMsg);
// Returns true if the argument at that index is not a number.
bool nn_checknumber(nn_Computer *computer, size_t idx, const char *errMsg);
// Returns true if the argument at that index is not an integer.
bool nn_checkinteger(nn_Computer *computer, size_t idx, const char *errMsg);
// Returns true if the argument at that index is not a string.
bool nn_checkstring(nn_Computer *computer, size_t idx, const char *errMsg);
// Returns true if the argument at that index is not userdata.
bool nn_checkuserdata(nn_Computer *computer, size_t idx, const char *errMsg);
// Returns true if the argument at that index is a table.
bool nn_checktable(nn_Computer *computer, size_t idx, const char *errMsg);

// Checks if idx is equal to the stack size.
// If it is, it will push a null.
nn_Exit nn_defaultnull(nn_Computer *computer, size_t idx);
// Checks if idx is equal to the stack size.
// If it is, it will push a boolean [value].
nn_Exit nn_defaultboolean(nn_Computer *computer, size_t idx, bool value);
// Checks if idx is equal to the stack size.
// If it is, it will push a number [num].
nn_Exit nn_defaultnumber(nn_Computer *computer, size_t idx, double num);
// Checks if idx is equal to the stack size.
// If it is, it will push an integer [num].
nn_Exit nn_defaultinteger(nn_Computer *computer, size_t idx, intptr_t num);
// Checks if idx is equal to the stack size.
// If it is, it will push a string [str].
nn_Exit nn_defaultstring(nn_Computer *computer, size_t idx, const char *str);
// Checks if idx is equal to the stack size.
// If it is, it will push a string [str].
nn_Exit nn_defaultlstring(nn_Computer *computer, size_t idx, const char *str, size_t len);
// Checks if idx is equal to the stack size.
// If it is, it will push the userdata [userdataIdx].
nn_Exit nn_defaultuserdata(nn_Computer *computer, size_t idx, size_t userdataIdx);
// Checks if idx is equal to the stack size.
// If it is, it will push an empty table.
nn_Exit nn_defaulttable(nn_Computer *computer, size_t idx);

// NOTE: behavior of the nn_to*() functions and nn_dumptable() when the values have the wrong types or at out of bounds indexes is undefined.

// Returns the boolean value at [idx].
bool nn_toboolean(nn_Computer *computer, size_t idx);
// Returns the number value at [idx].
double nn_tonumber(nn_Computer *computer, size_t idx);
// Returns the number value at [idx] cast to an intptr_t.
// NOTE: for numbers where nn_isinteger() returns false,
// the cast is undefined.
// This includes values such as infinity and NaN, where 
// the behavior is platform, ABI and compiler-specific.
intptr_t nn_tointeger(nn_Computer *computer, size_t idx);
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
// The algorithm is as mentioned in https://ocdoc.cil.li/component:modem
// and is as follows:
// - Every value adds a 2 byte overhead
// - Numbers add another 8 bytes, true/false/null another 4 bytes, strings as
// many bytes as they contain, except empty strings count as 1 byte.
int nn_countValueCost(nn_Computer *computer, size_t values);

// computes the signal cost.
// This is a slightly modified version of value cost, except it allows
// tables and userdata.
// All values are always valid.
// For userdata and tables:
// - Userdata adds another 8 bytes overhead like numbers do.
// - Tables add yet another 2 byte overhead for their terminator, and the sum of all of the size of the keys and values they contain as per this algorithm.
size_t nn_countSignalCost(nn_Computer *computer, size_t values);

// Returns the amount of signals stored
size_t nn_countSignals(nn_Computer *computer);
// Pops [valueCount] values from the call stack and pushes them as a signal.
nn_Exit nn_pushSignal(nn_Computer *computer, size_t valueCount);
// Removes the first signal and pushes the values onto the call stack, while setting valueCount to the amount of values in the signal.
// If there is no signal, it returns EBADSTATE
nn_Exit nn_popSignal(nn_Computer *computer, size_t *valueCount);

// The high-level API of the built-in components.
// These components still make no assumptions about the OS, and still require handlers to connect them to the outside work.

// TODO: screen, gpu, filesystem, eeprom and the rest of the universe

typedef enum nn_EEPROMAction {
	// the eeprom instance has been dropped
	NN_EEPROM_DROP,
	NN_EEPROM_GET,
	NN_EEPROM_SET,
	NN_EEPROM_GETDATA,
	NN_EEPROM_SETDATA,
	NN_EEPROM_GETLABEL,
	NN_EEPROM_SETLABEL,
	NN_EEPROM_GETARCH,
	NN_EEPROM_SETARCH,
	NN_EEPROM_ISREADONLY,
	NN_EEPROM_MAKEREADONLY,
} nn_EEPROMAction;

typedef struct nn_EEPROMRequest {
	// associated userdata
	void *userdata;
	// associated component userdata
	void *instance;
	// the computer making the request
	nn_Computer *computer;
	const struct nn_EEPROM *eepromConf;
	nn_EEPROMAction action;
	// all the get* options should set this to the length,
	// and its initial value is the capacity of [buf].
	// For ISREADONLY, this should be set to 0 if false and 1 if true.
	unsigned int buflen;
	// this may be the buffer length
	char *buf;
} nn_EEPROMRequest;

// reads and writes are always 1/1
typedef struct nn_EEPROM {
	// the maximum capacity of the EEPROM
	size_t size;
	// the maximum capacity of the EEPROM's associated data
	size_t dataSize;
	// the energy cost of reading an EEPROM
	double readEnergyCost;
	// the energy cost of reading an EEPROM's associated data
	double readDataEnergyCost;
	// the energy cost of writing to an EEPROM
	double writeEnergyCost;
	// the energy cost of writing to an EEPROM's associated data
	double writeDataEnergyCost;
} nn_EEPROM;

extern nn_EEPROM nn_defaultEEPROM;

typedef struct nn_VEEPROM {
	const char *code;
	size_t codelen;
	const char *data;
	size_t datalen;
	const char *label;
	size_t labellen;
	const char *arch;
	bool isReadonly;
} nn_VEEPROM;

typedef nn_Exit nn_EEPROMHandler(nn_EEPROMRequest *request);

// the userdata passed to the component is the userdata
// in the handler
nn_ComponentType *nn_createEEPROM(nn_Universe *universe, const nn_EEPROM *eeprom, nn_EEPROMHandler *handler, void *userdata);
nn_ComponentType *nn_createVEEPROM(nn_Universe *universe, const nn_EEPROM *eeprom, const nn_VEEPROM *vmem);

// Note on paths:
// - Paths given always have their length stored, but also have a NULL terminator.
// - Paths are validated. They check for illegal characters as per OC's definition.
// - Logical paradoxes such as rename("a", "a/b") are automatically checked and handled.
// - \ are automatically replaced with /
// - .. and leading / is handled automatically. This also improves sandboxing, as ../a.txt would become just a.txt
// - For rename, it automatically checks if the destination exists and if so, errors out.
typedef enum nn_FilesystemAction {
	// the filesystem instance has been dropped.
	// Make sure to close all file descriptors which are still open.
	NN_FS_DROP,
	// open a file. strarg1 stores the path, and strarg2 stores the mode.
	// strarg1len and strarg2len are their respective lengths.
	// The output should be in fd.
	NN_FS_OPEN,
	// read a file.
	// The file descriptor is stored in fd,
	// make sure to ensure it is valid.
	// strarg1len is the capacity of strarg1.
	// Write the result of reading into strarg1.
	// Update strarg1len to reflect the amount of data read.
	// Set strarg1 to NULL to indicate EOF.
	NN_FS_READ,
	// write to a file.
	// The file descriptor is stored in fd,
	// make sure to ensure it is valid.
	// strarg1len is the amount of data to write.
	// strarg1 is the contents of the buffer to write.
	NN_FS_WRITE,
	// seek a file.
	// The file descriptor is stored in fd,
	// make sure to ensure it is valid.
	// The offset is stored in off.
	// The seek mode is stored in whence.
	// It should set off to the new position.
	NN_FS_SEEK,
	// close a file.
	// The file descriptor is stored in fd,
	// make sure to ensure it is valid.
	NN_FS_CLOSE,
	// open a directory file descriptor.
	// The result should be in fd.
	NN_FS_OPENDIR,
	// read a directory file descriptor, stored in fd.
	// The entry should be stored in strarg2, and strarg2len is the capacity of the buffer.
	// If the buffer is too short, truncate the result.
	// Set strarg2len to the length of the entry.
	// If there are no more entries, set strarg2 to NULL.
	// Do note that directories should have / appended at the end of their entries.
	// Directory file descriptors are not exposed to the architecture,
	// thus they can only come from NN_FS_OPENDIR.
	// This means you may not need to validate these file descriptors.
	NN_FS_READDIR,
	// close a directory file descriptor, stored in fd.
	// Directory file descriptors are not exposed to the architecture,
	// thus they can only come from NN_FS_OPENDIR.
	// This means you may not need to validate these file descriptors.
	NN_FS_CLOSEDIR,
	// Create a directory at a given path stored in strarg1.
	// strarg1len is the length of the path.
	// It is meant to also create parent directories recursively
	// as needed.
	NN_FS_MKDIR,
	// Return the lastmodified timestamp.
	// This number is stored in seconds.
	// The timestamp should be stored in size, it may not make
	// sense but it is a field and it is there.
	// Do note that the lastModified() method returns it in milliseconds,
	// however it must be a multiple of 1000 due to OpenOS depending
	// on that behavior.
	NN_FS_LASTMODIFIED,
	// Checks if a path, stored in strarg1, is a directory.
	// If it is, size should be set to 1.
	// If it is not, size should be set to 0.
	NN_FS_ISDIRECTORY,
	// Checks if the filesystem is read-only.
	// If it is, size should be set to 1.
	// If it is not, size should be set to 0.
	NN_FS_ISREADONLY,
	// Checks if a path, stored in strarg1, exists on the filesystem.
	// If it is, size should be set to 1.
	// If it is not, size should be set to 0.
	NN_FS_EXISTS,
	// Returns the label.
	// The label should be written into strarg1, with strarg1len as the capacity.
	// Set strarg1len to the label length.
	NN_FS_GETLABEL,
	// Sets the label.
	// The label is stored in strarg1, with strarg1len as the length.
	NN_FS_SETLABEL,
	// Gets the space used, it should be stored in size.
	NN_FS_SPACEUSED,
	// Gets 2 paths, strarg1 and strarg2, with their lengths.
	// It should try to rename strarg1 to strarg2, as in,
	// it should move strarg1 to be at strarg2, potentially
	// using recursive directory copies.
	NN_FS_RENAME,
	// Removes the path stored in strarg1.
	NN_FS_REMOVE,
	// Returns the size of the entry at strarg1.
	// The size of a directory is typically 0.
	// The size of a file is typically the amount of bytes in its contents.
	// Using other measures of size will rarely break code,
	// but may confuse users.
	NN_FS_SIZE,
} nn_FilesystemAction;

typedef enum nn_FilesystemWhence {
	// relative to start
	NN_SEEK_SET,
	// relative to the current position
	NN_SEEK_CUR,
	// relative to the EOF position.
	NN_SEEK_END,
} nn_FilesystemWhence;

typedef struct nn_FilesystemRequest {
	void *userdata;
	void *instance;
	nn_Computer *computer;
	struct nn_Filesystem *fsConf;
	nn_FilesystemAction action;
	int fd;
	nn_FilesystemWhence whence;
	int off;
	char *strarg1;
	size_t strarg1len;
	char *strarg2;
	size_t strarg2len;
	size_t size;
} nn_FilesystemRequest;

typedef struct nn_Filesystem {
	// the maximum capacity of the filesystem
	size_t spaceTotal;
	// how many read calls can be done per tick
	// list, exists, isDirectory, seek also count as reads.
	double readsPerTick;
	// how many write calls can be done per tick
	// makeDirectory, open, remove and rename also count as writes.
	double writesPerTick;
	// The energy cost of an actual read/write.
	// It is per-byte, so if a read returns 4096 bytes, then this cost is multiplied by 4096.
	double dataEnergyCost;
} nn_Filesystem;

// 4 Tiers.
// 0 - Tier 1 equivalent
// 1 - Tier 2 equivalent
// 2 - Tier 3 equivalent
// 3 - Tier 4, a better version of Tier 3.
extern nn_Filesystem nn_defaultFilesystems[4];
// a basic floppy
extern nn_Filesystem nn_defaultFloppy;

typedef nn_Exit nn_FilesystemHandler(nn_FilesystemRequest *request);

nn_ComponentType *nn_createFilesystem(nn_Universe *universe, const nn_Filesystem *filesystem, nn_FilesystemHandler *handler, void *userdata);

typedef enum nn_ScreenAction {
	// instance dropped
	NN_SCR_DROP,

	// set w to 1 if it is on, or 0 if it is off.
	NN_SCR_ISON,
	// attempt to turn the screen on.
	// set w to 1 if it was on, or 0 if it was off.
	// set h to 1 if it is now on, or 0 if it is now off.
	NN_SCR_TURNON,
	// attempt to turn the screen off.
	// set w to 1 if it was on, or 0 if it was off.
	// set h to 1 if it is now on, or 0 if it is now off.
	NN_SCR_TURNOFF,
	// get a keyboard. The index requested is stored in h.
	// If the index is out of bounds, set keyboard to NULL.
	// Else, write the keyboard address into the buffer in keyboard.
	// The capacity of the buffer is stored in w.
	NN_SCR_GETKEYBOARD,
	// change the screen to/from precise mode.
	// Precise mode means mouse events will have real-number coordinates, as opposed to integer-based ones.
	// NeoNucleus does not automatically round this, you are meant to round it.
	// The new precision value is stored in w, where it is a 1 to enable it and 0 to disable it.
	// Set w to 1 if precise mode is now enabled, or 0 if it isn't.
	NN_SCR_SETPRECISE,
	// Set w to 1 if precise mode is enabled, or 0 if it isn't.
	NN_SCR_ISPRECISE,
	// change the screen to/from inverted touch mode.
	// Inverted touch mode normally provides an alternative way to interact with the touchscreen.
	// For example, in OC, it makes the GUI only open with shift+rightclick, and normal rightclick
	// triggers a touch event instead. It is best to give it an equivalent meaning to OC's to prevent
	// unexpected program behavior.
	// The new inverted touch mode state is stored in w, where it is a 1 to enable it and 0 to disable it.
	// Set w to 1 if inverted touch mode is now enabled, or 0 if it isn't.
	NN_SCR_SETTOUCHINVERTED,
	// Set w to 1 if inverted touch mode is enabled, or 0 if it isn't.
	NN_SCR_ISTOUCHINVERTED,
	// Gets the aspect ratio (amount of screen blocks joined together).
	// Outside of MC, this may not make much sense, in which case you can just set it to 1x1.
	// Store the width in w and the height in h.
	NN_SCR_GETASPECTRATIO,
} nn_ScreenAction;

typedef struct nn_ScreenRequest {
	void *userdata;
	void *instance;
	nn_Computer *computer;
	nn_ScreenAction action;
	int w;
	int h;
	char *keyboard;
} nn_ScreenRequest;

typedef enum nn_ScreenFeatures {
	NN_SCRF_NONE = 0,
	// whether it supports mouse input.
	// If it doesn't, it should not emit
	// touch, drag or other mouse events.
	// Walk events should also not be emitted.
	NN_SCRF_MOUSE = 1<<0,
	// Whether precise mode is supported.
	NN_SCRF_PRECISE = 1<<1,
	// Whether touch inverted is supported.
	NN_SCRF_TOUCHINVERTED = 1<<2,
	// it indicates that the palette can be edited.
	NN_SCRF_EDITABLECOLORS = 1<<3,
} nn_ScreenFeatures;

// A struct for the reference screen configurations
// This does not influence the interface at all,
// however it exists as a runtime reference of what
// the conventional screen tiers are.
typedef struct nn_ScreenConfig {
	int maxWidth;
	int maxHeight;
	nn_ScreenFeatures features;
	int paletteColors;
	char maxDepth;
} nn_ScreenConfig;

// OC has 3 tiers, NN adds a 4th one as well.
extern nn_ScreenConfig nn_defaultScreens[4];

typedef nn_Exit nn_ScreenHandler(nn_ScreenRequest *req);

nn_ComponentType *nn_createScreen(nn_Universe *universe, nn_ScreenHandler *handler, void *userdata);
// a useless component which does nothing
nn_ComponentType *nn_createKeyboard(nn_Universe *universe);

// Remember:
// - Colors are in 0xRRGGBB format.
// - Screen coordinates and palettes are 1-indexed.
// - If NN_GPU_SETRESOLUTION returns NN_OK, a screen_resized signal is queued automatically.
// - VRAM is always fast
typedef enum nn_GPUAction {
	// instance dropped
	NN_GPU_DROP,

	// Conventional GPU functions

	// requests to bind to a screen connected to the computer.
	// The address is stored in text, with the length in width.
	// The interface does check that the computer does have the screen, but do look out
	// for time-of-check/time-of-use issues which may occur in multi-threaded environments.
	// If x is set to 1, the reset flag is enabled. This means the GPU should "reset" the state
	// of the screen.
	NN_GPU_BIND,
	// requests to unbind the GPU from its screen.
	// If there is no screen, it just does nothing.
	NN_GPU_UNBIND,
	// Ask for the screen the GPU is currently bound to.
	// If it is not bound to any, text should be set to NULL.
	// If it is, you must write to text the address of the screen.
	// width stores the capacity of text, so if needed, truncate it to that many bytes.
	// The length of this address must be stored in width.
	NN_GPU_GETSCREEN,
	// Gets the current background.
	// x should store either the color in 0xRRGGBB format or the palette index.
	// y should be 1 if x is a palette index and 0 if it is a color.
	NN_GPU_GETBACKGROUND,
	// Sets the current background.
	// x should store either the color in 0xRRGGBB format or the palette index.
	// y should be 1 if x is a palette index and 0 if it is a color.
	// The values x and y should be updated to reflect the old state.
	NN_GPU_SETBACKGROUND,
	// Gets the current foreground.
	// x should store either the color in 0xRRGGBB format or the palette index.
	// y should be 1 if x is a palette index and 0 if it is a color.
	NN_GPU_GETFOREGROUND,
	// Sets the current foreground.
	// x should store either the color in 0xRRGGBB format or the palette index.
	// y should be 1 if x is a palette index and 0 if it is a color.
	// The values x and y should be updated to reflect the old state.
	NN_GPU_SETFOREGROUND,
	// Gets the palette color.
	// x is the index.
	// y should be set to the color.
	NN_GPU_GETPALETTECOLOR,
	// Gets the palette color.
	// x is the index.
	// y is the color.
	NN_GPU_SETPALETTECOLOR,
	// Gets the maximum depth supported by the GPU and screen.
	// Valid depth values in OC are 1, 4 and 8, however NN also recognizes 2, 3, 16 and 24.
	// The result should be stored in x.
	NN_GPU_MAXDEPTH,
	// Gets the current depth the screen is displaying at.
	// The result should be stored in x.
	NN_GPU_GETDEPTH,
	// Sets the current depth the screen is displaying at.
	// The new depth is in x.
	// This should not change the stored color values of neither the palette nor the characters,
	// but simply change what their color is translated to graphically.
	// The old depth should be stored in x.
	NN_GPU_SETDEPTH,
	// Gets the maximum resolution supported by the GPU and screen.
	// Result should be in width and height.
	NN_GPU_MAXRESOLUTION,
	// Gets the resolution of the screen.
	// Result should be in width and height.
	NN_GPU_GETRESOLUTION,
	// Sets the resolution of the screen.
	// The new resolution should be stored in width and height.
	// If successful, a screen_resized event is implicitly queued.
	NN_GPU_SETRESOLUTION,
	// Gets the current screen viewport.
	// The result should be in width and height.
	NN_GPU_GETVIEWPORT,
	// Sets the screen viewport.
	// The new viewport dimensions are stored in width and height.
	NN_GPU_SETVIEWPORT,
	// Gets a character.
	// The position requested is given in x and y.
	// The codepoint of the character should be set in [codepoint].
	// The foreground and background color should be set in [width] and [height].
	// The palette indexes of the foreground and background should be set
	// in [dest] and [src] respectively. If the pixel color was not from
	// the palette, the imaginary -1 palette index can be used.
	NN_GPU_GET,
	// Sets a horizontal line of text at a given x, y.
	// The position is stored in x, y, and is the position of the first character.
	// The text goes left-to-right on the horizontal line. Anything off-screen is discared.
	// There is no wrapping.
	// The text is stored in text, with the size of the text, in bytes, being stored in width.
	NN_GPU_SET,
	// like NN_GPU_SET, but the text is set vertically.
	// This means instead of going from left-to-right on the screen on a horizontal line,
	// it is up-to-down on a vertical line.
	NN_GPU_SETVERTICAL,
	// Copies a portion of the screen to another location.
	// The rectangle being copied is width x height, and has the top-left corner at x, y.
	// The destination rectangle is also width x height, but has the top-left corner at x + tx, y + ty.
	// The copy happens as if it is using an intermediary buffer, thus even if the source and destination
	// intersect, the order in which characters are copied must not change the result.
	NN_GPU_COPY,
	// Fills a rectangle
	// The rectangle's top-left corner is at x, y, and its dimensions are width x height.
	// The character it should be filled with has its unicode codepoint stored in codepoint.
	NN_GPU_FILL,

	// VRAM buffers (always blazing fast)
	
	// Should return the current active buffer.
	// 0 for the screen, or if there is no screen.
	// The result should be stored in x.
	NN_GPU_GETACTIVEBUFFER,
	// Switches the active buffer to a new one, stored in x.
	NN_GPU_SETACTIVEBUFFER,
	// Gets a buffer by index in an imaginary list containing all of them.
	// The index is in x, the buffer is output in y.
	// If y is 0, the sequence is assumed to end.
	NN_GPU_BUFFERS,
	// Allocates a buffer.
	// The buffer sizes are in width and height, with 0 x 0 meaning max resolution (default).
	// This consumes exactly width * height VRAM.
	// The new buffer should be put in x.
	// If there was not enough VRAM for this, x can be set to 0.
	NN_GPU_ALLOCBUFFER,
	// Frees a buffer.
	// The buffer is stored in x.
	// This releases the same VRAM that the buffer consumed when allocated.
	NN_GPU_FREEBUFFER,
	// Frees all buffers. The free VRAM should be equal to the total VRAM after this.
	NN_GPU_FREEBUFFERS,
	// Gets memory info about the GPU.
	// x should be set to the amount of free VRAM available.
	NN_GPU_FREEMEM,
	// Gets the size of a buffer, stored in x.
	// The size should be stored in width and height.
	NN_GPU_GETBUFFERSIZE,
	// Copy a region between buffers or between the screen and buffers.
	// The destination buffer is stored in dest. If 0, it refers to the screen.
	// The source buffer is stored in src. If 0, it refers to the screen.
	// x, y, width and height define the source rectangle, in the same way as in fill, to copy from the source buffer.
	// tx, ty refer to the top-left corner for the destination rectangle, in the destination buffer. It has the same width
	// and height as the source rectangle.
	// Screen-to-screen copies are illegal and checked, no need to worry about handling them.
	NN_GPU_BITBLT,
} nn_GPUAction;

typedef struct nn_GPURequest {
	void *userdata;
	void *instance;
	nn_Computer *computer;
	struct nn_GPU *gpuConf;
	nn_GPUAction action;
	int x;
	int y;
	int width;
	int height;
	union {
		struct {
			int tx;
			int ty;
		};
		nn_codepoint codepoint;
		char *text;
	};
	int dest;
	int src;
} nn_GPURequest;

typedef struct nn_GPU {
	// the minimum between these and the screen's
	// are the maximum width/height/depth supported.
	int maxWidth;
	int maxHeight;
	char maxDepth;
	// this is in pixels.
	size_t totalVRAM;
	// amount of times copy can be called before running out of budget.
	int copyPerTick;
	// amount of times fill can be called before running out of budget.
	int fillPerTick;
	// amount of times set can be called before running out of budget.
	int setPerTick;
	// amount of times setForeground can be called before running out of budget.
	int setForegroundPerTick;
	// amount of times setBackground can be called before running out of budget.
	int setBackgroundPerTick;
	// energy per non-space set.
	double energyPerWrite;
	// energy per space set.
	double energyPerClear;
} nn_GPU;

typedef nn_Exit nn_GPUHandler(nn_GPURequest *req);

// 1 GPU tier for every screen.
extern nn_GPU nn_defaultGPUs[4];

nn_ComponentType *nn_createGPU(nn_Universe *universe, const nn_GPU *gpu, nn_GPUHandler *handler, void *userdata);

// Colors and palettes.
// Do note that the 

// The NeoNucleus 2-bit palette
extern int nn_palette2[4];

// The NeoNucleus 3-bit palette
extern int nn_palette3[8];

// The OC 4-bit palette.
extern int nn_ocpalette4[16];

// The Minecraft 4-bit palette, using dye colors.
extern int nn_mcpalette4[16];

// The OC 8-bit palette.
extern int nn_ocpalette8[256];

// initializes the contents of the palettes.
void nn_initPalettes();

// Expensive.
// Maps a color to the closest match in a palette.
int nn_mapColor(int color, int *palette, size_t len);
// Expensive.
// Maps a color within a given depth.
// ocCompatible only matters for 4-bit, and determines whether to use the OC palette or the MC palette.
// Invalid depths behave identically to 24-bit, in which case the color is left unchanged.
int nn_mapDepth(int color, int depth, bool ocCompatible);

// the name of a depth, if valid.
// If invalid, NULL is returned, thus this can be used to check
// if a depth is valid as well.
// Valid depths are 1, 2, 3, 4, 8, 16 and 24.
const char *nn_depthName(int depth);

// Signal helpers

// common mouse buttons, not an exhaustive list
#define NN_BUTTON_LEFT 0
#define NN_BUTTON_RIGHT 1
#define NN_BUTTON_MIDDLE 2

// OC keycodes
// taken from https://github.com/MightyPirates/OpenComputers/blob/52da41b5e171b43fea80342dc75d808f97a0f797/src/main/resources/assets/opencomputers/loot/openos/lib/core/full_keyboard.lua
#define NN_KEY_UNKNOWN 0
#define NN_KEY_1 0x02
#define NN_KEY_2 0x03
#define NN_KEY_3 0x04
#define NN_KEY_4 0x05
#define NN_KEY_5 0x06
#define NN_KEY_6 0x07
#define NN_KEY_7 0x08
#define NN_KEY_8 0x09
#define NN_KEY_9 0x0A
#define NN_KEY_0 0x0B
#define NN_KEY_A 0x1E
#define NN_KEY_B 0x30
#define NN_KEY_C 0x2E
#define NN_KEY_D 0x20
#define NN_KEY_E 0x12
#define NN_KEY_F 0x21
#define NN_KEY_G 0x22
#define NN_KEY_H 0x23
#define NN_KEY_I 0x17
#define NN_KEY_J 0x24
#define NN_KEY_K 0x25
#define NN_KEY_L 0x26
#define NN_KEY_M 0x32
#define NN_KEY_N 0x31
#define NN_KEY_O 0x18
#define NN_KEY_P 0x19
#define NN_KEY_Q 0x10
#define NN_KEY_R 0x13
#define NN_KEY_S 0x1F
#define NN_KEY_T 0x14
#define NN_KEY_U 0x16
#define NN_KEY_V 0x2F
#define NN_KEY_W 0x11
#define NN_KEY_X 0x2D
#define NN_KEY_Y 0x15
#define NN_KEY_Z 0x2C

#define NN_KEY_APOSTROPHE 0x28
#define NN_KEY_AT 0x91
#define NN_KEY_BACK 0x0E
#define NN_KEY_BACKSLASH 0x2B
// caps-lock
#define NN_KEY_CAPITAL 0x3A
#define NN_KEY_COLON 0x92
#define NN_KEY_COMMA 0x33
#define NN_KEY_ENTER 0x1C
#define NN_KEY_EQUALS 0x0D
// accent grave
#define NN_KEY_GRAVE 0x29
#define NN_KEY_LBRACKET 0x1A
#define NN_KEY_LCONTROL 0x1D
// left alt
#define NN_KEY_LMENU 0x38
#define NN_KEY_LSHIFT 0x2A
#define NN_KEY_MINUS 0x0C
#define NN_KEY_NUMLOCK 0x45
#define NN_KEY_PAUSE 0xC5
#define NN_KEY_PERIOD 0x34
#define NN_KEY_RBRACKET 0x1B
#define NN_KEY_RCONTROL 0x9D
// right alt
#define NN_KEY_RMENU 0xB8
#define NN_KEY_RSHIFT 0x36
// scroll lock
#define NN_KEY_SCROLL 0x46
#define NN_KEY_SEMICOLON 0x27
#define NN_KEY_SLASH 0x35
#define NN_KEY_SPACE 0x39
#define NN_KEY_STOP 0x95
#define NN_KEY_TAB 0x0F
#define NN_KEY_UNDERLINE 0x93

#define NN_KEY_UP 0xC8
#define NN_KEY_DOWN 0xD0
#define NN_KEY_LEFT 0xCB
#define NN_KEY_RIGHT 0xCD
#define NN_KEY_HOME 0xC7
#define NN_KEY_END 0xCF
#define NN_KEY_PAGEUP 0xC9
#define NN_KEY_PAGEDOWN 0xD1
#define NN_KEY_INSERT 0xD2
#define NN_KEY_DELETE 0xD3

#define NN_KEY_F1 0x3B
#define NN_KEY_F2 0x3C
#define NN_KEY_F3 0x3D
#define NN_KEY_F4 0x3E
#define NN_KEY_F5 0x3F
#define NN_KEY_F6 0x40
#define NN_KEY_F7 0x41
#define NN_KEY_F8 0x42
#define NN_KEY_F9 0x43
#define NN_KEY_F10 0x44
#define NN_KEY_F11 0x57
#define NN_KEY_F12 0x58
#define NN_KEY_F13 0x64
#define NN_KEY_F14 0x65
#define NN_KEY_F15 0x66
#define NN_KEY_F16 0x67
#define NN_KEY_F17 0x68
#define NN_KEY_F18 0x69
#define NN_KEY_F19 0x71

#define NN_KEYS_KANA 0x70
#define NN_KEYS_KANJI 0x94
#define NN_KEYS_CONVERT 0x79
#define NN_KEYS_NOCONVERT 0x7B
#define NN_KEYS_YEN 0x7D
#define NN_KEYS_CIRCUMFLEX 0x90
#define NN_KEYS_AX 0x96

#define NN_KEYS_NUMPAD0 0x52
#define NN_KEYS_NUMPAD1 0x4F
#define NN_KEYS_NUMPAD2 0x50
#define NN_KEYS_NUMPAD3 0x51
#define NN_KEYS_NUMPAD4 0x4B
#define NN_KEYS_NUMPAD5 0x4C
#define NN_KEYS_NUMPAD6 0x4D
#define NN_KEYS_NUMPAD7 0x47
#define NN_KEYS_NUMPAD8 0x48
#define NN_KEYS_NUMPAD9 0x49
#define NN_KEYS_NUMPADMUL 0x37
#define NN_KEYS_NUMPADDIV 0xB5
#define NN_KEYS_NUMPADSUB 0x4A
#define NN_KEYS_NUMPADADD 0x4E
#define NN_KEYS_NUMPADDECIMAL 0x53
#define NN_KEYS_NUMPADCOMMA 0xB3
#define NN_KEYS_NUMPADENTER 0x9C
#define NN_KEYS_NUMPADEQUALS 0x8D

// pushes a screen_resized signal
nn_Exit nn_pushScreenResized(nn_Computer *computer, const char *screenAddress, int newWidth, int newHeight);
// pushes a touch signal
// The signal is checked, as in, the player must be a user of the computer if users are defined.
nn_Exit nn_pushTouch(nn_Computer *computer, const char *screenAddress, double x, double y, int button, const char *player);
// pushes a drag signal
// The signal is checked, as in, the player must be a user of the computer if users are defined.
nn_Exit nn_pushDrag(nn_Computer *computer, const char *screenAddress, double x, double y, int button, const char *player);
// pushes a drop signal
// The signal is checked, as in, the player must be a user of the computer if users are defined.
nn_Exit nn_pushDrop(nn_Computer *computer, const char *screenAddress, double x, double y, int button, const char *player);
// pushes a scroll signal
// A positive direction usually means up, a negative one usually means down.
// The signal is checked, as in, the player must be a user of the computer if users are defined.
nn_Exit nn_pushScroll(nn_Computer *computer, const char *screenAddress, double x, double y, double direction, const char *player);
// pushes a walk signal
// The signal is checked, as in, the player must be a user of the computer if users are defined.
nn_Exit nn_pushWalk(nn_Computer *computer, const char *screenAddress, double x, double y, const char *player);

// pushes a key_down event
// charcode is the unicode code-point of the typed character. It should be uppercase/lowercase depending on shift or capslock.
// keycode is an OC-specific keycode, and should be from the NN_KEY_* constants.
// player is the name of the player which used the keyboard. Some programs use it for splitscreen games.
// The signal is checked, as in, the player must be a user of the computer if users are defined.
nn_Exit nn_pushKeyDown(nn_Computer *computer, const char *keyboardAddress, nn_codepoint charcode, int keycode, const char *player);
// pushes a key_up event
// charcode is the unicode code-point of the typed character. It should be uppercase/lowercase depending on shift or capslock.
// keycode is an OC-specific keycode, and should be from the NN_KEY_* constants.
// player is the name of the player which used the keyboard. Some programs use it for splitscreen games.
// The signal is checked, as in, the player must be a user of the computer if users are defined.
nn_Exit nn_pushKeyUp(nn_Computer *computer, const char *keyboardAddress, nn_codepoint charcode, int keycode, const char *player);
// pushes a clipboard event
// clipboard should be a NULL-terminated string.
// NN does no truncation of the contents, but it is best to limit it.
// The signal is checked, as in, the player must be a user of the computer if users are defined.
nn_Exit nn_pushClipboard(nn_Computer *computer, const char *keyboardAddress, const char *clipboard, const char *player);
// pushes a clipboard event
// len is the length of the clipboard.
// NN does no truncation of the contents, but it is best to limit it.
// The signal is checked, as in, the player must be a user of the computer if users are defined.
nn_Exit nn_pushLClipboard(nn_Computer *computer, const char *keyboardAddress, const char *clipboard, size_t len, const char *player);

// TODO: the remaining vanilla ones in https://ocdoc.cil.li/component:signals

#ifdef __cplusplus
}
#endif

#endif
