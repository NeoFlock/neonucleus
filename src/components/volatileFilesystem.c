#include "../neonucleus.h"

// TODO: finish

// Data structures

typedef struct nn_vfnode {
    struct nn_vfilesystem *fs;
    char name[NN_MAX_PATH];
    struct nn_vfnode *parent;
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
    nn_intptr_t position;
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
        .lastModified = nn_vf_now(fs),
        .parent = NULL,
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
        .lastModified = nn_vf_now(fs),
        .parent = NULL,
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

nn_vfnode *nn_vf_find(nn_vfnode *parent, const char *name) {
    if(parent->isDirectory) return NULL;
    for(nn_size_t i = 0; i < parent->len; i++) {
        nn_vfnode *entry = parent->entries[i];

        if(nn_strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

nn_bool_t nn_vf_ensureFileCapacity(nn_vfnode *file, nn_size_t capacity) {
    if(file->isDirectory) return false;
    nn_Alloc *alloc = &file->fs->ctx.allocator;

    if(file->cap >= capacity) return true; // already at that point

    char *newData = nn_resize(alloc, file->data, file->cap, capacity);
    if(newData == NULL) {
        return false; // OOM
    }
    file->data = newData;
    file->cap = capacity;

    return true;
}

// this is used to compute exponential backoff
// TODO: add an option to select either a slower growth rate or
// linear backoff to reduce memory usage at the cost of speed
nn_size_t nn_vf_getIdealCapacity(nn_vfnode *file, nn_size_t spaceNeeded) {
    nn_size_t cap = file->cap;
    if(cap == 0) cap = 1;
    while(cap < spaceNeeded) cap *= 2; // this would mean a file with 1,048,577 bytes takes up 2,097,152 bytes, potentially wasting 1,048,575 bytes
    return cap;
}

void nn_vf_clampHandlePosition(nn_vfhandle *handle) {
    nn_size_t len = handle->node->len;
    if(handle->position < 0) handle->position = 0;
    if(handle->position > len) handle->position = len;
}

nn_vfnode *nn_vf_resolvePathFromNode(nn_vfnode *node, const char *path) {
    if(path[0] == 0) return node;
    char firstDirectory[NN_MAX_PATH];
    char subpath[NN_MAX_PATH];
    if(nn_path_firstName(path, firstDirectory, subpath)) {
        return nn_vf_find(node, firstDirectory);
    }

    nn_vfnode *dir = nn_vf_find(node, firstDirectory);
    return nn_vf_resolvePathFromNode(dir, subpath);
}

nn_vfnode *nn_vf_resolvePath(nn_vfilesystem *fs, const char *path) {
    return nn_vf_resolvePathFromNode(fs->root, path);
}

nn_size_t nn_vf_countTree(nn_vfnode *node) {
    if(!node->isDirectory) return 1;
    nn_size_t n = 1;
    for(nn_size_t i = 0; i < node->len; i++) {
        n += nn_vf_countTree(node->entries[i]);
    }
    return n;
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

nn_size_t nn_vfs_size(nn_vfilesystem *fs, const char *path, nn_errorbuf_t err) {
    nn_vfnode *node = nn_vf_resolvePath(fs, path);
    if(node == NULL) {
        nn_error_write(err, "No such file");
        return 0;
    }
    if(node->isDirectory) return 0;
    return node->len;
}

nn_size_t nn_vfs_remove(nn_vfilesystem *fs, const char *path, nn_errorbuf_t err) {
    nn_vfnode *node = nn_vf_resolvePath(fs, path);
    if(node == NULL) {
        nn_error_write(err, "No such file");
        return 0;
    }
    nn_vfnode *parent = node->parent;
    if(parent == NULL) {
        // root, can't delete
        nn_error_write(err, "Unable to delete root");
        return 0;
    }
    nn_size_t removed = nn_vf_countTree(node);
    // it is super easy to delete a tree
    nn_size_t j = 0;
    for(nn_size_t i = 0; i < parent->len; i++) {
        if(parent->entries[i] != node) {
            parent->entries[j] = parent->entries[i];
            j++;
        }
    }
    parent->len = j;
    parent->lastModified = nn_vf_now(fs);
    nn_vf_freeNode(node);
    return removed;
}

nn_size_t nn_vfs_lastModified(nn_vfilesystem *fs, const char *path, nn_errorbuf_t err) {
    nn_vfnode *node = nn_vf_resolvePath(fs, path);
    if(node == NULL) {
        nn_error_write(err, "No such file");
        return 0;
    }
    return node->lastModified;
}

nn_size_t nn_vfs_rename(nn_vfilesystem *fs, const char *from, const char *to, nn_errorbuf_t err) {
    // TODO: implement rename
    nn_error_write(err, "Unsupported operation");
    return 0;
}

nn_bool_t nn_vfs_exists(nn_vfilesystem *fs, const char *path, nn_errorbuf_t err) {
    nn_vfnode *node = nn_vf_resolvePath(fs, path);
    return node != NULL;
}

nn_bool_t nn_vfs_isDirectory(nn_vfilesystem *fs, const char *path, nn_errorbuf_t err) {
    nn_vfnode *node = nn_vf_resolvePath(fs, path);
    if(node == NULL) {
        nn_error_write(err, "No such file");
        return 0;
    }
    return node->isDirectory;
}

nn_bool_t nn_vfs_makeDirectory(nn_vfilesystem *fs, const char *path, nn_errorbuf_t err) {
    nn_vfnode *node = nn_vf_resolvePath(fs, path);
    if(node != NULL) {
        nn_error_write(err, "File already exists");
        return 0;
    }
    char name[NN_MAX_PATH];
    char parentPath[NN_MAX_PATH];
    nn_path_lastName(path, name, parentPath);
    nn_vfnode *parent = nn_vf_resolvePath(fs, path);
    return false;
}

char **nn_vfs_list(nn_Alloc *alloc, nn_vfilesystem *fs, const char *path, nn_size_t *len, nn_errorbuf_t err) {
    nn_vfnode *node = nn_vf_resolvePath(fs, path);
    if(node == NULL) {
        nn_error_write(err, "No such file");
        return NULL;
    }
    if(!node->isDirectory) {
        nn_error_write(err, "Not a directory");
        return NULL;
    }
    nn_size_t count = node->len;
    *len = count;
    char **buf = nn_alloc(alloc, sizeof(char *) * count);
    if(buf == NULL) {
        nn_error_write(err, "Out of memory");
        return NULL;
    }
    for(nn_size_t i = 0; i < count; i++) {
        nn_vfnode *entry = node->entries[i];
        char *s = NULL;
        if(entry->isDirectory) {
            nn_size_t l = nn_strlen(entry->name);
            s = nn_alloc(alloc, l + 2);
            if(s != NULL) {
                nn_memcpy(s, entry->name, l);
                entry->name[l] = '/';
                entry->name[l+1] = 0;
            }
        } else {
            s = nn_strdup(alloc, entry->name);
        }
        if(s == NULL) {
            for(nn_size_t j = 0; j < i; j++) {
                nn_deallocStr(alloc, buf[j]);
            }
            nn_error_write(err, "Out of memory");
            return NULL;
        }
        buf[i] = s;
    }
    return buf;
}

void *nn_vfs_open(nn_vfilesystem *fs, const char *path, const char *mode, nn_errorbuf_t err) {
    // TODO: complete
    char m = mode[0];
    nn_vfmode fmode = NN_VFMODE_READ;
    if(m == 'w') fmode = NN_VFMODE_WRITE;
    if(m == 'a') fmode = NN_VFMODE_APPEND;
    return NULL;
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
        .size = (void *)nn_vfs_size,
        .remove = (void *)nn_vfs_remove,
        .exists = (void *)nn_vfs_exists,
        .isDirectory = (void *)nn_vfs_isDirectory,
        .makeDirectory = (void *)nn_vfs_makeDirectory,
        .list = (void *)nn_vfs_list,
    };
    return nn_newFilesystem(context, table, control);
}
