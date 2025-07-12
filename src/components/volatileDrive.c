#include "../neonucleus.h"

// TODO: make it allocate lazily

typedef struct nn_vdrive {
    nn_Context ctx;
    char *buffer;
    nn_size_t sectorSize;
    nn_size_t capacity;
    char label[NN_LABEL_SIZE];
    nn_size_t labelLen;
} nn_vdrive;

static void nni_vdrive_deinit(nn_vdrive *vdrive) {
    nn_Alloc a = vdrive->ctx.allocator;
    nn_dealloc(&a, vdrive->buffer, vdrive->capacity);
    nn_dealloc(&a, vdrive, sizeof(nn_vdrive));
}

static void nni_vdrive_getLabel(nn_vdrive *vdrive, char *buf, nn_size_t *buflen) {
    nn_memcpy(buf, vdrive->label, vdrive->labelLen);
    *buflen = vdrive->labelLen;
}

static nn_size_t nni_vdrive_setLabel(nn_vdrive *vdrive, const char *buf, nn_size_t buflen) {
    if(buflen > NN_LABEL_SIZE) buflen = NN_LABEL_SIZE;
    nn_memcpy(vdrive->label, buf, buflen);
    vdrive->labelLen = buflen;
    return buflen;
}

static void nni_vdrive_readSector(nn_vdrive *vdrive, int sector, char *buf) {
    nn_memcpy(buf, vdrive->buffer + (sector - 1) * vdrive->sectorSize, vdrive->sectorSize);
}

static void nni_vdrive_writeSector(nn_vdrive *vdrive, int sector, const char *buf) {
    nn_memcpy(vdrive->buffer + (sector - 1) * vdrive->sectorSize, buf, vdrive->sectorSize);
}

nn_drive *nn_volatileDrive(nn_Context *context, nn_vdriveOptions opts, nn_driveControl control) {
    nn_Alloc *alloc = &context->allocator;

    char *buffer = nn_alloc(alloc, opts.capacity);
    if(buffer == NULL) return NULL;
    nn_vdrive *drive = nn_alloc(alloc, sizeof(nn_vdrive));
    if(drive == NULL) {
        nn_dealloc(alloc, buffer, opts.capacity);
        return NULL;
    }
    drive->ctx = *context;
    drive->buffer = buffer;
    drive->sectorSize = opts.sectorSize;
    drive->capacity = opts.capacity;
    nn_memcpy(drive->label, opts.label, opts.labelLen);
    drive->labelLen = opts.labelLen;
    if(opts.data == NULL) {
        nn_memset(buffer, 0, opts.capacity);
    } else {
        nn_memcpy(buffer, opts.data, opts.capacity);
    }

    nn_driveTable table = {
        .userdata = drive,
        .deinit = (void *)nni_vdrive_deinit,
        .getLabel = (void *)nni_vdrive_getLabel,
        .setLabel = (void *)nni_vdrive_setLabel,
        .readSector = (void *)nni_vdrive_readSector,
        .writeSector = (void *)nni_vdrive_writeSector,
        .sectorSize = opts.sectorSize,
        .platterCount = opts.platterCount,
        .capacity = opts.capacity,
    };
    return nn_newDrive(context, table, control);
}
