#ifndef NN_MODEL_H
#define NN_MODEL_H

#include "neonucleus.h"

typedef enum nn_BuiltinComponent {
	NN_BUILTIN_GPU = 0,
	NN_BUILTIN_SCREEN,
	NN_BUILTIN_KEYBOARD,
	NN_BUILTIN_FILESYSTEM,

	// to determine array size
	NN_BUILTIN_COUNT,
} nn_BuiltinComponent;

typedef struct nn_ComponentType {
	nn_Universe *universe;
	char *name;
	// NULL-terminated
	nn_ComponentMethod *methods;
} nn_ComponentType;

typedef struct nn_Universe {
	nn_Context ctx;
	nn_ComponentType *types[NN_BUILTIN_COUNT];
} nn_Universe;

typedef struct nn_Component {
	char *address;
	nn_ComponentType *ctype;
	size_t slot;
	void *userdata;
} nn_Component;

// the values
typedef enum nn_ValueType {
	NN_VAL_NULL,
	NN_VAL_BOOL,
	NN_VAL_NUM,
	NN_VAL_STR,
	NN_VAL_USERDATA,
	NN_VAL_TABLE,
} nn_ValueType;

typedef struct nn_String {
	nn_Context ctx;
	size_t refc;
	size_t len;
	char data[];
} nn_String;

typedef struct nn_Value {
	nn_ValueType type;
	union {
		bool boolean;
		double number;
		nn_String *string;
		size_t userdataIdx;
		struct nn_Table *table;
	};
} nn_Value;

typedef struct nn_Table {
	nn_Context ctx;
	size_t refc;
	size_t len;
	nn_Value vals[];
} nn_Table;

typedef struct nn_Computer {
	nn_ComputerState state;
	nn_Universe *universe;
	char *address;
	nn_Architecture *arch;
	nn_Architecture *desiredArch;
	size_t componentCap;
	size_t componentLen;
	nn_Component *components;
	size_t deviceInfoCap;
	size_t deviceInfoLen;
	nn_DeviceInfo *deviceInfo;
	double totalEnergy;
	double energy;
	size_t totalMemory;
	double creationTimestamp;
	size_t stackSize;
	size_t archCount;
	nn_Value callstack[NN_MAX_STACK];
	char errorBuffer[NN_MAX_ERROR_SIZE];
	nn_Architecture archs[NN_MAX_ARCHITECTURES];
} nn_Computer;

#endif
