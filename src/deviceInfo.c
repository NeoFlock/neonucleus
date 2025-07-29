#include "neonucleus.h"

typedef struct nn_deviceInfoPair_t {
	char *key;
	char *value;
} nn_deviceInfoPair_t;

typedef struct nn_deviceInfo_t {
	nn_deviceInfoPair_t *pairs;
	nn_size_t len;
	nn_size_t capacity;
	char *address;
	nn_Alloc alloc;
} nn_deviceInfo_t;

typedef struct nn_deviceInfoList_t {
	nn_Context ctx;
	nn_deviceInfo_t *info;
	nn_size_t len;
	nn_size_t cap;
} nn_deviceInfoList_t;

nn_deviceInfoList_t *nn_newDeviceInfoList(nn_Context *ctx, nn_size_t preallocate) {
	nn_Alloc *alloc = &ctx->allocator;

	nn_deviceInfoList_t *list = nn_alloc(alloc, sizeof(nn_deviceInfoList_t));
	if(list == NULL) return NULL;
	list->ctx = *ctx;
	list->len = 0;
	list->cap = preallocate;
	list->info = nn_alloc(alloc, sizeof(nn_deviceInfo_t) * list->cap);
	if(list->info == NULL) {
		nn_dealloc(alloc, list, sizeof(nn_deviceInfoList_t));
		return NULL;
	}
	return list;
}

static void nn_deleteDeviceInfo(nn_Context *ctx, nn_deviceInfo_t *info) {
	nn_Alloc *alloc = &ctx->allocator;

	nn_deallocStr(alloc, info->address);
	nn_dealloc(alloc, info->pairs, sizeof(nn_deviceInfoPair_t) * info->capacity);
}

void nn_deleteDeviceInfoList(nn_deviceInfoList_t *list) {
	for(nn_size_t i = 0; i < list->len; i++) {
		nn_deleteDeviceInfo(&list->ctx, &list->info[i]);
	}

	nn_Alloc alloc = list->ctx.allocator;

	nn_dealloc(&alloc, list->info, sizeof(nn_deviceInfo_t) * list->cap);
	nn_dealloc(&alloc, list, sizeof(nn_deviceInfoList_t));
}

nn_deviceInfo_t *nn_addDeviceInfo(nn_deviceInfoList_t *list, nn_address address, nn_size_t maxKeys) {
	if(list->len == list->cap) {
		nn_size_t neededCap = list->cap;
		if(neededCap < 1) neededCap = 1;
		while(neededCap < (list->len + 1)) neededCap *= 2;

		nn_deviceInfo_t *newBuf = nn_resize(&list->ctx.allocator, list->info, sizeof(nn_deviceInfo_t) * list->cap, sizeof(nn_deviceInfo_t) * neededCap);
		if(newBuf == NULL) return NULL;
		list->cap = neededCap;
		list->info = newBuf;
	}

	nn_deviceInfoPair_t *pairs = nn_alloc(&list->ctx.allocator, sizeof(nn_deviceInfoPair_t) * maxKeys);
	if(pairs == NULL) return NULL;

	nn_size_t i = list->len;
	list->info[i] = (nn_deviceInfo_t) {
		.alloc = list->ctx.allocator,
		.pairs = pairs,
		.len = 0,
		.address = address == NULL ? nn_randomUUID(&list->ctx) : nn_strdup(&list->ctx.allocator, address), // TODO: handle OOM
		.capacity = maxKeys,
	};
	list->len++;
	return list->info + i;
}

void nn_removeDeviceInfo(nn_deviceInfoList_t *list, const char *key);

nn_bool_t nn_registerDeviceKey(nn_deviceInfo_t *deviceInfo, const char *key, const char *value) {
	if(deviceInfo->len == deviceInfo->capacity) return false;
	nn_size_t i = deviceInfo->len;
	nn_Alloc *alloc = &deviceInfo->alloc;
	// TODO: handle OOM
	deviceInfo->pairs[i].key = nn_strdup(alloc, key);
	deviceInfo->pairs[i].value = nn_strdup(alloc, value);
	deviceInfo->len++;
	return true;
}

nn_deviceInfo_t *nn_getDeviceInfoAt(nn_deviceInfoList_t *list, nn_size_t idx) {
	if(idx >= list->len) return NULL;
	return &list->info[idx];
}

const char *nn_getDeviceInfoAddress(nn_deviceInfo_t *deviceInfo) {
	return deviceInfo->address;
}

nn_size_t nn_getDeviceCount(nn_deviceInfoList_t *list) {
	return list->len;
}

const char *nn_iterateDeviceInfoKeys(nn_deviceInfo_t *deviceInfo, nn_size_t idx, const char **value) {
	if(idx >= deviceInfo->len) return NULL;
	nn_deviceInfoPair_t pair = deviceInfo->pairs[idx];
	if(value != NULL) *value = pair.value;
	return pair.key;
}

nn_size_t nn_getDeviceKeyCount(nn_deviceInfo_t *deviceInfo) {
	return deviceInfo->len;
}
