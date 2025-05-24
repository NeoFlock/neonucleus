#include "../neonucleus.h"

void nn_eeprom_destroy(void *_, nn_component *component, nn_eeprom *eeprom) {
    if(atomic_fetch_sub(&eeprom->refc, 1) > 1) return;

    if(eeprom->deinit == NULL) {
        eeprom->deinit(component, eeprom);
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
        nn_return(computer, nn_values_string(buf, l));
    }
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
    nn_return(computer, nn_values_string(buf, l));
}

void nn_eeprom_get(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    size_t cap = eeprom->getSize(component, eeprom->userdata);
    char *buf = nn_malloc(cap);
    if(buf == NULL) {
        nn_setCError(computer, "out of memory");
        return;
    }
    size_t len = eeprom->get(component, eeprom->userdata, buf);
    nn_return(computer, nn_values_string(buf, len));
    nn_free(buf);
}

void nn_eeprom_set(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    nn_value data = nn_getArgument(computer, 0);
    size_t len;
    const char *buf = nn_toString(data, &len);
    if(len > eeprom->getSize(component, eeprom->userdata)) {

    }
    eeprom->set(component, eeprom->userdata, buf, len);
}

void nn_eeprom_getData(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    size_t cap = eeprom->getDataSize(component, eeprom->userdata);
    char *buf = nn_malloc(cap);
    if(buf == NULL) {
        nn_setCError(computer, "out of memory");
        return;
    }
    int len = eeprom->getData(component, eeprom->userdata, buf);
    if(len < 0) {
        nn_return(computer, nn_values_nil());
    } else {
        nn_return(computer, nn_values_string(buf, len));
    }
    nn_free(buf);
}

void nn_eeprom_setData(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    nn_value data = nn_getArgument(computer, 0);
    size_t len;
    const char *buf = nn_toString(data, &len);
    eeprom->setData(component, eeprom->userdata, buf, len);
}

void nn_eeprom_isReadOnly(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    nn_return(computer, nn_values_boolean(eeprom->isReadonly(component, eeprom->userdata)));
}

void nn_eeprom_makeReadonly(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    eeprom->makeReadonly(component, eeprom->userdata);
}

// TODO: make good
void nn_eeprom_getChecksum(nn_eeprom *eeprom, void *_, nn_component *component, nn_computer *computer) {
    size_t cap = eeprom->getDataSize(component, eeprom->userdata);
    char *buf = nn_malloc(cap);
    if(buf == NULL) {
        nn_setCError(computer, "out of memory");
        return;
    }
    size_t len = eeprom->getData(component, eeprom->userdata, buf);
    size_t sum = 0;
    for(size_t i = 0; i < len; i++) {
        sum += buf[i];
    }
    nn_free(buf);

    nn_return(computer, nn_values_string((void *)&sum, sizeof(sum)));
}

void nn_loadEepromTable(nn_universe *universe) {
    nn_componentTable *eepromTable = nn_newComponentTable("eeprom", NULL, NULL, (void *)nn_eeprom_destroy);
    nn_storeUserdata(universe, "NN:EEPROM", eepromTable);

    nn_defineMethod(eepromTable, "getSize", true, (void *)nn_eeprom_getSize, NULL, "getSize(): integer - Returns the maximum code capacity of the EEPROM.");
    nn_defineMethod(eepromTable, "getDataSize", true, (void *)nn_eeprom_getDataSize, NULL, "getDataSize(): integer - Returns the maximum data capacity of the EEPROM.");
    nn_defineMethod(eepromTable, "getLabel", false, (void *)nn_eeprom_getLabel, NULL, "getLabel(): string - Returns the current label.");
    nn_defineMethod(eepromTable, "setLabel", false, (void *)nn_eeprom_setLabel, NULL, "setLabel(label: string): string - Sets the new label. Returns the actual label set to, which may be truncated.");
    nn_defineMethod(eepromTable, "get", false, (void *)nn_eeprom_get, NULL, "get(): string - Reads the current code contents.");
    nn_defineMethod(eepromTable, "set", false, (void *)nn_eeprom_set, NULL, "set(data: string) - Sets the current code contents.");
    nn_defineMethod(eepromTable, "getData", false, (void *)nn_eeprom_getData, NULL, "getData(): string - Reads the current data contents.");
    nn_defineMethod(eepromTable, "setData", false, (void *)nn_eeprom_setData, NULL, "setData(data: string) - Sets the current data contents.");
    nn_defineMethod(eepromTable, "isReadOnly", false, (void *)nn_eeprom_isReadOnly, NULL, "isReadOnly(): boolean - Returns whether this EEPROM is read-only.");
    nn_defineMethod(eepromTable, "makeReadOnly", false, (void *)nn_eeprom_makeReadonly, NULL, "makeReadOnly() - Makes the current EEPROM read-only. Normally, this cannot be undone.");
    nn_defineMethod(eepromTable, "makeReadonly", false, (void *)nn_eeprom_makeReadonly, NULL, "makeReadonly() - Legacy alias to makeReadOnly()");
    nn_defineMethod(eepromTable, "getChecksum", false, (void *)nn_eeprom_getChecksum, NULL, "getChecksum(): string - Returns a checksum of the data on the EEPROM.");
}

nn_component *nn_addEeprom(nn_computer *computer, nn_address address, int slot, nn_eeprom *eeprom) {
    nn_componentTable *eepromTable = nn_queryUserdata(nn_getUniverse(computer), "NN:EEPROM");

    return nn_newComponent(computer, address, slot, eepromTable, eeprom);
}
