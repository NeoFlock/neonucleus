#include "../neonucleus.h"

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

nn_filesystem *nn_volatileFilesystem(nn_Context *context, nn_vfilesystemOptions opts, nn_filesystemControl control) {

}
