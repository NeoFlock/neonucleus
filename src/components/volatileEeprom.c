#include "../neonucleus.h"

typedef struct nn_veeprom {
    nn_Context ctx;
    char *code;
    nn_size_t codeLen;
    nn_size_t codeSize;
    char *data;
    nn_size_t dataLen;
    nn_size_t dataSize;
    char label[NN_LABEL_SIZE];
    nn_size_t labelLen;
    nn_bool_t isReadOnly;
} nn_veeprom;

static void nni_veeprom_deinit(nn_veeprom *veeprom) {
    nn_Alloc a = veeprom->ctx.allocator;
    nn_dealloc(&a, veeprom->code, veeprom->codeSize);
    nn_dealloc(&a, veeprom->data, veeprom->dataSize);
    nn_dealloc(&a, veeprom, sizeof(nn_veeprom));
}

static void nni_veeprom_getLabel(nn_veeprom *veeprom, char *buf, nn_size_t *buflen) {
    nn_memcpy(buf, veeprom->label, veeprom->labelLen);
    *buflen = veeprom->labelLen;
}

static nn_size_t nni_veeprom_setLabel(nn_veeprom *veeprom, const char *buf, nn_size_t buflen) {
    if(buflen > NN_LABEL_SIZE) buflen = NN_LABEL_SIZE;
    nn_memcpy(veeprom->label, buf, buflen);
    veeprom->labelLen = buflen;
    return buflen;
}

static nn_size_t nni_veeprom_get(nn_veeprom *veeprom, char *buf) {
    nn_memcpy(buf, veeprom->code, veeprom->codeLen);
    return veeprom->codeLen;
}

static void nni_veeprom_set(nn_veeprom *veeprom, const char *buf, nn_size_t len) {
    nn_memcpy(veeprom->code, buf, len);
    veeprom->codeLen = len;
}

static nn_size_t nni_veeprom_getData(nn_veeprom *veeprom, char *buf) {
    nn_memcpy(buf, veeprom->data, veeprom->dataLen);
    return veeprom->dataLen;
}

static void nni_veeprom_setData(nn_veeprom *veeprom, const char *buf, nn_size_t len) {
    nn_memcpy(veeprom->data, buf, len);
    veeprom->dataLen = len;
}

static nn_bool_t nni_veeprom_isReadonly(nn_veeprom *eeprom) {
    return eeprom->isReadOnly;
}

static void nni_veeprom_makeReadonly(nn_veeprom *eeprom) {
    eeprom->isReadOnly = true;
}

nn_eeprom *nn_volatileEEPROM(nn_Context *context, nn_veepromOptions opts, nn_eepromControl control) {
    nn_Alloc *a = &context->allocator;

    // TODO: handle OOM
    nn_veeprom *veeprom = nn_alloc(a, sizeof(nn_veeprom));
    veeprom->ctx = *context;
    veeprom->codeSize = opts.size;
    veeprom->code = nn_alloc(a, veeprom->codeSize);
    veeprom->codeLen = opts.len;
    veeprom->dataSize = opts.dataSize;
    veeprom->data = nn_alloc(a, veeprom->dataSize);
    veeprom->dataLen = opts.dataLen;
    veeprom->isReadOnly = opts.isReadOnly;
    veeprom->labelLen = opts.labelLen;
    nn_memcpy(veeprom->label, opts.label, veeprom->labelLen);

    nn_memcpy(veeprom->code, opts.code, veeprom->codeLen);
    nn_memcpy(veeprom->data, opts.data, veeprom->dataLen);

    nn_eepromTable table = {
        .userdata = veeprom,
        .deinit = (void *)nni_veeprom_deinit,
        .size = opts.size,
        .dataSize = opts.dataSize,
        .getLabel = (void *)nni_veeprom_getLabel,
        .setLabel = (void *)nni_veeprom_setLabel,
        .get = (void *)nni_veeprom_get,
        .set = (void *)nni_veeprom_set,
        .getData = (void *)nni_veeprom_getData,
        .setData = (void *)nni_veeprom_setData,
        .isReadonly = (void *)nni_veeprom_isReadonly,
        .makeReadonly = (void *)nni_veeprom_makeReadonly,
    };

    return nn_newEEPROM(context, table, control);
}
