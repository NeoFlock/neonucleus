#include "../neonucleus.h"

typedef struct nn_externalComputer_t {
	nn_Context ctx;
	nn_refc refc;
	nn_guard *lock;
	nn_externalComputerTable_t table;
} nn_externalComputer_t;

nn_externalComputer_t *nn_newExternalComputer(nn_Context *ctx, nn_externalComputerTable_t table) {
	nn_externalComputer_t *external = nn_alloc(&ctx->allocator, sizeof(nn_externalComputer_t));
	if(external == NULL) return NULL;
	external->lock = nn_newGuard(ctx);
	if(external->lock == NULL) {
		nn_dealloc(&ctx->allocator, external, sizeof(nn_externalComputer_t));
		return NULL;
	}
	return external;
}

nn_guard *nn_externalComputer_getLock(nn_externalComputer_t *external) {
	return external->lock;
}

void nn_externalComputer_retain(nn_externalComputer_t *external) {
	nn_incRef(&external->refc);
}

nn_bool_t nn_externalComputer_destroy(nn_externalComputer_t *external) {
	if(!nn_decRef(&external->refc)) return false;
	
	nn_Context ctx = external->ctx;
	
	nn_deleteGuard(&ctx, external->lock);
	nn_dealloc(&ctx.allocator, external, sizeof(nn_externalComputer_t));

	return true;
}

void nni_externalComputer_componentDestroy(void *_, nn_component *component, nn_externalComputer_t *external) {
    nn_externalComputer_destroy(external);
}

static void nni_externalComputer_start(nn_externalComputer_t *external, void *_, nn_component *component, nn_computer *computer) {
	nn_errorbuf_t err = "";

	nn_lock(&external->ctx, external->lock);
	nn_bool_t worked = external->table.start(external->table.userdata, computer, err);
	nn_unlock(&external->ctx, external->lock);

	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}

	nn_return_boolean(computer, worked);
}

static void nni_externalComputer_stop(nn_externalComputer_t *external, void *_, nn_component *component, nn_computer *computer) {
	nn_errorbuf_t err = "";

	nn_lock(&external->ctx, external->lock);
	nn_bool_t worked = external->table.stop(external->table.userdata, computer, err);
	nn_unlock(&external->ctx, external->lock);

	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}

	nn_return_boolean(computer, worked);
}

static void nni_externalComputer_isRunning(nn_externalComputer_t *external, void *_, nn_component *component, nn_computer *computer) {
	nn_errorbuf_t err = "";

	nn_lock(&external->ctx, external->lock);
	nn_bool_t truthy = external->table.isRunning(external->table.userdata, computer, err);
	nn_unlock(&external->ctx, external->lock);

	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}

	nn_return_boolean(computer, truthy);
}

static void nni_externalComputer_isRobot(nn_externalComputer_t *external, void *_, nn_component *component, nn_computer *computer) {
	nn_errorbuf_t err = "";

	nn_lock(&external->ctx, external->lock);
	nn_bool_t truthy = external->table.isRobot(external->table.userdata, computer, err);
	nn_unlock(&external->ctx, external->lock);

	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}

	nn_return_boolean(computer, truthy);
}

static void nni_externalComputer_getArchitecture(nn_externalComputer_t *external, void *_, nn_component *component, nn_computer *computer) {
	nn_errorbuf_t err = "";

	nn_lock(&external->ctx, external->lock);
	nn_architecture *arch = external->table.getArchitecture(external->table.userdata, computer, err);
	nn_unlock(&external->ctx, external->lock);

	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}

	nn_return_cstring(computer, arch->archName);
}

static void nni_externalComputer_getDeviceInfo(nn_externalComputer_t *external, void *_, nn_component *component, nn_computer *computer) {
	nn_errorbuf_t err = "";
	nn_deviceInfoList_t *info = nn_newDeviceInfoList(&external->ctx, 16);
	if(info == NULL) {
		nn_setCError(computer, "out of memory");
		return;
	}

	nn_lock(&external->ctx, external->lock);
	external->table.getDeviceInfo(external->table.userdata, info, computer, err);
	nn_unlock(&external->ctx, external->lock);

	if(!nn_error_isEmpty(err)) {
		nn_deleteDeviceInfoList(info);
		nn_setError(computer, err);
		return;
	}

	nn_size_t deviceCount = nn_getDeviceCount(info);
	nn_value devicesSerialized = nn_return_table(computer, deviceCount);

	for(nn_size_t i = 0; i < deviceCount; i++) {
		nn_deviceInfo_t *device = nn_getDeviceInfoAt(info, i);
		nn_size_t deviceInfoSize = nn_getDeviceKeyCount(device);

		nn_value deviceTable = nn_values_table(&external->ctx.allocator, deviceInfoSize);
	
		for(nn_size_t j = 0; j < deviceInfoSize; j++) {
			const char *value = NULL;
			const char *key = nn_iterateDeviceInfoKeys(device, j, &value);

			nn_values_setPair(
				deviceTable,
				j,
				nn_values_string(&external->ctx.allocator, key, nn_strlen(key)),
				nn_values_string(&external->ctx.allocator, value, nn_strlen(value))
			);
		}

		const char *addr = nn_getDeviceInfoAddress(device);
		nn_values_setPair(
			devicesSerialized,
			i,
			nn_values_string(&external->ctx.allocator, addr, nn_strlen(addr)),
			deviceTable
		);
	}

	nn_deleteDeviceInfoList(info);
}

void nn_loadExternalComputerTable(nn_universe *universe) {
    nn_componentTable *computerTable = nn_newComponentTable(nn_getAllocator(universe), "modem", NULL, NULL, (nn_componentDestructor *)nni_externalComputer_componentDestroy);
    nn_storeUserdata(universe, "NN:COMPUTER", computerTable);

	nn_method_t *method;
	method = nn_defineMethod(computerTable, "start", (nn_componentMethod *)nni_externalComputer_start, "start(): boolean - Starts the computer. Returns whether it was successful");
	nn_method_setDirect(method, false);
	method = nn_defineMethod(computerTable, "stop", (nn_componentMethod *)nni_externalComputer_stop, "stop(): boolean - Stops the computer. Returns whether it was successful");
	nn_method_setDirect(method, false);
	method = nn_defineMethod(computerTable, "isRunning", (nn_componentMethod *)nni_externalComputer_isRunning, "isRunning(): boolean - Returns whether the computer was running");
	nn_method_setDirect(method, false);
	method = nn_defineMethod(computerTable, "isRobot", (nn_componentMethod *)nni_externalComputer_isRobot, "isRobot(): boolean - Returns whether the computer was running");
	nn_method_setDirect(method, false);
	method = nn_defineMethod(computerTable, "getArchitecture", (nn_componentMethod *)nni_externalComputer_getArchitecture, "getArchitecture(): string - Returns the name of the architecture of the computer");
	nn_method_setDirect(method, false);
	method = nn_defineMethod(computerTable, "getDeviceInfo", (nn_componentMethod *)nni_externalComputer_getDeviceInfo, "getDeviceList(): {[string]: {[string]: string}} - Returns information about the devices connected to the computer");
	nn_method_setDirect(method, false);
}

nn_component *nn_externalComputer_addTo(nn_computer *computer, nn_address address, int slot, nn_externalComputer_t *external) {
    nn_componentTable *computerTable = nn_queryUserdata(nn_getUniverse(computer), "NN:COMPUTER");
    return nn_newComponent(computer, address, slot, computerTable, external);
}
