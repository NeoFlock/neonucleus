#include "../neonucleus.h"

typedef struct nn_drive {
    nn_refc refc;
    nn_guard *lock;
    nn_Context ctx;
    nn_size_t currentSector;
    nn_driveTable table;
    nn_driveControl ctrl;
} nn_drive;

nn_drive *nn_newDrive(nn_Context *context, nn_driveTable table, nn_driveControl control) {
    nn_drive *d = nn_alloc(&context->allocator, sizeof(nn_drive));
    if(d == NULL) return NULL;
    d->lock = nn_newGuard(context);
    if(d->lock == NULL) {
        nn_dealloc(&context->allocator, d, sizeof(nn_drive));
        return NULL;
    }
    d->refc = 1;
    d->table = table;
    d->ctrl = control;
    d->currentSector = 0;
    d->ctx = *context;
    return d;
}

nn_guard *nn_getDriveLock(nn_drive *drive) {
    return drive->lock;
}

void nn_retainDrive(nn_drive *drive) {
    nn_incRef(&drive->refc);
}

nn_bool_t nn_destroyDrive(nn_drive *drive) {
    if(!nn_decRef(&drive->refc)) return false;

    if(drive->table.deinit != NULL) {
        drive->table.deinit(drive->table.userdata);
    }

    nn_Context ctx = drive->ctx;

    nn_deleteGuard(&ctx, drive->lock);
    nn_dealloc(&ctx.allocator, drive, sizeof(nn_drive));
    return true;
}

void nn_drive_destroy(void *_, nn_component *component, nn_drive *drive) {
    nn_destroyDrive(drive);
}

void nni_drive_readCost(nn_component *component, nn_drive *drive) {
    nn_driveControl ctrl = drive->ctrl;
    nn_computer *computer = nn_getComputerOfComponent(component);

    nn_simulateBufferedIndirect(component, 1, ctrl.readSectorsPerTick);
    nn_addHeat(computer, ctrl.readHeatPerSector);
    nn_removeEnergy(computer, ctrl.readEnergyPerSector);
}

void nni_drive_writeCost(nn_component *component, nn_drive *drive) {
    nn_driveControl ctrl = drive->ctrl;
    nn_computer *computer = nn_getComputerOfComponent(component);

    nn_simulateBufferedIndirect(component, 1, ctrl.writeSectorsPerTick);
    nn_addHeat(computer, ctrl.writeHeatPerSector);
    nn_removeEnergy(computer, ctrl.writeEnergyPerSector);
}

void nni_drive_seekTo(nn_component *component, nn_drive *drive, nn_size_t sector) {
    sector = sector - 1; // switch to 0 to N-1 sector addressing,
                         // which is much nicer to do math with
                         // and Lua made a big oopsie.
    nn_size_t old = drive->currentSector;
    nn_driveControl ctrl = drive->ctrl;
    if(ctrl.seekSectorsPerTick == 0) return; // seek latency disabled
    nn_computer *computer = nn_getComputerOfComponent(component);

    // Compute important constants
    nn_size_t sectorSize = drive->table.sectorSize;
    nn_size_t platterCount = drive->table.platterCount;
    nn_size_t capacity = drive->table.capacity;

    nn_size_t sectorsPerPlatter = (capacity / sectorSize) / platterCount;

    sector %= sectorsPerPlatter;
    
    if(old == sector) return;
    drive->currentSector = sector;

    nn_size_t moved = 0;
    if(sector > old) {
        moved = sector - old; // moved forwards
    } else if(sector < old) {
        // moved back, depends on some options
        if(ctrl.reversable) {
            // horribly fucking unrealistic, as HDDs
            // spin at ~7200 RPM, and if it decides
            // to spontaneously instantly reverse direction,
            // the force would crack the disk
            // However, real life is a myth.
            moved = old - sector;
        } else {
            // full turn
            moved = sectorsPerPlatter - old;
        }
    }
    
    nn_simulateBufferedIndirect(component, moved, ctrl.seekSectorsPerTick);
    nn_addHeat(computer, ctrl.motorHeatPerSector * moved);
    nn_removeEnergy(computer, ctrl.motorEnergyPerSector * moved);
}

void nn_drive_getLabel(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    char buf[NN_LABEL_SIZE];
    nn_size_t l = NN_LABEL_SIZE;
    nn_lock(&drive->ctx, drive->lock);
    drive->table.getLabel(drive->table.userdata, buf, &l);
    nn_unlock(&drive->ctx, drive->lock);
    if(l == 0) {
        nn_return(computer, nn_values_nil());
    } else {
        nn_return_string(computer, buf, l);
    }
}
void nn_drive_setLabel(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t l = 0;
    nn_value label = nn_getArgument(computer, 0);
    const char *buf = nn_toString(label, &l);
    if(buf == NULL) {
        nn_setCError(computer, "bad label (string expected)");
        return;
    }
    nn_lock(&drive->ctx, drive->lock);
    l = drive->table.setLabel(drive->table.userdata, buf, l);
    nn_unlock(&drive->ctx, drive->lock);
    nn_return_string(computer, buf, l);
}
void nn_drive_getSectorSize(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t sector_size = drive->table.sectorSize;
    nn_return(computer, nn_values_integer(sector_size));
}
void nn_drive_getPlatterCount(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t platter_count = drive->table.platterCount;
    nn_return(computer, nn_values_integer(platter_count));
}
void nn_drive_getCapacity(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t capacity = drive->table.capacity;
    nn_return(computer, nn_values_integer(capacity));
}
void nn_drive_readSector(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value sectorValue = nn_getArgument(computer, 0);
    int sector = nn_toInt(sectorValue);
    nn_size_t sector_size = drive->table.sectorSize;
    // we leave the +1 intentionally to compare the end of the real sector
    if (sector < 1 || (sector * sector_size > drive->table.capacity)) {
        nn_setCError(computer, "bad argument #1 (sector out of range)");
        return;
    }
    char buf[sector_size];
    nn_lock(&drive->ctx, drive->lock);
    drive->table.readSector(drive->table.userdata, sector, buf);
    nn_unlock(&drive->ctx, drive->lock);
    nn_return_string(computer, buf, sector_size);
}
void nn_drive_writeSector(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value sectorValue = nn_getArgument(computer, 0);
    int sector = nn_toInt(sectorValue);
    nn_size_t sector_size = drive->table.sectorSize;
    nn_value bufValue = nn_getArgument(computer, 1);

    nn_size_t buf_size = 0;
    const char *buf = nn_toString(bufValue, &buf_size);
    if (buf_size != sector_size) {
        nn_setCError(computer, "bad argument #2 (expected buffer of length `sectorSize`)");
        return;
    }
    // we leave the +1 intentionally to compare the end of the real sector
    if (sector < 1 || (sector * sector_size > drive->table.capacity)) {
        nn_setCError(computer, "bad argument #1 (sector out of range)");
        return;
    }
    nn_lock(&drive->ctx, drive->lock);
    drive->table.writeSector(drive->table.userdata, sector, buf);
    nn_unlock(&drive->ctx, drive->lock);

    nn_return_boolean(computer, true);
}
void nn_drive_readByte(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value offsetValue = nn_getArgument(computer, 0);
    nn_size_t disk_offset = nn_toInt(offsetValue) - 1;
    nn_size_t sector_size = drive->table.sectorSize;
    int sector = (disk_offset / sector_size) + 1;
    nn_size_t sector_offset = disk_offset % sector_size;

    if (disk_offset >= drive->table.capacity) {
        nn_setCError(computer, "bad argument #1 (index out of range)");
        return;
    }
    char buf[sector_size];
    nn_lock(&drive->ctx, drive->lock);
    drive->table.readSector(drive->table.userdata, sector, buf);
    nn_unlock(&drive->ctx, drive->lock);

    nn_return(computer, nn_values_integer(buf[sector_offset]));
}
void nn_drive_writeByte(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value offsetValue = nn_getArgument(computer, 0);
    nn_value writeValue = nn_getArgument(computer, 1);
    nn_size_t disk_offset = nn_toInt(offsetValue) - 1;
    nn_intptr_t write = nn_toInt(writeValue);
    nn_size_t sector_size = drive->table.sectorSize;
    int sector = (disk_offset / sector_size) + 1;
    nn_size_t sector_offset = disk_offset % sector_size;

    if (write < -128 || write > 255) {
        nn_setCError(computer, "bad argument #2 (byte out of range)");
        return;
    }
    if (disk_offset >= drive->table.capacity) {
        nn_setCError(computer, "bad argument #1 (index out of range)");
        return;
    }
    nn_lock(&drive->ctx, drive->lock);
    char buf[sector_size];
    drive->table.readSector(drive->table.userdata, sector, buf);

    buf[sector_offset] = write;

    drive->table.writeSector(drive->table.userdata, sector, buf);
    nn_unlock(&drive->ctx, drive->lock);

    nn_return_boolean(computer, true);
}

void nn_loadDriveTable(nn_universe *universe) {
    nn_componentTable *driveTable = nn_newComponentTable(nn_getAllocator(universe), "drive", NULL, NULL, (void *)nn_drive_destroy);
    nn_storeUserdata(universe, "NN:DRIVE", driveTable);

    nn_defineMethod(driveTable, "getLabel", false, (void *)nn_drive_getLabel, NULL, "getLabel():string - Get the current label of the drive.");
    nn_defineMethod(driveTable, "setLabel", false, (void *)nn_drive_setLabel, NULL, "setLabel(value:string):string - Sets the label of the drive. Returns the new value, which may be truncated.");
    nn_defineMethod(driveTable, "getSectorSize", true, (void *)nn_drive_getSectorSize, NULL, "getSectorSize():number - Returns the size of a single sector on the drive, in bytes.");
    nn_defineMethod(driveTable, "getPlatterCount", true, (void *)nn_drive_getPlatterCount, NULL, "getPlatterCount():number - Returns the number of platters in the drive.");
    nn_defineMethod(driveTable, "getCapacity", true, (void *)nn_drive_getCapacity, NULL, "getCapacity():number - Returns the total capacity of the drive, in bytes.");
    nn_defineMethod(driveTable, "readSector", false, (void *)nn_drive_readSector, NULL, "readSector(sector:number):string - Read the current contents of the specified sector.");
    nn_defineMethod(driveTable, "writeSector", false, (void *)nn_drive_writeSector, NULL, "writeSector(sector:number, value:string) - Write the specified contents to the specified sector.");
    nn_defineMethod(driveTable, "readByte", false, (void *)nn_drive_readByte, NULL, "readByte(offset:number):number - Read a single byte at the specified offset.");
    nn_defineMethod(driveTable, "writeByte", false, (void *)nn_drive_writeByte, NULL, "writeByte(offset:number, value:number) - Write a single byte to the specified offset.");
}

nn_component *nn_addDrive(nn_computer *computer, nn_address address, int slot, nn_drive *drive) {
    nn_componentTable *driveTable = nn_queryUserdata(nn_getUniverse(computer), "NN:DRIVE");
    return nn_newComponent(computer, address, slot, driveTable, drive);
}

