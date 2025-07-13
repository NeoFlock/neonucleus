#include "../neonucleus.h"

// Data structures

typedef struct nn_vfnode {
    struct nn_vfilesystem *fs;
    struct nn_vfnode *parent;
    char name[NN_MAX_PATH];
    nn_bool_t isDirectory;
    union {
        // if directory
        struct nn_vfnode **entries;
        // if file
        char *data;
    };
    nn_size_t len;
    nn_size_t cap;
    nn_size_t lastModified;
    // this is used to block deleting
    nn_refc handleCount;
} nn_vfnode;

typedef enum nn_vfmode {
    NN_VFMODE_READ,
    NN_VFMODE_WRITE,
    NN_VFMODE_APPEND,
} nn_vfmode;

typedef struct nn_vfhandle {
    nn_vfnode *node;
    nn_size_t position;
    nn_vfmode mode;
} nn_vfhandle;

typedef struct nn_vfilesystem {
    nn_Context ctx;
    nn_vfilesystemOptions opts;
    double birthday;
    nn_vfnode *root;
} nn_vfilesystem;

// virtual node helpers

nn_size_t nn_vf_now(nn_vfilesystem *fs) {
    nn_Clock c = fs->ctx.clock;
    double time = c.proc(c.userdata);
    double elapsed = time - fs->birthday;
    nn_size_t elapsedMS = elapsed * 1000;
    return fs->opts.creationTime + elapsedMS;
}

nn_vfnode *nn_vf_allocFile(nn_vfilesystem *fs, const char *name) {
    nn_Alloc *alloc = &fs->ctx.allocator;
    nn_vfnode *node = nn_alloc(alloc, sizeof(nn_vfnode));
    if(node == NULL) return NULL;
    *node = (nn_vfnode) {
        .fs = fs,
        .parent = NULL,
        .lastModified = nn_vf_now(fs),
        .isDirectory = false,
        .data = NULL,
        .len = 0,
        .cap = 0,
        .handleCount = 0,
    };
    // we pray
    nn_strcpy(node->name, name);
    return node;
}

nn_vfnode *nn_vf_allocDirectory(nn_vfilesystem *fs, const char *name) {
    nn_Alloc *alloc = &fs->ctx.allocator;
    nn_vfnode *node = nn_alloc(alloc, sizeof(nn_vfnode));
    if(node == NULL) return NULL;
    nn_vfnode **buffer = nn_alloc(alloc, sizeof(nn_vfnode *));
    if(buffer == NULL) {
        nn_dealloc(alloc, node, sizeof(nn_vfnode));
        return NULL;
    }
    *node = (nn_vfnode) {
        .fs = fs,
        .parent = NULL,
        .lastModified = nn_vf_now(fs),
        .isDirectory = false,
        .entries = buffer,
        .len = 0,
        .cap = fs->opts.maxDirEntries,
        .handleCount = 0,
    };
    // we pray
    nn_strcpy(node->name, name);
    return node;
}

void nn_vf_freeNode(nn_vfnode *node) {
    nn_Alloc *alloc = &node->fs->ctx.allocator;

    if(node->isDirectory) {
        for(nn_size_t i = 0; i < node->len; i++) {
            nn_vf_freeNode(node->entries[i]);
        }
        nn_dealloc(alloc, node->entries, sizeof(nn_vfnode *) * node->cap);
    } else {
        nn_dealloc(alloc, node->data, node->cap);
    }

    nn_dealloc(alloc, node, sizeof(nn_vfnode));
}

nn_size_t nn_vf_spaceUsedByNode(nn_vfnode *node) {
    if(node->isDirectory) {
        nn_size_t sum = 0;
        for(nn_size_t i = 0; i < node->len; i++) {
            sum = nn_vf_spaceUsedByNode(node->entries[i]);
        }
        return sum;
    } else {
        return node->len;
    }
}

// methods

void nn_vfs_deinit(nn_vfilesystem *fs) {
    nn_Context ctx = fs->ctx;

    nn_vf_freeNode(fs->root);
    nn_dealloc(&ctx.allocator, fs, sizeof(nn_vfilesystem));
}

void nn_vfs_getLabel(nn_vfilesystem *fs, char *buf, nn_size_t *buflen) {
    *buflen = fs->opts.labelLen;
    nn_memcpy(buf, fs->opts.label, fs->opts.labelLen);
}

void nn_vfs_setLabel(nn_vfilesystem *fs, const char *buf, nn_size_t buflen) {
    nn_memcpy(fs->opts.label, buf, buflen);
    fs->opts.labelLen = buflen;
}

nn_size_t nn_vfs_spaceUsed(nn_vfilesystem *fs) {
    return nn_vf_spaceUsedByNode(fs->root);
}

nn_bool_t nn_vfs_isReadOnly(nn_vfilesystem *fs) {
    return fs->opts.isReadOnly;
}

// main funciton

nn_filesystem *nn_volatileFilesystem(nn_Context *context, nn_vfilesystemOptions opts, nn_filesystemControl control) {
    // TODO: handle OOM
    nn_vfilesystem *fs = nn_alloc(&context->allocator, sizeof(nn_vfilesystem));
    nn_Clock c = fs->ctx.clock;
    double time = c.proc(c.userdata);
    fs->ctx = *context;
    fs->birthday = time;
    fs->opts = opts;
    fs->root = nn_vf_allocDirectory(fs, "/");
    nn_filesystemTable table = {
        .userdata = fs,
        .deinit = (void *)nn_vfs_deinit,
        .getLabel = (void *)nn_vfs_getLabel,
        .setLabel = (void *)nn_vfs_setLabel,
        .spaceUsed = (void *)nn_vfs_spaceUsed,
        .spaceTotal = opts.capacity,
        .isReadOnly = (void *)nn_vfs_isReadOnly,
    };
    return nn_newFilesystem(context, table, control);
}
