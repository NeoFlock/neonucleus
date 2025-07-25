#include "../neonucleus.h"

typedef struct nn_diskDrive {
	nn_Context ctx;
	nn_guard *lock;
	nn_refc refc;
	nn_diskDriveTable table;
} nn_diskDrive;

nn_diskDrive *nn_newDiskDrive(nn_Context *context, nn_diskDriveTable table) {
	nn_diskDrive *drive = nn_alloc(&context->allocator, sizeof(nn_diskDrive));
	if(drive == NULL) return NULL;
	drive->lock = nn_newGuard(context);
	if(drive->lock == NULL) {
		nn_dealloc(&context->allocator, drive, sizeof(nn_diskDrive));
	}
	drive->refc = 1;
	drive->table = table;
	drive->ctx = *context;
	return drive;
}

nn_guard *nn_getDiskDriveLock(nn_diskDrive *diskDrive) {
	return diskDrive->lock;
}

void nn_retainDiskDrive(nn_diskDrive *diskDrive) {
	nn_incRef(&diskDrive->refc);
}

nn_bool_t nn_destroyDiskDrive(nn_diskDrive *diskDrive) {
	if(!nn_decRef(&diskDrive->refc)) return false;
	if(diskDrive->table.deinit != NULL) {
		diskDrive->table.deinit(diskDrive->table.userdata);
	}
	nn_Context ctx = diskDrive->ctx;
	nn_Alloc a = ctx.allocator;

	nn_deleteGuard(&ctx, diskDrive->lock);
	nn_dealloc(&a, diskDrive, sizeof(nn_diskDrive));
	return true;
}

void nn_diskDrive_destroy(void *_, nn_component *component, nn_diskDrive *diskDrive) {
    nn_destroyDiskDrive(diskDrive);
}

void nn_diskDrive_eject(nn_diskDrive *diskDrive, void *_, nn_component *component, nn_computer *computer) {
	double velocity = nn_toNumberOr(nn_getArgument(computer, 0), 0);

	nn_errorbuf_t err = "";
	nn_lock(&diskDrive->ctx, diskDrive->lock);
	if(diskDrive->table.isEmpty(diskDrive->table.userdata)) {
		nn_unlock(&diskDrive->ctx, diskDrive->lock);
		nn_return_boolean(computer, false);
		return;
	}
	diskDrive->table.eject(diskDrive->table.userdata, velocity, err);
	nn_unlock(&diskDrive->ctx, diskDrive->lock);

	if(!nn_error_isEmpty(err)) {
		nn_setError(computer, err);
		return;
	}

	nn_return_boolean(computer, true);
}

void nn_diskDrive_isEmpty(nn_diskDrive *diskDrive, void *_, nn_component *component, nn_computer *computer) {
	nn_lock(&diskDrive->ctx, diskDrive->lock);
	nn_bool_t empty = diskDrive->table.isEmpty(diskDrive->table.userdata);
	nn_unlock(&diskDrive->ctx, diskDrive->lock);

	nn_return_boolean(computer, empty);
}

void nn_diskDrive_media(nn_diskDrive *diskDrive, void *_, nn_component *component, nn_computer *computer) {
	nn_errorbuf_t err = "";
	nn_Alloc *a = &diskDrive->ctx.allocator;
	nn_lock(&diskDrive->ctx, diskDrive->lock);
	if(diskDrive->table.isEmpty(diskDrive->table.userdata)) {
		nn_unlock(&diskDrive->ctx, diskDrive->lock);
		nn_setCError(computer, "drive is empty");
		return;
	}
	nn_address s = diskDrive->table.media(diskDrive->table.userdata, a, err);
	nn_unlock(&diskDrive->ctx, diskDrive->lock);

	if(!nn_error_isEmpty(err)) {
		nn_deallocStr(a, s);
		nn_setError(computer, err);
		return;
	}

	nn_return_string(computer, s, nn_strlen(s));
	nn_deallocStr(a, s);
}

void nn_loadDiskDriveTable(nn_universe *universe) {
    nn_componentTable *diskDriveTable = nn_newComponentTable(nn_getAllocator(universe), "disk_drive", NULL, NULL, (void *)nn_diskDrive_destroy);
    nn_storeUserdata(universe, "NN:DISK_DRIVE", diskDriveTable);

	nn_defineMethod(diskDriveTable, "eject", (nn_componentMethod *)nn_diskDrive_eject, "eject([velocity: number]): boolean - Ejects the floopy, if present. Returns whether it was present.");
	nn_defineMethod(diskDriveTable, "isEmpty", (nn_componentMethod *)nn_diskDrive_isEmpty, "isEmpty(): boolean - Returns whether the drive is empty.");
	nn_defineMethod(diskDriveTable, "media", (nn_componentMethod *)nn_diskDrive_media, "media(): string - Returns the address of the inner floppy disk.");
}

nn_component *nn_addDiskDrive(nn_computer *computer, nn_address address, int slot, nn_diskDrive *diskDrive) {
    nn_componentTable *diskDriveTable = nn_queryUserdata(nn_getUniverse(computer), "NN:DISK_DRIVE");

    return nn_newComponent(computer, address, slot, diskDriveTable, diskDrive);
}
