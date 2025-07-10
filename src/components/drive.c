#include "../neonucleus.h"

typedef struct nni_drive {
    nn_drive *funcs;
    nn_size_t currentSector;
} nni_drive;

void nn_drive_destroy(void *_, nn_component *component, nni_drive *drive) {
    if(!nn_decRef(&drive->funcs->refc)) return;

    if(drive->funcs->deinit != NULL) {
        drive->funcs->deinit(component, drive->funcs->userdata);
    }

    nn_Alloc *alloc = nn_getAllocator(nn_getUniverse(nn_getComputerOfComponent(component)));

    nn_dealloc(alloc, drive, sizeof(nni_drive));
}

nn_driveControl nn_drive_getControl(nn_component *component, nni_drive *drive) {
    return drive->funcs->control(component, drive->funcs->userdata);
}

void nni_drive_readCost(nn_component *component, nni_drive *drive) {
    nn_driveControl ctrl = nn_drive_getControl(component, drive);
    nn_computer *computer = nn_getComputerOfComponent(component);

    nn_simulateBufferedIndirect(component, 1, ctrl.readSectorsPerTick);
    nn_addHeat(computer, ctrl.readHeatPerSector);
    nn_removeEnergy(computer, ctrl.readEnergyPerSector);
}

void nni_drive_writeCost(nn_component *component, nni_drive *drive) {
    nn_driveControl ctrl = nn_drive_getControl(component, drive);
    nn_computer *computer = nn_getComputerOfComponent(component);

    nn_simulateBufferedIndirect(component, 1, ctrl.writeSectorsPerTick);
    nn_addHeat(computer, ctrl.writeHeatPerSector);
    nn_removeEnergy(computer, ctrl.writeEnergyPerSector);
}

void nni_drive_seekTo(nn_component *component, nni_drive *drive, nn_size_t sector) {
    sector = sector - 1; // switch to 0 to N-1 sector addressing,
                         // which is much nicer to do math with
                         // and Lua made a big oopsie.
    nn_size_t old = drive->currentSector;
    nn_driveControl ctrl = nn_drive_getControl(component, drive);
    if(ctrl.seekSectorsPerTick == 0) return; // seek latency disabled
    nn_computer *computer = nn_getComputerOfComponent(component);

    // Compute important constants
    nn_size_t sectorSize = drive->funcs->getSectorSize(component, drive->funcs->userdata);
    nn_size_t platterCount = drive->funcs->getPlatterCount(component, drive->funcs->userdata);
    nn_size_t capacity = drive->funcs->getCapacity(component, drive->funcs->userdata);

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

void nn_drive_getLabel(nni_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    char buf[NN_LABEL_SIZE];
    nn_size_t l = NN_LABEL_SIZE;
    drive->funcs->getLabel(component, drive->funcs->userdata, buf, &l);
    if(l == 0) {
        nn_return(computer, nn_values_nil());
    } else {
        nn_return_string(computer, buf, l);
    }
}
void nn_drive_setLabel(nni_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t l = 0;
    nn_value label = nn_getArgument(computer, 0);
    const char *buf = nn_toString(label, &l);
    if(buf == NULL) {
        nn_setCError(computer, "bad label (string expected)");
        return;
    }
    l = drive->funcs->setLabel(component, drive->funcs->userdata, buf, l);
    nn_return_string(computer, buf, l);
}
void nn_drive_getSectorSize(nni_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t sector_size = drive->funcs->getSectorSize(component, drive->funcs->userdata);
    nn_return(computer, nn_values_integer(sector_size));
}
void nn_drive_getPlatterCount(nni_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t platter_count = drive->funcs->getPlatterCount(component, drive->funcs->userdata);
    nn_return(computer, nn_values_integer(platter_count));
}
void nn_drive_getCapacity(nni_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t capacity = drive->funcs->getCapacity(component, drive->funcs->userdata);
    nn_return(computer, nn_values_integer(capacity));
}
void nn_drive_readSector(nni_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value sectorValue = nn_getArgument(computer, 0);
    int sector = nn_toInt(sectorValue);
    nn_size_t sector_size = drive->funcs->getSectorSize(component, drive->funcs->userdata);
    // we leave the +1 intentionally to compare the end of the real sector
    if (sector < 1 || (sector * sector_size > drive->funcs->getCapacity(component, drive->funcs->userdata))) {
        nn_setCError(computer, "bad argument #1 (sector out of range)");
        return;
    }
    char buf[sector_size];
    drive->funcs->readSector(component, drive->funcs->userdata, sector, buf);
    nn_return_string(computer, buf, sector_size);
}
void nn_drive_writeSector(nni_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value sectorValue = nn_getArgument(computer, 0);
    int sector = nn_toInt(sectorValue);
    nn_size_t sector_size = drive->funcs->getSectorSize(component, drive->funcs->userdata);
    nn_value bufValue = nn_getArgument(computer, 1);

    nn_size_t buf_size = 0;
    const char *buf = nn_toString(bufValue, &buf_size);
    if (buf_size != sector_size) {
        nn_setCError(computer, "bad argument #2 (expected buffer of length `sectorSize`)");
        return;
    }
    // we leave the +1 intentionally to compare the end of the real sector
    if (sector < 1 || (sector * sector_size > drive->funcs->getCapacity(component, drive->funcs->userdata))) {
        nn_setCError(computer, "bad argument #1 (sector out of range)");
        return;
    }
    drive->funcs->writeSector(component, drive->funcs->userdata, sector, buf);
}
void nn_drive_readByte(nni_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value offsetValue = nn_getArgument(computer, 0);
    nn_size_t disk_offset = nn_toInt(offsetValue) - 1;
    nn_size_t sector_size = drive->funcs->getSectorSize(component, drive->funcs->userdata);
    int sector = (disk_offset / sector_size) + 1;
    nn_size_t sector_offset = disk_offset % sector_size;

    if (disk_offset >= drive->funcs->getCapacity(component, drive->funcs->userdata)) {
        nn_setCError(computer, "bad argument #1 (index out of range)");
        return;
    }
    char buf[sector_size];
    drive->funcs->readSector(component, drive->funcs->userdata, sector, buf);

    nn_return(computer, nn_values_integer(buf[sector_offset]));
}
void nn_drive_writeByte(nni_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value offsetValue = nn_getArgument(computer, 0);
    nn_value writeValue = nn_getArgument(computer, 1);
    nn_size_t disk_offset = nn_toInt(offsetValue) - 1;
    nn_intptr_t write = nn_toInt(writeValue);
    nn_size_t sector_size = drive->funcs->getSectorSize(component, drive->funcs->userdata);
    int sector = (disk_offset / sector_size) + 1;
    nn_size_t sector_offset = disk_offset % sector_size;

    if (write < -128 || write > 255) {
        nn_setCError(computer, "bad argument #2 (byte out of range)");
        return;
    }
    if (disk_offset >= drive->funcs->getCapacity(component, drive->funcs->userdata)) {
        nn_setCError(computer, "bad argument #1 (index out of range)");
        return;
    }
    char buf[sector_size];
    drive->funcs->readSector(component, drive->funcs->userdata, sector, buf);

    buf[sector_offset] = write;

    drive->funcs->writeSector(component, drive->funcs->userdata, sector, buf);
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
    nn_Alloc *alloc = nn_getAllocator(nn_getUniverse(computer));
    nni_drive *d = nn_alloc(alloc, sizeof(nni_drive));
    d->currentSector = 0;
    d->funcs = drive;
    nn_componentTable *driveTable = nn_queryUserdata(nn_getUniverse(computer), "NN:DRIVE");
    nn_component *c = nn_newComponent(computer, address, slot, driveTable, drive);
    if(c == NULL) {
        nn_dealloc(alloc, d, sizeof(nni_drive));
    }
    return c;
}

