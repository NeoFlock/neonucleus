#ifndef NEONUCLEUS_H
#define NEONUCLEUS_H

#ifdef __cplusplus
extern "C" {
#endif

// Platform checking support, to help out users.
// Used internally as well.
// Based off https://stackoverflow.com/questions/5919996/how-to-detect-reliably-mac-os-x-ios-linux-windows-in-c-preprocessor
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
	#ifndef NN_WINDOWS
	#define NN_WINDOWS
	#endif
#elif __APPLE__
    #ifndef NN_MACOS
    #define NN_MACOS
    #endif
#elif __linux__
    #ifndef NN_LINUX
    #define NN_LINUX
    #endif
#endif

#if __unix__
    #ifndef NN_UNIX
    #define NN_UNIX
    #endif
    #ifndef NN_POSIX
    #define NN_POSIX
    #endif
#elif defined(_POSIX_VERSION)
    #ifndef NN_POSIX
    #define NN_POSIX
    #endif
#endif


#if defined(_MSC_VER) && !defined(__cplusplus)
#ifndef NN_ATOMIC_MSVC
#define NN_ATOMIC_MSVC
#endif
#endif

#ifdef _MSC_VER
#define NN_INIT(type)
#else
#define NN_INIT(type) (type)
#endif

// every C standard header we depend on, conveniently put here
#include <stddef.h> // for NULL,
#include <stdint.h> // for intptr_t
#include <stdbool.h> // for true, false and bool

/* MSVC can't use VLA;
	* What we see 							: NN_VLA(const char *, comps, len);
	* What compiler see after preproccessor : const char * comps[len];
	* What actaully was						: const char *comps[len];
*/
// Test: gcc -E -DNN_VLA\(type,name,count\)="type name[count]" -x c - <<< 'NN_VLA(const char *, comps, len);'
#ifdef _MSC_VER
// avoid #include <malloc.h>, for it can pollute the namespace
// with symbols to functions which are not linked with in baremetal.
void *_alloca(size_t);
#define NN_VLA(type, name, count) type *name = (type *)_alloca(sizeof(type) * (count))
#else
#define NN_VLA(type, name, count) type name[count]
#endif

// Internally we need stdatomic.h and, if NN_BAREMETAL is not defined, stdlib.h and time.h

// The entire NeoNucleus API, in one header file

// Internal limits or constants

#define NN_KiB (1024)
#define NN_MiB (1024 * NN_KiB)
#define NN_GiB (1024 * NN_MiB)
#define NN_TiB ((size_t)1024 * NN_GiB)

// the alignment an allocation should have
#define NN_ALLOC_ALIGN 16
// the maximum amount of items the callstack can have.
#define NN_MAX_STACK 256
// the maximum size a path is allowed to have, including the NULL terminator!
#define NN_MAX_PATH 256
// the maximum size of a label
#define NN_MAX_LABEL 256
// maximum size of a wakeup message
#define NN_MAX_WAKEUPMSG 2048
// the maximum amount of file descriptors that can be open simultaneously
#define NN_MAX_OPENFILES 16
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
#define NN_MAX_UNICODE_BUFFER 4

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
size_t nn_unicode_codepointToChar(char buffer[NN_MAX_UNICODE_BUFFER], nn_codepoint codepoint);
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
// In NeoNucleus, it is the same stuff, but direct ones may still be used across threads.
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

// Memory allocation!!!

void *nn_alloc(nn_Context *ctx, size_t size);
void nn_free(nn_Context *ctx, void *memory, size_t size);
void *nn_realloc(nn_Context *ctx, void *memory, size_t oldSize, size_t newSize);

char *nn_strdup(nn_Context *ctx, const char *s);
void nn_strfree(nn_Context *ctx, char *s);

typedef struct nn_Lock nn_Lock;

nn_Lock *nn_createLock(nn_Context *ctx);
void nn_destroyLock(nn_Context *ctx, nn_Lock *lock);
void nn_lock(nn_Context *ctx, nn_Lock *lock);
void nn_unlock(nn_Context *ctx, nn_Lock *lock);

double nn_currentTime(nn_Context *ctx);

// generate a random RNG from 0 to the maximum
size_t nn_rand(nn_Context *ctx);
// generate a random float [0, 1)
double nn_randf(nn_Context *ctx);
// generate a random float [0, 1]
double nn_randfi(nn_Context *ctx);

typedef char nn_uuid[37];
void nn_randomUUID(nn_Context *ctx, nn_uuid uuid);

// Basic utils

// Does canonical path handling. Is used for sandboxing paths.
// It will turn \ to /, will turn invalid characters into spaces,
// and will resolve ...
// it also gets rid of / prefixes, / suffixes and //
void nn_simplifyPath(const char original[NN_MAX_PATH], char simplified[NN_MAX_PATH]);

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

nn_Universe *nn_createUniverse(nn_Context *ctx, void *userdata);
void nn_destroyUniverse(nn_Universe *universe);
void *nn_getUniverseData(nn_Universe *universe);
size_t nn_getUniverseMemoryLimit(nn_Universe *universe);
size_t nn_limitMemory(nn_Universe *universe, size_t memory);
void nn_setUniverseMemoryLimit(nn_Universe *universe, size_t limit);
size_t nn_getUniverseStorageLimit(nn_Universe *universe);
void nn_setUniverseStorageLimit(nn_Universe *universe, size_t limit);
size_t nn_limitStorage(nn_Universe *universe, size_t storage);

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
	// run 1 tick or synchronized task
	NN_ARCH_TICK,
	// get the free memory
	NN_ARCH_FREEMEM,
	// deserialize from an encoded state
	NN_ARCH_DESERIALIZE,
	// serialize to an encoded state
	NN_ARCH_SERIALIZE,
	// drop the encoded buffer
	NN_ARCH_DROPSERIALIZED,
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
		// in the case of NN_ARCH_TICK, where the tick is synchronized
		bool synchronized;
		// in the case of NN_ARCH_FREEMEM, the free memory
		size_t freeMemory;
		// in the case of NN_ARCH_DESERIALIZE, NN_ARCH_SERIALIZE and NN_ARCH_DROPSERIALIZED, the buffer.
		struct {
			union {
				char *memOut;
				const char *memIn;
			};
			size_t memLen;
		};
	};
} nn_ArchitectureRequest;

typedef nn_Exit nn_ArchitectureHandler(nn_ArchitectureRequest *req);

typedef struct nn_Architecture {
	const char *name;
	void *state;
	nn_ArchitectureHandler *handler;
} nn_Architecture;

// Standard RAM sizes.
// Standard OC goes from tier 1 to tier 6,
// NN adds 2 more tiers.
extern size_t nn_ramSizes[8];

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
void nn_lockComputer(nn_Computer *computer);
void nn_unlockComputer(nn_Computer *computer);
// stops the computer if an architecture state is already present,
// will also clear the signal buffer and set the state to NN_BOOTUP.
nn_Exit nn_startComputer(nn_Computer *computer);
// destroys the architecture state if present.
// Will also do other shutdown routines, such as unmounting every
void nn_stopComputer(nn_Computer *computer);
void nn_forceCrashComputer(nn_Computer *computer, const char *s);
// returns whether an architecture state is present
bool nn_isComputerOn(nn_Computer *computer);

typedef enum nn_ComputerEvent {
	// when powered on
	NN_COMPUTER_POWERON,
	// when powered off
	NN_COMPUTER_POWEROFF,
	// when force-crashed
	NN_COMPUTER_FORCECRASH,
	// when crashed
	NN_COMPUTER_CRASH,
} nn_ComputerEvent;

typedef void nn_ComputerListener(nn_Computer *computer, nn_ComputerEvent event);
void nn_setComputerListener(nn_Computer *computer, nn_ComputerListener *listener);

typedef struct nn_Beep {
	double frequency;
	double duration;
	double volume;
} nn_Beep;

void nn_setComputerBeep(nn_Computer *computer, nn_Beep beep);
bool nn_getComputerBeep(nn_Computer *computer, nn_Beep *beep);
void nn_clearComputerBeep(nn_Computer *computer);

// get the userdata pointer
void *nn_getComputerUserdata(nn_Computer *computer);
const char *nn_getComputerAddress(nn_Computer *computer);
nn_Universe *nn_getComputerUniverse(nn_Computer *computer);
nn_Context *nn_getUniverseContext(nn_Universe *universe);
nn_Context *nn_getComputerContext(nn_Computer *computer);
// Sets the memory scale, which defaults to 1.
// For context, OC will set the memory scale to 1.8 on 64-bit systems by default.
// This scale affects how much real-world memory an amount of VM actually takes up.
// This means if the total memory is 4 MiB, and the scale is set to 2, the computer can take up to 8MiB of actual RAM.
// However, nn_getTotalMemory() will still return 4 MiB.
// The architecture is meant to ensure that the reported free memory is also scaled. As in, the real-world free memory
// is divided by this scale to ensure it is within the correct range.
// It is undefined behavior to change the memory scale *after* the first call to nn_tick().
// Some architectures may ignore this, if they are very low-level and thus
// do not have any implicit changes of sizes between 32-bit and 64-bit.
void nn_setMemoryScale(nn_Computer *computer, double scale);
double nn_getMemoryScale(nn_Computer *computer);

// Returns the memory usage limit of the computer.
size_t nn_getTotalMemory(nn_Computer *computer);
// Gets the total amount of free memory the computer has available. The total memory - this is the amount of memory used.
size_t nn_getFreeMemory(nn_Computer *computer);
// Gets the total amount of used memory the computer has allocated.
// This is just the total minus the free, and does not take into
// account the overhead of storing the computer instance.
size_t nn_getUsedMemory(nn_Computer *computer);
// gets the current uptime of a computer. When the computer is not running, this value can be anything and loses all meaning.
double nn_getUptime(nn_Computer *computer);

// Deserialize an encoded computer state.
// Encoding depends on architecture.
nn_Exit nn_deserializeComputer(nn_Computer *computer, const char *buf, size_t buflen);

// Serialize the computer state.
// Encoding depends on architecture.
nn_Exit nn_serializeComputer(nn_Computer *computer, char **buf, size_t *buflen);

// Free the serialized buffer.
nn_Exit nn_freeSerializedComputer(nn_Computer *computer, char *buf, size_t buflen);

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
// gets the current amount of energy
double nn_getEnergy(nn_Computer *computer);
// Returns true if there is no more energy left, and a blackout has occured.
bool nn_removeEnergy(nn_Computer *computer, double energy);

// the handler of energy costs.
// The default handler just returns the total energy.
// Computers do not keep track of their current energy, they just call this function.
// getEnergy() calls this with amountToRemove set to 0. It is recommended to handle this as a fastpath.
// This should return the new amount of energy after the removal.
// A negative amount can be returned, it will be clamped to 0.
// If an error occurs, it is recommended to just return 0 and trigger a blackout.
// TODO: evaluate if API should be reworked to handle errors in energy handler.
typedef double nn_EnergyHandler(void *energyState, nn_Computer *computer, double amountToRemove);

void nn_setEnergyHandler(nn_Computer *computer, void *energyState, nn_EnergyHandler *handler);

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

// Checks if the uptime is below the idle timestamp.
bool nn_isComputerIdle(nn_Computer *computer);
// Shifts over the idle timestamp.
void nn_addIdleTime(nn_Computer *computer, double time);
void nn_resetIdleTime(nn_Computer *computer);
// runs a tick of the computer. Make sure to check the state as well!
// Does not do anything if we're currently waiting on a synced call
// This automatically resets the component budgets and call budget.
// It also sets the idle timestamp to the current uptime.
nn_Exit nn_tick(nn_Computer *computer);

// runs a synchronized tick of the computer. How this differs depends on architecture.
// Generally, this is meant to be in the same thread for all computers, and is if the external world is fundamentally not thread-safe,
// however components must interact with it.
// In this case, those component methods would be marked as NN_INDIRECT, or more accurately will not be marked as NN_DIRECT, and the architecture would queue them as synchronized tasks.
// Architectures should generally NOT ignore this if they can.
nn_Exit nn_tickSynchronized(nn_Computer *computer);

// raw component and methods

typedef struct nn_Component nn_Component;

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
	const char *doc;
	nn_MethodFlags flags;
} nn_Method;

// component signals

// mounted
#define NN_CSIGMOUNTED "mounted"
// unmounted
#define NN_CSIGUNMOUNTED "unmounted"

typedef enum nn_ComponentAction {
	// component dropped
	NN_COMP_DROP,
	// component method invoked
	NN_COMP_INVOKE,
	// checking if component method is enabled
	// (may be locked by tier)
	NN_COMP_CHECKMETHOD,
	// signal sent to the machine
	NN_COMP_SIGNAL,
} nn_ComponentAction;

typedef struct nn_ComponentRequest {
	nn_Context *ctx;
	nn_Computer *computer;
	void *state;
	void *classState;
	const char *compAddress;
	nn_ComponentAction action;
	// method index
	unsigned int methodIdx;
	union {
		// return count
		size_t returnCount;
		// method enabled
		bool methodEnabled;
		// signal invocation
		const char *signal;
	};
} nn_ComponentRequest;

typedef nn_Exit (nn_ComponentHandler)(nn_ComponentRequest *request);

// creates a blank component.
// It has no methods, 
nn_Component *nn_createComponent(nn_Universe *universe, const char *address, const char *type);
void nn_retainComponent(nn_Component *c);
void nn_retainComponentN(nn_Component *c, size_t n);
void nn_dropComponent(nn_Component *c);
void nn_dropComponentN(nn_Component *c, size_t n);

// configure the state
void nn_setComponentHandler(nn_Component *c, nn_ComponentHandler *handler);
void nn_setComponentState(nn_Component *c, void *state);
void nn_setComponentClassState(nn_Component *c, void *state);
// sets the methods, same implications as setComponentMethodsArray.
// methods is NULL-terminated, as in, it is terminated by a method with a NULL name.
nn_Exit nn_setComponentMethods(nn_Component *c, const nn_Method *methods);
// sets the methods.
// The memory of the strings is copied, so they can be freed after this returns.
// This operation is NOT atomic, if it fails, it will clear out the previous methods.
nn_Exit nn_setComponentMethodsArray(nn_Component *c, const nn_Method *methods, size_t count);
// Sets an internal type ID, which is meant to be a more precise typename.
// For example, ncomplib would set ncl-screen for the screen component,
// so the GPU can confirm it is being bound to a screen it knows how to use.
nn_Exit nn_setComponentTypeID(nn_Component *c, const char *internalTypeID);

// get component state
void *nn_getComponentState(nn_Component *c);
// get component class state
void *nn_getComponentClassState(nn_Component *c);
// counts how many methods are registered. May return too many if some of them are not enabled.
size_t nn_countComponentMethods(nn_Component *c);
// will fill the methodnames array with the names of the *enabled* methods.
// Will set *len to the amount of methods.
void nn_getComponentMethods(nn_Component *c, const char **methodnames, size_t *len);
// whether a method is defined and enabled
bool nn_hasComponentMethod(nn_Component *c, const char *method);
const char *nn_getComponentDoc(nn_Component *c, const char *method);
nn_MethodFlags nn_getComponentMethodFlags(nn_Component *c, const char *method);
const char *nn_getComponentType(nn_Component *c);
const char *nn_getComponentTypeID(nn_Component *c);
const char *nn_getComponentAddress(nn_Component *c);

// Adds a component to the computer on a given slot.
// This will also queue a component_added signal if the computer is in a running state, unless silent is true.
// If the component already is mounted, an error is returned.
nn_Exit nn_mountComponent(nn_Computer *c, nn_Component *comp, int slot, bool silent);
// Removes a component from the computer.
// This will also queue a component_removed signal if the computer is in a running state, unless silent is true.
// If the component is not mounted, no error is returned.
nn_Exit nn_unmountComponent(nn_Computer *c, const char *address, bool silent);
nn_Exit nn_swapComponents(nn_Computer *c, nn_Component *previous, nn_Component *next, int slot);
// gets a component by address. Will return NULL if there is none.
nn_Component *nn_getComponent(nn_Computer *c, const char *address);
int nn_getComponentSlot(nn_Computer *c, const char *address);
size_t nn_countComponents(nn_Computer *c);
void nn_getComponents(nn_Computer *c, const char **components);

// invoke the component method.
// Everything on-stack is taken as an argument.
// Will pop off trailing nulls.
// Every remaining is what the component returned.
// In the case of 
nn_Exit nn_invokeComponent(nn_Computer *computer, const char *compAddress, const char *method);

// send a signal to a component.
// Computer actually can be NULL, but the component may crash if the signal
// assumes one is specified.
nn_Exit nn_signalComponent(nn_Component *component, nn_Computer *computer, const char *signal);

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

// The high-level API of the built-in component classes.
// These components still make no assumptions about the OS, and still require handlers to connect them to the outside work.

// Wrapping a computer as a component.
// It's a new instance each time.
// The computer MUST NOT be dropped before this component is fully gone.

nn_Exit nn_transferErrorFrom(nn_Exit exit, nn_Computer *from, nn_Computer *to);
nn_Computer *nn_fromWrappedComputer(nn_Component *component);
nn_Component *nn_wrapComputer(nn_Computer *computer);

// EEPROM class

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
	// idle time added when writing code
	double writeDelay;
	// idle time added when writing data
	double writeDataDelay;
} nn_EEPROM;

typedef enum nn_EEPROMAction {
	// component is dropped
	NN_EEPROM_DROP,
	// check if readonly. If so, buflen should be 1, else it should be 0.
	NN_EEPROM_ISRO,
	// make the EEPROM readonly. Checksum already verified.
	NN_EEPROM_MKRO,
	// write the contents of the code into buf.
	// Set buflen to the length.
	NN_EEPROM_GET,
	// store the contents in buf into the EEPROM as code.
	// the length of buf is in buflen.
	NN_EEPROM_SET,
	NN_EEPROM_GETDATA,
	NN_EEPROM_SETDATA,
	NN_EEPROM_GETARCH,
	NN_EEPROM_SETARCH,
	NN_EEPROM_GETLABEL,
	NN_EEPROM_SETLABEL,
} nn_EEPROMAction;

typedef struct nn_EEPROMRequest {
	nn_Context *ctx;
	nn_Computer *computer;
	void *state;
	const nn_EEPROM *eeprom;
	nn_EEPROMAction action;
	union {
		char *buf;
		const char *robuf;
	};
	size_t buflen;
} nn_EEPROMRequest;

typedef nn_Exit (nn_EEPROMHandler)(nn_EEPROMRequest *request);

// Tier 1 - The normal EEPROM equivalent
// Tier 2 - A better EEPROM
// Tier 3 - An even better EEPROM
// Tier 4- The best EEPROM
extern const nn_EEPROM nn_defaultEEPROMs[4];

nn_Component *nn_createEEPROM(nn_Universe *universe, const char *address, const nn_EEPROM *eeprom, void *state, nn_EEPROMHandler *handler);

// Filesystem class

typedef struct nn_Filesystem {
	// the maximum capacity of the filesystem
	size_t spaceTotal;
	// how many read calls can be done per tick
	// seek also count as reads.
	double readsPerTick;
	// how many write calls can be done per tick
	double writesPerTick;
	// The energy cost of an actual read/write.
	// It is per-byte, so if a read returns 4096 bytes, then this cost is multiplied by 4096.
	double dataEnergyCost;
	// maximum size of a read.
	// Do note that this entire buffer is allocated, and thus if you
	// set it to a high number, you may get weird high allocations.
	// This also determines read performance.
	size_t maxReadSize;
} nn_Filesystem;

typedef enum nn_FSAction {
	NN_FS_DROP,
	
	// drive metadata
	NN_FS_SPACEUSED,
	NN_FS_GETLABEL,
	NN_FS_SETLABEL,
	NN_FS_ISRO,

	// for file I/O
	NN_FS_OPEN,
	NN_FS_CLOSE,
	NN_FS_READ,
	NN_FS_WRITE,
	NN_FS_SEEK,

	// for list
	NN_FS_OPENDIR,
	NN_FS_READDIR,
	NN_FS_CLOSEDIR,

	// checking metadata
	NN_FS_STAT,
	// make directory, recursively
	NN_FS_MKDIR,
	
	// rename, if renamed to NULL then remove
	NN_FS_RENAME,
} nn_FSAction;

typedef enum nn_FSWhence {
	NN_SEEK_SET,
	NN_SEEK_CUR,
	NN_SEEK_END,
} nn_FSWhence;

typedef struct nn_FSRequest {
	nn_Context *ctx;
	nn_Computer *computer;
	void *state;
	const nn_Filesystem *fs;
	nn_FSAction action;
	int fd;
	union {
		struct {
			const char *path;
			const char *mode;
		} open;
		struct {
			char *buf;
			size_t len;
		} read;
		struct {
			const char *buf;
			size_t len;
		} write;
		struct {
			nn_FSWhence whence;
			// set to new offset
			int off;
		} seek;
		const char *opendir;
		struct {
			// directory path, as a reminder if need be
			const char *dirpath;
			char *buf;
			// set to length of entry name
			size_t len;
		} readdir;
		struct {
			// set to NULL if missing
			const char *path;
			// whether it is a directory
			bool isDirectory;
			// in seconds. Result will be multiplied by 1000.
			// This is because OpenOS code is garbage.
			intptr_t lastModified;
			// size. 0 for directories.
			size_t size;
		} stat;
		struct {
			const char *from;
			// if NULL, delete from, recursively.
			const char *to;
		} rename;
		const char *mkdir;
		struct {
			const char *buf;
			size_t len;
		} setlabel;
		struct {
			char *buf;
			size_t len;
		} getlabel;
		bool isReadonly;
		size_t spaceUsed;
	};
} nn_FSRequest;

typedef nn_Exit (nn_FSHandler)(nn_FSRequest *request);

// 4 Tiers.
// 0 - Tier 1 equivalent
// 1 - Tier 2 equivalent
// 2 - Tier 3 equivalent
// 3 - Tier 4, a better version of Tier 3.
extern const nn_Filesystem nn_defaultFilesystems[4];
// a basic floppy
extern const nn_Filesystem nn_defaultFloppy;
// a generic tmpfs
extern const nn_Filesystem nn_defaultTmpFS;

nn_Component *nn_createFilesystem(nn_Universe *universe, const char *address, const nn_Filesystem *fs, void *state, nn_FSHandler *handler);

bool nn_mergeFilesystems(nn_Filesystem *merged, const nn_Filesystem *fs, size_t len);

// Drive class

typedef struct nn_Drive {
	// The capacity of the drive.
	// It is in bytes, but it MUST be a multiple of the sector size.
	// The total amount of sectors, as in capacity / sectorSize, must also be divisible by the platter count.
	// If it is not, it is UB.
	size_t capacity;
	// the sector size, typically 512
	size_t sectorSize;
	// the amount of platters the drive has. This contributes to how many "rotations" are needed.
	// A drive with 8 sectors but 1 platter, when seeking from sector 1 to 8, would mean 7 rotations.
	// However, if it has 2 platters, it'd be seen as 1 to 4 being at the same angle as 5 to 8, which
	// would mean only 3 rotations.
	size_t platterCount;
	// how many reads can be issued per tick.
	// Anything that kicks out the current cacheline counts as a read.
	size_t readsPerTick;
	// how many writes can be issued per tick.
	// Writing a sector counts as 1 write.
	// Writing a byte counts as 1 read (may be eaten by cache) and 1 write,
	// you can imagine it as reading the sector, editing the byte,
	// then writing the sector back.
	size_t writesPerTick;
	// Set to 0 for *infinite*, effectively an SSD with infinite lifespan.
	// This would mean there is 0 penalty for seeking (technically unreliastic even for an SSD).
	// This is simply used to compute idle time. It is in literal full rotations per minute.
	// It is aligned to the cache lines.
	size_t rpm;
	// If false, it behaves like a normal OC drive, where the drive can spin backwards to seek.
	// However, this is unrealistic, as doing so may crack the sensitive platter and make the
	// reader lose lift.
	// For fans of physics, this option only allows the seeks to go forwards.
	// This is super punishing at a slow RPM, so it is recommended to bump up
	// the RPM to something like 7200 RPM.
	bool onlySpinForwards;
	// The energy cost of an actual read/write.
	// It is per-byte, so if a read returns 4096 bytes, then this cost is multiplied by 4096.
	double dataEnergyCost;
} nn_Drive;

extern const nn_Drive nn_defaultDrives[4];
extern const nn_Drive nn_floppyDrive;


typedef enum nn_DriveAction {
	// drive gone
	NN_DRIVE_DROP,
	// get current label
	NN_DRIVE_GETLABEL,
	// set or remove current label
	NN_DRIVE_SETLABEL,
	// get the current head position, as a sector
	NN_DRIVE_CURPOS,
	// read a sector
	NN_DRIVE_READSECTOR,
	// write a sector
	NN_DRIVE_WRITESECTOR,
	// write a byte
	NN_DRIVE_WRITEBYTE,
	// is drive read-only
	NN_DRIVE_ISRO,
} nn_DriveAction;

typedef struct nn_DriveRequest {
	nn_Context *ctx;
	nn_Computer *computer;
	void *state;
	const nn_Drive *drv;
	nn_DriveAction action;
	union {
		struct {
			char *buf;
			size_t len;
		} getlabel;
		struct {
			const char *label;
			size_t len;
		} setlabel;
		size_t curpos;
		struct {
			// 1-indexed
			size_t sector;
			char *buf;
		} readSector;
		struct {
			// 1-indexed
			size_t sector;
			const char *buf;
		} writeSector;
		struct {
			// 1-indexed
			size_t byte;
			unsigned char value;
		} writeByte;
		bool readonly;
	};
} nn_DriveRequest;

typedef nn_Exit (nn_DriveHandler)(nn_DriveRequest *request);

nn_Component *nn_createDrive(nn_Universe *universe, const char *address, const nn_Drive *drive, void *state, nn_DriveHandler *handler);

bool nn_mergeDrives(nn_Drive *merged, const nn_Drive *drives, size_t len);

typedef struct nn_NandFlash {
	// capacity of flash
	size_t capacity;
	// sector size
	size_t sectorSize;
	// reads per tick
	size_t readsPerTick;
	// writes per tick
	size_t writesPerTick;
	// The layering, in bits.
	// 1 is SLC, 2 is MLC, 3 is TLC, etc.
	// This number may amplify how quickly the total write count increases.
	size_t cellLevel;
	// the maximum amount of write amplification.
	// Set to 0 to disable amplification RNG.
	// The game will generate, using Context RNG, a real number from [0, 1]
	// then raise it to writeAmplificationExponent,
	// then multiply it by this number, and by the cell level.
	// then clamp it to be at least 1 and at most this maximum.
	unsigned int maxWriteAmplification;
	int writeAmplificationExponent;
	// the maximum amount of writes *per sector.*
	// Set to 0 to make the nandflash eternal.
	size_t maxWriteCount;
	// how much per byte
	double dataEnergyCost;
} nn_NandFlash;

typedef enum nn_FlashAction {
	NN_FLASH_DROP,
	NN_FLASH_GETLABEL,
	NN_FLASH_SETLABEL,
	NN_FLASH_ISRO,
	// read a sector
	NN_FLASH_READSECTOR,
	// write a sector
	// also adds an amount of writes
	NN_FLASH_WRITESECTOR,
	// write a sector
	// also adds an amount of writes
	NN_FLASH_WRITEBYTE,
	// get the amount of writes
	NN_FLASH_GETWRITES,
} nn_FlashAction;

typedef struct nn_FlashRequest {
	nn_Context *ctx;
	nn_Computer *computer;
	void *state;
	const nn_NandFlash *flash;
	nn_FlashAction action;
	union {
		struct {
			char *buf;
			size_t len;
		} getlabel;
		struct {
			const char *buf;
			size_t len;
		} setlabel;
		struct {
			char *buf;
			// 1-indexed
			size_t sec;
		} readsector;
		struct {
			const char *buf;
			// 1-indexed
			size_t sec;
			// how many writes to add
			size_t writesAdded;
		} writesector;
		struct {
			size_t byte;
			char val;
			// how many writes to add
			size_t writesAdded;
		} writebyte;
		// for GETWRITES
		size_t writeCount;
		bool readonly;
	};
} nn_FlashRequest;

typedef nn_Exit (nn_FlashHandler)(nn_FlashRequest *request);

extern const nn_NandFlash nn_defaultSSDs[4];
extern const nn_NandFlash nn_floppySSD;

nn_Component *nn_createFlash(nn_Universe *universe, const char *address, const nn_NandFlash *drive, void *state, nn_FlashHandler *handler);

bool nn_mergeFlash(nn_NandFlash *merged, const nn_NandFlash *flash, size_t len);

// Screen class

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
	// maximum width
	int maxWidth;
	// maximum height
	int maxHeight;
	// screen features
	nn_ScreenFeatures features;
	// default palette, if applicable.
	// Can be NULL if there is none,
	// in which case consider memsetting
	// them to #000000.
	int *defaultPalette;
	// the amount of editable palette colors
	int paletteColors;
	// how many editable palette colors there are.
	// It'd always be the first N ones.
	int editableColors;
	// the maximum depth of the screen
	char maxDepth;
	// energy per fully white pixel.
	// Scaled to mathc luminance of each pixel.
	// This is meant to be per Minecraft tick, so 20 times per second.
	double energyPerPixel;
	// minimum brightness. Default brightness is always 100%
	// The value here is meant to be scaled such that 1 means 100% brightness
	double minBrightness;
	// maximum brightness. Note that brightness rendering is emulator-specific
	// The value here is meant to be scaled such that 1 means 100% brightness
	double maxBrightness;
} nn_ScreenConfig;

// OC has 3 tiers, NN adds a 4th one as well.
extern const nn_ScreenConfig nn_defaultScreens[4];


// GPU class
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

// 1 GPU tier for every screen.
extern const nn_GPU nn_defaultGPUs[4];

typedef enum nn_GPUAction {
    NN_GPU_DROP,
    NN_GPU_BIND,
    NN_GPU_GETSCREEN,
    NN_GPU_GETBG,
    NN_GPU_SETBG,
    NN_GPU_GETFG,
    NN_GPU_SETFG,
    NN_GPU_GETPALETTE,
    NN_GPU_SETPALETTE,
    NN_GPU_MAXDEPTH,
    NN_GPU_GETDEPTH,
    NN_GPU_SETDEPTH,
    NN_GPU_MAXRES,
    NN_GPU_GETRES,
    NN_GPU_SETRES,
    NN_GPU_GETVIEWPORT,
    NN_GPU_SETVIEWPORT,
    NN_GPU_GET,
    NN_GPU_SET,
    NN_GPU_COPY,
    NN_GPU_FILL,
    NN_GPU_GETACTIVEBUF,
    NN_GPU_SETACTIVEBUF,
    NN_GPU_BUFFERS,
    NN_GPU_ALLOCBUF,
    NN_GPU_FREEBUF,
    NN_GPU_FREEALLBUFS,
    NN_GPU_FREEMEM,
    NN_GPU_GETBUFSIZE,
    NN_GPU_BITBLT,
} nn_GPUAction;

typedef struct nn_GPURequest {
    nn_Context *ctx;
    nn_Computer *computer;
    void *state;
    const nn_GPU *gpu;
    nn_GPUAction action;
    union {
        struct {
            const char *address;
            bool reset;
        } bind;
        // GETSCREEN result
        char screenAddr[NN_MAX_ADDRESS];
        // GET/SET BG/FG
        struct {
            int color;
            bool isPalette;
            int oldColor;
            bool wasPalette;
            int oldPaletteIdx; // -1 if none
        } color;
        // GET/SET PALETTE
        struct {
            int index;
            int color;
            int oldColor;
        } palette;
        // MAXDEPTH / GETDEPTH / SETDEPTH
        struct {
            char depth;
            char oldDepth;
        } depth;
        // MAXRES/GETRES/SETRES/GETVIEWPORT/SETVIEWPORT
        struct {
            int width;
            int height;
        } resolution;
        // GET pixel
        struct {
            int x, y;
            nn_codepoint codepoint;
            int fg, bg;
            int fgIdx, bgIdx; // -1 if not palette
        } get;
        // SET string
        struct {
            int x, y;
            const char *value;
            size_t len;
            bool vertical;
        } set;
        // COPY
        struct {
            int x, y, w, h, tx, ty;
        } copy;
        // FILL
        struct {
            int x, y, w, h;
            nn_codepoint codepoint;
        } fill;
        // GET/SET ACTIVE BUFFER, FREE BUFFER
        struct {
            int index;
        } buffer;
        // ALLOCATE BUFFER
        struct {
            int w, h, index;
        } allocBuf;
        // TOTALMEM / FREEMEM
        size_t memory;
        // GETBUFSIZE
        struct {
            int index, w, h;
        } bufSize;
        // BITBLT
        struct {
            int dst, col, row, w, h;
            int src, fromCol, fromRow;
        } bitblt;
        // BUFFERS / count returned here, indices
        // pushed on stack by handler
        size_t bufCount;
    };
} nn_GPURequest;

typedef nn_Exit (nn_GPUHandler)(nn_GPURequest *req);

nn_Component *nn_createGPU(
    nn_Universe *universe, const char *address,
    const nn_GPU *gpu, void *state,
    nn_GPUHandler *handler);

typedef enum nn_ScreenAction {
    NN_SCREEN_DROP,
    NN_SCREEN_ISON,
    NN_SCREEN_TURNON,
    NN_SCREEN_TURNOFF,
    NN_SCREEN_GETASPECTRATIO,
    NN_SCREEN_GETKEYBOARDS,
    NN_SCREEN_SETPRECISE,
    NN_SCREEN_ISPRECISE,
    NN_SCREEN_SETTOUCHINVERTED,
    NN_SCREEN_ISTOUCHINVERTED,
	// sets the brightness (from 0 to 1)
	NN_SCREEN_SETBRIGHT,
	// get the brightness (from 0 to 1)
	NN_SCREEN_GETBRIGHT,
} nn_ScreenAction;

typedef struct nn_ScreenRequest {
    nn_Context *ctx;
    nn_Computer *computer;
    void *state;
    const nn_ScreenConfig *screen;
    nn_ScreenAction action;
    union {
        // turnOn / turnOff / isOn
        struct {
            bool wasOn;
            bool isOn;
        } power;
        // getAspectRatio
        struct {
            int w, h;
        } aspect;
        // getKeyboards — addresses pushed on stack by
        // handler; count returned here
        size_t kbCount;
        // setPrecise / isPrecise /
        // setTouchModeInverted / isTouchModeInverted
        bool flag;
		double brightness;
    };
} nn_ScreenRequest;

typedef nn_Exit (nn_ScreenHandler)(nn_ScreenRequest *req);

nn_Component *nn_createScreen(
    nn_Universe *universe, const char *address,
    const nn_ScreenConfig *scrconf, void *state,
    nn_ScreenHandler *handler
);

typedef struct nn_Modem {
	// maximum range. Set to 0 for non-wireless modems
	size_t maxRange;
	// maximum values in a packet
	size_t maxValues;
	// maximum logical packet size. Note that the encoding is more efficient than the packet size algorithm estimates
	size_t maxPacketSize;
	// the maximum amount of open ports
	size_t maxOpenPorts;
	// whether the modem supports wired connectivity.
	// Support for wireless checks if maxRange > 0.
	bool isWired;
	// base energy cost of 1 network message
	double basePacketCost;
	// energy cost of a full packet, at the maximum logical size
	double fullPacketCost;
	// energy cost per wireless packet strength level
	double costPerStrength;
} nn_Modem;

// A buffer with encoded values
typedef struct nn_EncodedNetworkContents {
	nn_Context *ctx;
	char *buf;
	size_t buflen;
	size_t valueCount;
} nn_EncodedNetworkContents;

extern nn_Modem nn_defaultWiredModem;
extern nn_Modem nn_defaultWirelessModems[2];

typedef enum nn_ModemAction {
	// modem dropped
	NN_MODEM_DROP,
	// check whether a port is open
	NN_MODEM_ISOPEN,
	// open a port
	NN_MODEM_OPEN,
	// close a port
	NN_MODEM_CLOSE,
	// get open ports
	NN_MODEM_GETPORTS,
	// send/broadcast a message
	NN_MODEM_SEND,
	// get current modem strength
	NN_MODEM_GETSTRENGTH,
	// set current modem strength
	NN_MODEM_SETSTRENGTH,
	// returns the wake message
	NN_MODEM_GETWAKEMESSAGE,
	// set the wake message
	NN_MODEM_SETWAKEMESSAGE,
} nn_ModemAction;

typedef struct nn_ModemRequest {
    nn_Context *ctx;
    nn_Computer *computer;
    void *state;
    const nn_Modem *modem;
	const char *localAddress;
	nn_ModemAction action;
	union {
		struct {
			size_t port;
			bool opened;
		} isOpen;
		size_t openPort;
		// NN_CLOSEPORTS means close all
		size_t closePort;
		struct {
			// store the port numbers in this buffer
			size_t *activePorts;
			// the amount of active ports.
			// the initial value is the capacity of activePorts
			size_t len;
		} getPorts;
		struct {
			const nn_EncodedNetworkContents *contents;
			// NULL for broadcast
			const char *address;
			size_t port;
		} send;
		// for getStrength, setStrength.
		size_t strength;
		struct {
			char *buf;
			size_t len;
			bool isFuzzy;
		} getWake;
		struct {
			const char *buf;
			size_t len;
			bool isFuzzy;
		} setWake;
	};
} nn_ModemRequest;

typedef nn_Exit (nn_ModemHandler)(nn_ModemRequest *req);

nn_Component *nn_createModem(nn_Universe *universe, const char *address, const nn_Modem *modem, void *state, nn_ModemHandler *handler);

typedef struct nn_Tunnel {
	// maximum values in a packet
	size_t maxValues;
	// maximum logical packet size. Note that the encoding is more efficient than the packet size algorithm estimates
	size_t maxPacketSize;
} nn_Tunnel;

extern nn_Tunnel nn_defaultTunnel;

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

// Returns a number from 0 to 1 representing the perceived luminance.
double nn_colorLuminance(int color);
// Expensive.
// Maps a color to the closest match in a palette.
int nn_mapColor(int color, int *palette, size_t len);
// Expensive.
// Maps a color within a given depth.
// Invalid depths behave identically to 24-bit, in which case the color is left unchanged.
int nn_mapDepth(int color, int depth);

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

#define NN_KEY_KANA 0x70
#define NN_KEY_KANJI 0x94
#define NN_KEY_CONVERT 0x79
#define NN_KEY_NOCONVERT 0x7B
#define NN_KEY_YEN 0x7D
#define NN_KEY_CIRCUMFLEX 0x90
#define NN_KEY_AX 0x96

#define NN_KEY_NUMPAD0 0x52
#define NN_KEY_NUMPAD1 0x4F
#define NN_KEY_NUMPAD2 0x50
#define NN_KEY_NUMPAD3 0x51
#define NN_KEY_NUMPAD4 0x4B
#define NN_KEY_NUMPAD5 0x4C
#define NN_KEY_NUMPAD6 0x4D
#define NN_KEY_NUMPAD7 0x47
#define NN_KEY_NUMPAD8 0x48
#define NN_KEY_NUMPAD9 0x49
#define NN_KEY_NUMPADMUL 0x37
#define NN_KEY_NUMPADDIV 0xB5
#define NN_KEY_NUMPADSUB 0x4A
#define NN_KEY_NUMPADADD 0x4E
#define NN_KEY_NUMPADDECIMAL 0x53
#define NN_KEY_NUMPADCOMMA 0xB3
#define NN_KEY_NUMPADENTER 0x9C
#define NN_KEY_NUMPADEQUALS 0x8D

// the bottom side, can also be downwards / negative y
#define NN_SIDE_BOTTOM 0
// the top side, can also be upwards / positive y
#define NN_SIDE_TOP 1
// backwards, can also be north / negative z
#define NN_SIDE_BACK 2
// forwards, can also be south / positive z
#define NN_SIDE_FRONT 3
// to the right, can also be west / negative x
#define NN_SIDE_RIGHT 4
// to the left, can also be east / positive x
#define NN_SIDE_LEFT 5
// absolutely no clue
#define NN_SIDE_UNKNOWN 6

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

// pushes a redstone_changed signal.
// side is the side of the device the redstone changed on
// oldValue is the old value
// newValue is the new value, must not be equal to oldValue
// color is the color of the redstone signal
// if color < 0, it is set to null
nn_Exit nn_pushRedstoneChanged(nn_Computer *computer, const char *redstoneAddress, int side, int oldValue, int newValue, int color);

// pushes a motion signal.
// Do note that it is meant to only be sent if the entity has a direct line of sight and if |(relX, relY, relZ)| >= sensitivity.
// This signal does *not* check if the motion sensor actually is sensitive enough to detect it, so make sure to check it yourself.
// relX, relY and relZ are the relative postion in 3D Cartesian space.
// entityName can be NULL if the entity has no name.
nn_Exit nn_pushMotion(nn_Computer *computer, double relX, double relY, double relZ, const char *entityName);

// applies basic encoding to a network message. This encoding has a header, and thus should remain backwards-compatible.
// The encoding serves 2 purposes:
// 1. Prevent shared memory between computers. Values do not use atomic reference counting, and thus this could lead to UAF or memory leaks.
// 2. Simplify implementing packet queues, which should be used in relays. The lack of access to raw values would force implementers to use
// an encoding anyways.
// This only encodes the contents, not the sender, hops, or other metadata which may be needed in the queue.
// This does not pop the values, in case you need them afterwards. If you don't just call nn_popn().
// The encoding is architecture-dependent, so be careful with storing it on-disk.
// Do note that the architecture-dependent parts are sizeof(double), sizeof(size_t) and endianness.
// The encoding is simple:
// - 0x00 for null
// - 0x01 for true
// - 0x02 for false
// - 0x03 + <bytes of double> for a number
// - 0x04 + <bytes of size_t length> + <bytes> for a string
// - 0x05 + <bytes of size_t id> for resource
// - 0x06 + <bytes of size_t length> + <values> for a table
nn_Exit nn_encodeNetworkContents(nn_Computer *computer, nn_EncodedNetworkContents *contents, size_t valueCount);
// Allocates a copy of [buf] and stores it in contents.
// This is useful for copying network contents, either from storage or from another buffer.
nn_Exit nn_copyNetworkContents(nn_Context *ctx, nn_EncodedNetworkContents *contents, const char *buf, size_t buflen, size_t valueCount);
void nn_dropNetworkContents(nn_EncodedNetworkContents *contents);
// Pushes the encoded contents onto the stack.
// This does not drop the network contents.
nn_Exit nn_pushNetworkContents(nn_Computer *computer, const nn_EncodedNetworkContents *contents);

// push a modem_message, can be queued by both modems and tunnels.
// This does not check if the modem has that port open, so make sure to check it yourself.
// It does not check if the distance is within the modem's range, if it is wireless, and thus does not send it.
// Note that relays should change the sender.
nn_Exit nn_pushModemMessage(nn_Computer *computer, const char *modemAddress, const char *sender, int port, double distance, const nn_EncodedNetworkContents *contents);

#ifdef __cplusplus
}
#endif

#endif
