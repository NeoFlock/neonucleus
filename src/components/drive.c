#include "../neonucleus.h"

void nn_drive_destroy(void *_, nn_component *component, nn_drive *drive) {
    if(!nn_decRef(&drive->refc)) return;

    if(drive->deinit != NULL) {
        drive->deinit(component, drive->userdata);
    }
}

nn_driveControl nn_drive_getControl(nn_component *component, nn_drive *drive) {
    return drive->control(component, drive->userdata);
}

void nn_drive_getLabel(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    char buf[NN_LABEL_SIZE];
    nn_size_t l = NN_LABEL_SIZE;
    drive->getLabel(component, drive->userdata, buf, &l);
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
    l = drive->setLabel(component, drive->userdata, buf, l);
    nn_return_string(computer, buf, l);
}
void nn_drive_getSectorSize(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t sector_size = drive->getSectorSize(component, drive->userdata);
    nn_return(computer, nn_values_integer(sector_size));
}
void nn_drive_getPlatterCount(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t platter_count = drive->getPlatterCount(component, drive->userdata);
    nn_return(computer, nn_values_integer(platter_count));
}
void nn_drive_getCapacity(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t capacity = drive->getCapacity(component, drive->userdata);
    nn_return(computer, nn_values_integer(capacity));
}
void nn_drive_readSector(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value sectorValue = nn_getArgument(computer, 0);
    int sector = nn_toInt(sectorValue);
    nn_size_t sector_size = drive->getSectorSize(component, drive->userdata);
    // we leave the +1 intentionally to compare the end of the real sector
    if (sector < 1 || (sector * sector_size > drive->getCapacity(component, drive->userdata))) {
        nn_setCError(computer, "bad argument #1 (sector out of range)");
        return;
    }
    char buf[sector_size];
    drive->readSector(component, drive->userdata, sector, buf);
    nn_return_string(computer, buf, sector_size);
}
void nn_drive_writeSector(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value sectorValue = nn_getArgument(computer, 0);
    int sector = nn_toInt(sectorValue);
    nn_size_t sector_size = drive->getSectorSize(component, drive->userdata);
    nn_value bufValue = nn_getArgument(computer, 1);

    nn_size_t buf_size = 0;
    const char *buf = nn_toString(bufValue, &buf_size);
    if (buf_size != sector_size) {
        nn_setCError(computer, "bad argument #2 (expected buffer of length `sectorSize`)");
        return;
    }
    // we leave the +1 intentionally to compare the end of the real sector
    if (sector < 1 || (sector * sector_size > drive->getCapacity(component, drive->userdata))) {
        nn_setCError(computer, "bad argument #1 (sector out of range)");
        return;
    }
    drive->writeSector(component, drive->userdata, sector, buf);
}
void nn_drive_readByte(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value offsetValue = nn_getArgument(computer, 0);
    nn_size_t disk_offset = nn_toInt(offsetValue) - 1;
    nn_size_t sector_size = drive->getSectorSize(component, drive->userdata);
    int sector = (disk_offset / sector_size) + 1;
    nn_size_t sector_offset = disk_offset % sector_size;

    if (disk_offset >= drive->getCapacity(component, drive->userdata)) {
        nn_setCError(computer, "bad argument #1 (index out of range)");
        return;
    }
    char buf[sector_size];
    drive->readSector(component, drive->userdata, sector, buf);

    nn_return(computer, nn_values_integer(buf[sector_offset]));
}
void nn_drive_writeByte(nn_drive *drive, void *_, nn_component *component, nn_computer *computer) {
    nn_value offsetValue = nn_getArgument(computer, 0);
    nn_value writeValue = nn_getArgument(computer, 1);
    nn_size_t disk_offset = nn_toInt(offsetValue) - 1;
    nn_intptr_t write = nn_toInt(writeValue);
    nn_size_t sector_size = drive->getSectorSize(component, drive->userdata);
    int sector = (disk_offset / sector_size) + 1;
    nn_size_t sector_offset = disk_offset % sector_size;

    if (write < -128 || write > 255) {
        nn_setCError(computer, "bad argument #2 (byte out of range)");
        return;
    }
    if (disk_offset >= drive->getCapacity(component, drive->userdata)) {
        nn_setCError(computer, "bad argument #1 (index out of range)");
        return;
    }
    char buf[sector_size];
    drive->readSector(component, drive->userdata, sector, buf);

    buf[sector_offset] = write;

    drive->writeSector(component, drive->userdata, sector, buf);
}

void nn_loadDriveTable(nn_universe *universe) {
    nn_componentTable *driveTable = nn_newComponentTable(nn_getAllocator(universe), "drive", NULL, NULL, (void *)nn_drive_destroy);
    nn_storeUserdata(universe, "NN:DRIVE", driveTable);

    nn_defineMethod(driveTable, "getLabel", false, (void *)nn_drive_getLabel, NULL, "getLabel():string - Get the current label of the drive.");
    nn_defineMethod(driveTable, "setLabel", false, (void *)nn_drive_setLabel, NULL, "setLabel(value:string):string - Sets the label of the drive. Returns the new value, which may be truncated.");
    nn_defineMethod(driveTable, "getSectorSize", true, (void *)nn_drive_getSectorSize, NULL, "getSectorSize():number - Returns the size of a single sector on the drive, in bytes.");
    nn_defineMethod(driveTable, "getPlatterCounter", true, (void *)nn_drive_getPlatterCount, NULL, "getPlatterCount():number - Returns the number of platters in the drive.");
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

