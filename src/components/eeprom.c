#include "../neonucleus.h"

nn_eepromControl nn_eeprom_getControl(nn_component *component, nn_eeprom *eeprom) {
    return eeprom->control(component, eeprom->userdata);
}

static void nn_eeprom_readCost(nn_component *component, size_t bytesRead) {
    nn_eepromControl control = nn_eeprom_getControl(component, nn_getComponentUserdata(component));
    nn_computer *computer = nn_getComputerOfComponent(component);

    nn_removeEnergy(computer, control.readEnergyCostPerByte * bytesRead);
    nn_addHeat(computer, control.readHeatPerByte * bytesRead);
    nn_simulateBufferedIndirect(component, bytesRead, control.bytesReadPerTick);
}

static void nn_eeprom_writeCost(nn_component *component, size_t bytesWritten) {
    nn_eepromControl control = nn_eeprom_getControl(component, nn_getComponentUserdata(component));
    nn_computer *computer = nn_getComputerOfComponent(component);

    nn_removeEnergy(computer, control.writeEnergyCostPerByte * bytesWritten);
    nn_addHeat(computer, control.writeHeatPerByte * bytesWritten);
    nn_simulateBufferedIndirect(component, bytesWritten, control.bytesWrittenPerTick);
}

void nn_eeprom_destroy(void *_, nn_component *component, nn_eeprom *eeprom) {
    if(!nn_decRef(&eeprom->refc)) return;

    if(eeprom->deinit != NULL) {
        eeprom->deinit(component, eeprom->userdata);
    }
}

void nn_eeprom_getSize(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    nn_return(computer, nn_values_integer(eeprom->getSize(component, eeprom->userdata)));
}

void nn_eeprom_getDataSize(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    nn_return(computer, nn_values_integer(eeprom->getDataSize(component, eeprom->userdata)));
}

void nn_eeprom_getLabel(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    char buf[NN_LABEL_SIZE];
    size_t l = NN_LABEL_SIZE;
    eeprom->getLabel(component, eeprom->userdata, buf, &l);
    if(l == 0) {
        nn_return(computer, nn_values_nil());
    } else {
        nn_return_string(computer, buf, l);
    }
    
    // Latency, energy costs and stuff
    nn_eeprom_readCost(component, l);
}

void nn_eeprom_setLabel(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    size_t l = 0;
    nn_value label = nn_getArgument(computer, 0);
    const char *buf = nn_toString(label, &l);
    if(buf == NULL) {
        nn_setCError(computer, "bad label (string expected)");
        return;
    }
    l = eeprom->setLabel(component, eeprom->userdata, buf, l);
    nn_return_string(computer, buf, l);
    
    // Latency, energy costs and stuff
    nn_eeprom_writeCost(component, l);
}

void nn_eeprom_get(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    size_t cap = eeprom->getSize(component, eeprom->userdata);
    nn_Alloc *alloc = nn_getAllocator(nn_getUniverse(computer));
    char *buf = nn_alloc(alloc, cap);
    if(buf == NULL) {
        nn_setCError(computer, "out of memory");
        return;
    }
    size_t len = eeprom->get(component, eeprom->userdata, buf);
    nn_return_string(computer, buf, len);
    nn_dealloc(alloc, buf, cap);

    nn_eeprom_readCost(component, len);
}

void nn_eeprom_set(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    nn_value data = nn_getArgument(computer, 0);
    size_t len;
    const char *buf = nn_toString(data, &len);
    if(len > eeprom->getSize(component, eeprom->userdata)) {
        nn_setCError(computer, "out of space");
        return;
    }
    if(buf == NULL) {
        if(data.tag == NN_VALUE_NIL) {
            buf = "";
            len = 0;
        } else {
            nn_setCError(computer, "bad data (string expected)");
            return;
        }
    }
    eeprom->set(component, eeprom->userdata, buf, len);
    
    nn_eeprom_writeCost(component, len);
}

void nn_eeprom_getData(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    size_t cap = eeprom->getDataSize(component, eeprom->userdata);
    nn_Alloc *alloc = nn_getAllocator(nn_getUniverse(computer));
    char *buf = nn_alloc(alloc, cap);
    if(buf == NULL) {
        nn_setCError(computer, "out of memory");
        return;
    }
    int len = eeprom->getData(component, eeprom->userdata, buf);
    if(len < 0) {
        nn_return(computer, nn_values_nil());
    } else {
        nn_return_string(computer, buf, len);
    }
    nn_dealloc(alloc, buf, cap);
    
    nn_eeprom_readCost(component, len);
}

void nn_eeprom_setData(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    nn_value data = nn_getArgument(computer, 0);
    size_t len = 0;
    const char *buf = nn_toString(data, &len);
    if(buf == NULL) {
        if(data.tag == NN_VALUE_NIL) {
            buf = "";
            len = 0;
        } else {
            nn_setCError(computer, "bad data (string expected)");
            return;
        }
    }
    if(len > eeprom->getDataSize(component, eeprom->userdata)) {
        nn_setCError(computer, "out of space");
        return;
    }
    eeprom->setData(component, eeprom->userdata, buf, len);

    nn_eeprom_writeCost(component, len);
}

void nn_eeprom_isReadOnly(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    nn_return(computer, nn_values_boolean(eeprom->isReadonly(component, eeprom->userdata)));
}

void nn_eeprom_makeReadonly(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    eeprom->makeReadonly(component, eeprom->userdata);
}

void nn_eeprom_getChecksum(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    size_t cap = eeprom->getDataSize(component, eeprom->userdata);
    nn_Alloc *alloc = nn_getAllocator(nn_getUniverse(computer));
    char *buf = nn_alloc(alloc, cap);
    if(buf == NULL) {
        nn_setCError(computer, "out of memory");
        return;
    }
    int len = eeprom->getData(component, eeprom->userdata, buf);
    if(len < 0) {
        return;
    }
    char hash[4] = {1, 0, 0, 1};
    nn_data_crc32(buf, len, hash);
    nn_dealloc(alloc, buf, cap);

    nn_return_string(computer, hash, sizeof(hash));
    
    nn_eeprom_readCost(component, len);
}

void nn_loadEepromTable(nn_universe *universe) {
    nn_componentTable *eepromTable = nn_newComponentTable(nn_getAllocator(universe), "eeprom", NULL, NULL, (void *)nn_eeprom_destroy);
    nn_storeUserdata(universe, "NN:EEPROM", eepromTable);

    nn_defineMethod(eepromTable, "getSize", true, (void *)nn_eeprom_getSize, NULL, "getSize(): integer - Returns the maximum code capacity of the EEPROM.");
    nn_defineMethod(eepromTable, "getDataSize", true, (void *)nn_eeprom_getDataSize, NULL, "getDataSize(): integer - Returns the maximum data capacity of the EEPROM.");
    nn_defineMethod(eepromTable, "getLabel", true, (void *)nn_eeprom_getLabel, NULL, "getLabel(): string - Returns the current label.");
    nn_defineMethod(eepromTable, "setLabel", true, (void *)nn_eeprom_setLabel, NULL, "setLabel(label: string): string - Sets the new label. Returns the actual label set to, which may be truncated.");
    nn_defineMethod(eepromTable, "get", true, (void *)nn_eeprom_get, NULL, "get(): string - Reads the current code contents.");
    nn_defineMethod(eepromTable, "set", true, (void *)nn_eeprom_set, NULL, "set(data: string) - Sets the current code contents.");
    nn_defineMethod(eepromTable, "getData", true, (void *)nn_eeprom_getData, NULL, "getData(): string - Reads the current data contents.");
    nn_defineMethod(eepromTable, "setData", true, (void *)nn_eeprom_setData, NULL, "setData(data: string) - Sets the current data contents.");
    nn_defineMethod(eepromTable, "isReadOnly", true, (void *)nn_eeprom_isReadOnly, NULL, "isReadOnly(): boolean - Returns whether this EEPROM is read-only.");
    nn_defineMethod(eepromTable, "makeReadOnly", false, (void *)nn_eeprom_makeReadonly, NULL, "makeReadOnly() - Makes the current EEPROM read-only. Normally, this cannot be undone.");
    nn_defineMethod(eepromTable, "makeReadonly", false, (void *)nn_eeprom_makeReadonly, NULL, "makeReadonly() - Legacy alias to makeReadOnly()");
    nn_defineMethod(eepromTable, "getChecksum", true, (void *)nn_eeprom_getChecksum, NULL, "getChecksum(): string - Returns a checksum of the data on the EEPROM.");
}

nn_component *nn_addEeprom(nn_computer *computer, nn_address address, int slot, nn_eeprom *eeprom) {
    nn_componentTable *eepromTable = nn_queryUserdata(nn_getUniverse(computer), "NN:EEPROM");

    return nn_newComponent(computer, address, slot, eepromTable, eeprom);
}
