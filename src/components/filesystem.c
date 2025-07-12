#include "../neonucleus.h"

typedef struct nn_filesystem {
    nn_refc refc;
    nn_guard *lock;
    nn_Context ctx;
    nn_filesystemTable table;
    nn_filesystemControl control;
    nn_size_t spaceUsedCache;

    // last due to cache concerns (this struck is massive)
    void *files[NN_MAX_OPEN_FILES];
} nn_filesystem;

void nn_fs_destroy(void *_, nn_component *component, nn_filesystem *fs) {
    nn_destroyFilesystem(fs);
}

nn_filesystem *nn_newFilesystem(nn_Context *context, nn_filesystemTable table, nn_filesystemControl control) {
    nn_filesystem *fs = nn_alloc(&context->allocator, sizeof(nn_filesystem));
    if(fs == NULL) return NULL;
    fs->refc = 1;
    fs->ctx = *context;
    fs->table = table;
    fs->control = control;
    fs->spaceUsedCache = 0;
    fs->lock = nn_newGuard(context);
    if(fs->lock == NULL) {
        nn_dealloc(&context->allocator, fs, sizeof(nn_filesystem));
        return NULL;
    }
    
    for(nn_size_t i = 0; i < NN_MAX_OPEN_FILES; i++) {
        fs->files[i] = NULL;
    }
    return fs;
}

nn_guard *nn_getFilesystemLock(nn_filesystem *fs) {
    return fs->lock;
}

void nn_retainFilesystem(nn_filesystem *fs) {
    nn_incRef(&fs->refc);
}

nn_bool_t nn_destroyFilesystem(nn_filesystem *fs) {
    if(!nn_decRef(&fs->refc)) return false;

    if(fs->table.deinit != NULL) {
        fs->table.deinit(fs->table.userdata);
    }

    nn_Context ctx = fs->ctx;
    nn_deleteGuard(&ctx, fs->lock);
    nn_dealloc(&ctx.allocator, fs, sizeof(nn_filesystem));
    return true;
}

nn_size_t nn_fs_getSpaceUsed(nn_filesystem *fs) {
    if(fs->spaceUsedCache != 0) return fs->spaceUsedCache;
    nn_size_t spaceUsed = fs->table.spaceUsed(fs->table.userdata);
    fs->spaceUsedCache = spaceUsed;
    return spaceUsed;
}

void nn_fs_invalidateSpaceUsed(nn_filesystem *fs) {
    fs->spaceUsedCache = 0;
}

nn_size_t nn_fs_getSpaceRemaining(nn_filesystem *fs) {
    nn_size_t used = nn_fs_getSpaceUsed(fs);
    nn_size_t total = fs->table.spaceTotal;
    return total - used;
}

void *nn_fs_unwrapFD(nn_filesystem *fs, nn_size_t fd) {
    if(fd >= NN_MAX_OPEN_FILES) {
        return NULL;
    }
    void *file = fs->files[fd];
    if(file == NULL) {
        return NULL;
    }
    return file;
}

nn_bool_t nn_fs_illegalPath(const char *path) {
    // absolute disaster
    const char *illegal = "\"\\:*?<>|";

    for(nn_size_t i = 0; illegal[i] != '\0'; i++) {
        if(nn_strchr(path, illegal[i]) != NULL) return true;
    }
    return false;
}

void nn_fs_readCost(nn_filesystem *fs, nn_size_t bytes, nn_component *component) {
    nn_filesystemControl control = fs->control;
    nn_computer *computer = nn_getComputerOfComponent(component);

    nn_simulateBufferedIndirect(component, bytes, control.readBytesPerTick);
    nn_removeEnergy(computer, control.readEnergyPerByte * bytes);
    nn_addHeat(computer, control.readHeatPerByte * bytes);
}

void nn_fs_writeCost(nn_filesystem *fs, nn_size_t bytes, nn_component *component) {
    nn_filesystemControl control = fs->control;
    nn_computer *computer = nn_getComputerOfComponent(component);

    nn_simulateBufferedIndirect(component, bytes, control.writeBytesPerTick);
    nn_removeEnergy(computer, control.writeEnergyPerByte * bytes);
    nn_addHeat(computer, control.writeHeatPerByte * bytes);
}

void nn_fs_removeCost(nn_filesystem *fs, nn_size_t count, nn_component *component) {
    nn_filesystemControl control = fs->control;
    nn_computer *computer = nn_getComputerOfComponent(component);

    nn_simulateBufferedIndirect(component, count, control.removeFilesPerTick);
    nn_removeEnergy(computer, control.removeEnergy * count);
    nn_addHeat(computer, control.removeHeat * count);
}

void nn_fs_createCost(nn_filesystem *fs, nn_size_t count, nn_component *component) {
    nn_filesystemControl control = fs->control;
    nn_computer *computer = nn_getComputerOfComponent(component);

    nn_simulateBufferedIndirect(component, count, control.createFilesPerTick);
    nn_removeEnergy(computer, control.createEnergy * count);
    nn_addHeat(computer, control.createHeat * count);
}

void nn_fs_getLabel(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    char buf[NN_LABEL_SIZE];
    nn_size_t l = NN_LABEL_SIZE;
    fs->table.getLabel(fs->table.userdata, buf, &l);
    if(l == 0) {
        nn_return(computer, nn_values_nil());
    } else {
        nn_return_string(computer, buf, l);
    }

    nn_fs_readCost(fs, l, component);
}

void nn_fs_setLabel(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t l = 0;
    nn_value label = nn_getArgument(computer, 0);
    const char *buf = nn_toString(label, &l);
    if(buf == NULL) {
        nn_setCError(computer, "bad label (string expected)");
        return;
    }
    l = fs->table.setLabel(fs->table.userdata, buf, l);
    nn_return_string(computer, buf, l);

    nn_fs_writeCost(fs, l, component);
}

void nn_fs_spaceUsed(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t space = nn_fs_getSpaceUsed(fs);
    nn_return(computer, nn_values_integer(space));
}

void nn_fs_spaceTotal(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_return(computer, nn_values_integer(fs->table.spaceTotal));
}

void nn_fs_isReadOnly(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_return_boolean(computer, fs->table.isReadOnly(fs->table.userdata));
}

void nn_fs_size(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value pathValue = nn_getArgument(computer, 0);
    const char *path = nn_toCString(pathValue);
    if(path == NULL) {
        nn_setCError(computer, "bad path (string expected)");
        return;
    }
    if(nn_fs_illegalPath(path)) {
        nn_setCError(computer, "bad path (illegal path)");
        return;
    }

    nn_size_t byteSize = fs->table.size(fs->table.userdata, path);

    nn_return(computer, nn_values_integer(byteSize));
}

void nn_fs_remove(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value pathValue = nn_getArgument(computer, 0);
    const char *path = nn_toCString(pathValue);
    if(path == NULL) {
        nn_setCError(computer, "bad path (string expected)");
        return;
    }
    if(nn_fs_illegalPath(path)) {
        nn_setCError(computer, "bad path (illegal path)");
        return;
    }

    nn_size_t removed = fs->table.remove(fs->table.userdata, path);
    nn_return_boolean(computer, removed > 0);

    nn_fs_removeCost(fs, removed, component);
}

void nn_fs_lastModified(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value pathValue = nn_getArgument(computer, 0);
    const char *path = nn_toCString(pathValue);
    if(path == NULL) {
        nn_setCError(computer, "bad path (string expected)");
        return;
    }
    if(nn_fs_illegalPath(path)) {
        nn_setCError(computer, "bad path (illegal path)");
        return;
    }

    nn_size_t t = fs->table.lastModified(fs->table.userdata, path);

    // OpenOS does BULLSHIT with this thing, dividing it by 1000 and expecting it to be
    // fucking usable as a date, meaning it needs to be an int.
    // Because of that, we ensure it is divisible by 1000
    t -= t % 1000;

    nn_return(computer, nn_values_integer(t));
}

void nn_fs_rename(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value fromValue = nn_getArgument(computer, 0);
    const char *from = nn_toCString(fromValue);
    if(from == NULL) {
        nn_setCError(computer, "bad path #1 (string expected)");
        return;
    }
    if(nn_fs_illegalPath(from)) {
        nn_setCError(computer, "bad path #1 (illegal path)");
        return;
    }
    
    nn_value toValue = nn_getArgument(computer, 0);
    const char *to = nn_toCString(toValue);
    if(to == NULL) {
        nn_setCError(computer, "bad path #2 (string expected)");
        return;
    }
    if(nn_fs_illegalPath(to)) {
        nn_setCError(computer, "bad path #2 (illegal path)");
        return;
    }

    nn_size_t movedCount = fs->table.rename(fs->table.userdata, from, to);
    nn_return(computer, nn_values_boolean(movedCount > 0));
   
    nn_fs_removeCost(fs, movedCount, component);
    nn_fs_createCost(fs, movedCount, component);
}

void nn_fs_exists(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value pathValue = nn_getArgument(computer, 0);
    const char *path = nn_toCString(pathValue);
    if(path == NULL) {
        nn_setCError(computer, "bad path (string expected)");
        return;
    }
    if(nn_fs_illegalPath(path)) {
        nn_setCError(computer, "bad path (illegal path)");
        return;
    }

    nn_return_boolean(computer, fs->table.exists(fs->table.userdata, path));
}

void nn_fs_isDirectory(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value pathValue = nn_getArgument(computer, 0);
    const char *path = nn_toCString(pathValue);
    if(path == NULL) {
        nn_setCError(computer, "bad path (string expected)");
        return;
    }
    if(nn_fs_illegalPath(path)) {
        nn_setCError(computer, "bad path (illegal path)");
        return;
    }

    nn_return_boolean(computer, fs->table.isDirectory(fs->table.userdata, path));
}

void nn_fs_makeDirectory(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value pathValue = nn_getArgument(computer, 0);
    const char *path = nn_toCString(pathValue);
    if(path == NULL) {
        nn_setCError(computer, "bad path (string expected)");
        return;
    }
    if(nn_fs_illegalPath(path)) {
        nn_setCError(computer, "bad path (illegal path)");
        return;
    }

    nn_return_boolean(computer, fs->table.makeDirectory(fs->table.userdata, path));

    nn_fs_createCost(fs, 1, component);
}

void nn_fs_list(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value pathValue = nn_getArgument(computer, 0);
    const char *path = nn_toCString(pathValue);
    if(path == NULL) {
        nn_setCError(computer, "bad path (string expected)");
        return;
    }
    if(nn_fs_illegalPath(path)) {
        nn_setCError(computer, "bad path (illegal path)");
        return;
    }
    
    nn_Alloc *alloc = nn_getAllocator(nn_getUniverse(computer));

    nn_size_t fileCount = 0;
    char **files = fs->table.list(alloc, fs->table.userdata, path, &fileCount);

    if(files != NULL) {
        // operation succeeded
        nn_value arr = nn_values_array(alloc, fileCount);
        for(nn_size_t i = 0; i < fileCount; i++) {
            nn_values_set(arr, i, nn_values_string(alloc, files[i], nn_strlen(files[i])));
            nn_deallocStr(alloc, files[i]);
        }
        nn_dealloc(alloc, files, sizeof(char *) * fileCount);
        nn_return(computer, arr);
    }
}

void nn_fs_open(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value pathValue = nn_getArgument(computer, 0);
    const char *path = nn_toCString(pathValue);
    if(path == NULL) {
        nn_setCError(computer, "bad path (string expected)");
        return;
    }
    if(nn_fs_illegalPath(path)) {
        nn_setCError(computer, "bad path (illegal path)");
        return;
    }

    nn_value modeValue = nn_getArgument(computer, 1);
    const char *mode = nn_toCString(modeValue);
    if(mode == NULL) {
        mode = "r";
    }

    // technically wrongfully 
    if(!fs->table.exists(fs->table.userdata, path)) {
        nn_fs_createCost(fs, 1, component);
    }

    nn_size_t fd = 0;
    while(fs->files[fd] != NULL) {
        fd++;
        if(fd == NN_MAX_OPEN_FILES) {
            nn_setCError(computer, "too many open files");
            return;
        }
    }
    void *file = fs->table.open(fs->table.userdata, path, mode);
    if(file == NULL) {
        nn_setCError(computer, "no such file or directory");
        return;
    }
    fs->files[fd] = file;
    nn_return(computer, nn_values_integer(fd));
}

void nn_fs_close(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value fdValue = nn_getArgument(computer, 0);
    nn_size_t fd = nn_toInt(fdValue);
    void *file = nn_fs_unwrapFD(fs, fd);
    if(file == NULL) {
        nn_setCError(computer, "bad file descriptor");
        return;
    }

    nn_bool_t closed = fs->table.close(fs->table.userdata, file);
    if(closed) {
        fs->files[fd] = NULL;
    }
    nn_return(computer, nn_values_boolean(closed));
}

void nn_fs_write(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value fdValue = nn_getArgument(computer, 0);
    nn_size_t fd = nn_toInt(fdValue);

    nn_value bufferValue = nn_getArgument(computer, 1);
    nn_size_t len = 0;
    const char *buf = nn_toString(bufferValue, &len);
    if(buf == NULL) {
        nn_setCError(computer, "bad buffer (string expected)");
        return;
    }
    
    size_t spaceRemaining = nn_fs_getSpaceRemaining(fs);

    // overwriting would still work but OC does the same thing so...
    if(spaceRemaining < len) {
        nn_setCError(computer, "out of space");
        return;
    }
    void *file = nn_fs_unwrapFD(fs, fd);
    if(file == NULL) {
        nn_setCError(computer, "bad file descriptor");
        return;
    }

    nn_bool_t written = fs->table.write(fs->table.userdata, file, buf, len);
    nn_return(computer, nn_values_boolean(written));
    if(written) nn_fs_invalidateSpaceUsed(fs);

    nn_fs_writeCost(fs, len, component);
}

void nn_fs_read(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value fdValue = nn_getArgument(computer, 0);
    int fd = nn_toInt(fdValue);

    nn_value lenValue = nn_getArgument(computer, 1);
    double len = nn_toNumber(lenValue);
    // TODO: be smarter
    nn_size_t capacity = fs->table.spaceTotal;
    if(len > capacity) len = capacity;
    nn_size_t byteLen = len;
    
    void *file = nn_fs_unwrapFD(fs, fd);
    if(file == NULL) {
        nn_setCError(computer, "bad file descriptor");
        return;
    }

    nn_Alloc *alloc = nn_getAllocator(nn_getUniverse(computer));
    char *buf = nn_alloc(alloc, byteLen);
    if(buf == NULL) {
        nn_setCError(computer, "out of memory");
        return;
    }

    nn_size_t readLen = fs->table.read(fs->table.userdata, file, buf, byteLen);
    if(readLen > 0) {
        // Nothing read means EoF.
        nn_return_string(computer, buf, readLen);
    }
    nn_dealloc(alloc, buf, byteLen);

    nn_fs_readCost(fs, len, component);
}

nn_bool_t nn_fs_validWhence(const char *s) {
    return
        nn_strcmp(s, "set") == 0 ||
        nn_strcmp(s, "cur") == 0 ||
        nn_strcmp(s, "end") == 0;
}

void nn_fs_seek(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_size_t fd = nn_toInt(nn_getArgument(computer, 0));

    const char *whence = nn_toCString(nn_getArgument(computer, 1));

    int off = nn_toInt(nn_getArgument(computer, 2));

    if(whence == NULL) {
        nn_setCError(computer, "bad whence (string expected)");
        return;
    }
    
    if(!nn_fs_validWhence(whence)) {
        nn_setCError(computer, "bad whence");
        return;
    }

    void *file = nn_fs_unwrapFD(fs, fd);
    if(file == NULL) {
        nn_setCError(computer, "bad file descriptor");
        return;
    }

    nn_size_t pos = fs->table.seek(fs->table.userdata, file, whence, off);

    nn_return_integer(computer, pos);
}

void nn_loadFilesystemTable(nn_universe *universe) {
    nn_componentTable *fsTable = nn_newComponentTable(nn_getAllocator(universe), "filesystem", NULL, NULL, (void *)nn_fs_destroy);
    nn_storeUserdata(universe, "NN:FILESYSTEM", fsTable);

    nn_defineMethod(fsTable, "getLabel", true, (void *)nn_fs_getLabel, NULL, "getLabel(): string - Returns the label of the filesystem.");
    nn_defineMethod(fsTable, "setLabel", true, (void *)nn_fs_setLabel, NULL, "setLabel(label: string): string - Sets a new label for the filesystem and returns the new label of the filesystem, which may have been truncated.");
    nn_defineMethod(fsTable, "spaceUsed", true, (void *)nn_fs_spaceUsed, NULL, "spaceUsed(): integer - Returns the amounts of bytes used.");
    nn_defineMethod(fsTable, "spaceTotal", true, (void *)nn_fs_spaceTotal, NULL, "spaceTotal(): integer - Returns the capacity of the filesystem.");
    nn_defineMethod(fsTable, "isReadOnly", true, (void *)nn_fs_isReadOnly, NULL, "isReadOnly(): boolean - Returns whether the filesystem is in read-only mode.");
    nn_defineMethod(fsTable, "size", true, (void *)nn_fs_size, NULL, "size(path: string): integer - Gets the size, in bytes, of a file.");
    nn_defineMethod(fsTable, "remove", true, (void *)nn_fs_remove, NULL, "remove(path: string): boolean - Removes a file. Returns whether the operation succeeded.");
    nn_defineMethod(fsTable, "lastModified", true, (void *)nn_fs_lastModified, NULL, "remove(path: string): boolean - Removes a file. Returns whether the operation succeeded.");
    nn_defineMethod(fsTable, "rename", true, (void *)nn_fs_rename, NULL, "rename(from: string, to: string): boolean - Moves files from one path to another.");
    nn_defineMethod(fsTable, "exists", true, (void *)nn_fs_exists, NULL, "exists(path: string): boolean - Checks whether a file exists.");
    nn_defineMethod(fsTable, "isDirectory", true, (void *)nn_fs_isDirectory, NULL, "isDirectory(path: string): boolean - Returns whether a file is actually a directory.");
    nn_defineMethod(fsTable, "makeDirectory", true, (void *)nn_fs_makeDirectory, NULL, "makeDirectory(path: string): boolean - Creates a new directory at the given path. Returns whether it succeeded.");
    nn_defineMethod(fsTable, "list", true, (void *)nn_fs_list, NULL, "list(path: string): string[] - Returns a list of file paths. Directories will have a / after them");
    nn_defineMethod(fsTable, "open", true, (void *)nn_fs_open, NULL, "open(path: string[, mode: string = \"r\"]): integer - Opens a file, may create it.");
    nn_defineMethod(fsTable, "close", true, (void *)nn_fs_close, NULL, "close(fd: integer): boolean - Closes a file.");
    nn_defineMethod(fsTable, "write", true, (void *)nn_fs_write, NULL, "write(fd: integer, data: string): boolean - Writes data to a file.");
    nn_defineMethod(fsTable, "read", true, (void *)nn_fs_read, NULL, "read(fd: integer, len: number): string - Reads bytes from a file. Infinity is a valid length, in which case it reads as much as possible.");
    nn_defineMethod(fsTable, "seek", true, (void *)nn_fs_seek, NULL, "seek(fd: integer, whence: string, offset: integer): integer - Seeks a file. Returns the new position. Valid whences are set, cur and end.");
}

nn_component *nn_addFileSystem(nn_computer *computer, nn_address address, int slot, nn_filesystem *filesystem) {
    nn_componentTable *fsTable = nn_queryUserdata(nn_getUniverse(computer), "NN:FILESYSTEM");
    return nn_newComponent(computer, address, slot, fsTable, filesystem);
}
