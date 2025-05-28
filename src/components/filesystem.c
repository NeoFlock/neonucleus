#include "../neonucleus.h"
#include <string.h>

void nn_fs_destroy(void *_, nn_component *component, nn_filesystem *fs) {
    if(!nn_decRef(&fs->refc)) return;

    if(fs->deinit == NULL) {
        fs->deinit(component, fs->userdata);
    }
}

bool nn_fs_illegalPath(const char *path) {
    // absolute disaster
    const char *illegal = "\"\\:*?<>|";

    for(size_t i = 0; illegal[i] != '\0'; i++) {
        if(strchr(path, illegal[i]) != NULL) return true;
    }
    return false;
}

nn_filesystemControl nn_fs_getControl(nn_component *component, nn_filesystem *fs) {
    return fs->control(component, fs->userdata);
}

size_t nn_fs_countChunks(nn_filesystem *fs, size_t bytes, nn_component *component) {
    nn_filesystemControl control = nn_fs_getControl(component, fs);

    size_t chunks = bytes / control.pretendChunkSize;
    if(bytes % control.pretendChunkSize != 0) chunks++;
    return chunks;
}

void nn_fs_readCost(nn_filesystem *fs, size_t count, nn_component *component, nn_computer *computer) {
    nn_filesystemControl control = nn_fs_getControl(component, fs);
    nn_randomLatency(control.randomLatencyMin, control.randomLatencyMax);
    nn_busySleep(control.readLatencyPerChunk * count);
    nn_removeEnergy(computer, control.readEnergyCost * count);
    nn_callCost(computer, control.readCostPerChunk * count);
}

void nn_fs_writeCost(nn_filesystem *fs, size_t count, nn_component *component, nn_computer *computer) {
    nn_filesystemControl control = nn_fs_getControl(component, fs);
    nn_randomLatency(control.randomLatencyMin, control.randomLatencyMax);
    nn_busySleep(control.writeLatencyPerChunk * count);
    nn_removeEnergy(computer, control.writeEnergyCost * count);
    nn_addHeat(computer, control.writeHeatPerChunk * count);
    nn_callCost(computer, control.writeCostPerChunk * count);
}

void nn_fs_seekCost(nn_filesystem *fs, size_t count, nn_component *component, nn_computer *computer) {
    nn_filesystemControl control = nn_fs_getControl(component, fs);
    double seekLatency = ((double)control.pretendRPM / 60) * control.pretendChunkSize / fs->spaceTotal(component, fs->userdata);

    nn_randomLatency(control.randomLatencyMin, control.randomLatencyMax);
    nn_busySleep(seekLatency * count);
    nn_removeEnergy(computer, control.writeEnergyCost * count);
    nn_addHeat(computer, control.writeHeatPerChunk * count);
    nn_callCost(computer, control.writeCostPerChunk * count);
}

void nn_fs_getLabel(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    char buf[NN_LABEL_SIZE];
    size_t l = NN_LABEL_SIZE;
    fs->getLabel(component, fs->userdata, buf, &l);
    if(l == 0) {
        nn_return(computer, nn_values_nil());
    } else {
        nn_return(computer, nn_values_string(buf, l));
    }
    
    // Latency, energy costs and stuff
    nn_filesystemControl control = nn_fs_getControl(component, fs);
    nn_randomLatency(control.randomLatencyMin, control.randomLatencyMax);
    nn_busySleep(control.readLatencyPerChunk);
    nn_removeEnergy(computer, control.readEnergyCost);
    nn_callCost(computer, control.readCostPerChunk);
}

void nn_fs_setLabel(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    size_t l = 0;
    nn_value label = nn_getArgument(computer, 0);
    const char *buf = nn_toString(label, &l);
    if(buf == NULL) {
        nn_setCError(computer, "bad label (string expected)");
        return;
    }
    l = fs->setLabel(component, fs->userdata, buf, l);
    nn_return(computer, nn_values_string(buf, l));

    nn_fs_readCost(fs, 1, component, computer);
}

void nn_fs_spaceUsed(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    size_t space = fs->spaceUsed(component, fs->userdata);
    nn_return(computer, nn_values_integer(space));
    
    nn_fs_readCost(fs, 1, component, computer);
}

void nn_fs_spaceTotal(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    size_t space = fs->spaceUsed(component, fs->userdata);
    nn_return(computer, nn_values_integer(space));
    
    nn_fs_readCost(fs, 1, component, computer);
}

void nn_fs_isReadOnly(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_return(computer, nn_values_boolean(fs->isReadOnly(component, fs->userdata)));
    
    nn_fs_readCost(fs, 1, component, computer);
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

    size_t byteSize = fs->size(component, fs->userdata, path);
    
    nn_fs_readCost(fs, 1, component, computer);
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

    nn_return(computer, nn_values_boolean(fs->remove(component, fs->userdata, path)));
   
    // Considered 1 safety check + the actual write
    nn_fs_readCost(fs, 1, component, computer);
    nn_fs_writeCost(fs, 1, component, computer);
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

    nn_return(computer, nn_values_integer(fs->remove(component, fs->userdata, path)));
   
    nn_fs_readCost(fs, 1, component, computer);
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

    size_t movedCount = fs->rename(component, fs->userdata, from, to);
    nn_return(computer, nn_values_boolean(movedCount > 0));
   
    // Considered 2 safety checks + 1 read per file + 1 write per file
    nn_fs_readCost(fs, 2 + movedCount, component, computer);
    nn_fs_writeCost(fs, movedCount, component, computer);
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

    nn_return(computer, nn_values_boolean(fs->exists(component, fs->userdata, path)));
   
    nn_fs_readCost(fs, 1, component, computer);
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

    nn_return(computer, nn_values_boolean(fs->isDirectory(component, fs->userdata, path)));
   
    nn_fs_readCost(fs, 1, component, computer);
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

    nn_return(computer, nn_values_boolean(fs->makeDirectory(component, fs->userdata, path)));
   
    nn_fs_readCost(fs, 1, component, computer);
    nn_fs_writeCost(fs, 1, component, computer);
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

    size_t fileCount = 0;
    char **files = fs->list(component, fs->userdata, path, &fileCount);

    if(files != NULL) {
        // operation succeeded
        nn_value arr = nn_values_array(fileCount);
        for(size_t i = 0; i < fileCount; i++) {
            nn_values_set(arr, i, nn_values_cstring(files[i]));
            nn_free(files[i]);
        }
        nn_free(files);
        nn_return(computer, arr);
    }

    nn_fs_readCost(fs, 1 + fileCount, component, computer);
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

    size_t fd = fs->open(component, fs->userdata, path, mode);
    nn_return(computer, nn_values_integer(fd));

    // 1 safety check
    nn_fs_readCost(fs, 1, component, computer);
}

void nn_fs_close(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value fdValue = nn_getArgument(computer, 0);
    size_t fd = nn_toInt(fdValue);

    bool closed = fs->close(component, fs->userdata, fd);
    nn_return(computer, nn_values_boolean(closed));

    // do not ask where it comes from, balance is hard
    nn_fs_readCost(fs, 1, component, computer);
}

void nn_fs_write(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value fdValue = nn_getArgument(computer, 0);
    size_t fd = nn_toInt(fdValue);

    size_t spaceRemaining = fs->spaceTotal(component, fs->userdata) - fs->spaceUsed(component, fs->userdata);

    nn_value bufferValue = nn_getArgument(computer, 1);
    size_t len = 0;
    const char *buf = nn_toString(bufferValue, &len);
    if(buf == NULL) {
        nn_setCError(computer, "bad buffer (string expected)");
        return;
    }

    bool closed = fs->write(component, fs->userdata, fd, buf, len);
    nn_return(computer, nn_values_boolean(closed));

    // do not ask where it comes from, balance is hard
    nn_fs_writeCost(fs, nn_fs_countChunks(fs, len, component), component, computer);
    nn_fs_seekCost(fs, nn_fs_countChunks(fs, len, component), component, computer);
}

void nn_fs_read(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    nn_value fdValue = nn_getArgument(computer, 0);
    size_t fd = nn_toInt(fdValue);

    nn_value lenValue = nn_getArgument(computer, 1);
    double len = nn_toNumber(lenValue);
    size_t capacity = fs->spaceTotal(component, fs->userdata);
    if(len > capacity) len = capacity;
    size_t byteLen = len;

    char *buf = nn_malloc(byteLen);
    if(buf == NULL) {
        nn_setCError(computer, "out of memory");
        return;
    }

    size_t readLen = fs->read(component, fs->userdata, fd, buf, byteLen);
    if(readLen > 0) {
        // Nothing read means EoF.
        nn_return(computer, nn_values_string(buf, readLen));
    }
    nn_free(buf);

    // do not ask where it comes from, balance is hard
    nn_fs_readCost(fs, nn_fs_countChunks(fs, readLen, component), component, computer);
    nn_fs_seekCost(fs, nn_fs_countChunks(fs, readLen, component), component, computer);
}

bool nn_fs_validWhence(const char *s) {
    return
        strcmp(s, "set") == 0 ||
        strcmp(s, "cur") == 0 ||
        strcmp(s, "end") == 0;
}

void nn_fs_seek(nn_filesystem *fs, void *_, nn_component *component, nn_computer *computer) {
    size_t fd = nn_toInt(nn_getArgument(computer, 0));

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

    size_t capacity = fs->spaceTotal(component, fs->userdata);
    int moved = 0;

    size_t pos = fs->seek(component, fs->userdata, fd, whence, off, &moved);
    if(moved < 0) moved = -moved;

    // do not ask where it comes from, balance is hard
    nn_fs_readCost(fs, 1, component, computer);
    nn_fs_seekCost(fs, nn_fs_countChunks(fs, moved, component), component, computer);
}

void nn_loadFilesystemTable(nn_universe *universe) {
    nn_componentTable *fsTable = nn_newComponentTable("filesystem", NULL, NULL, (void *)nn_fs_destroy);
    nn_storeUserdata(universe, "NN:FILESYSTEM", fsTable);

    nn_defineMethod(fsTable, "getLabel", false, (void *)nn_fs_getLabel, NULL, "getLabel(): string - Returns the label of the filesystem.");
    nn_defineMethod(fsTable, "setLabel", false, (void *)nn_fs_setLabel, NULL, "setLabel(label: string): string - Sets a new label for the filesystem and returns the new label of the filesystem, which may have been truncated.");
    nn_defineMethod(fsTable, "spaceUsed", false, (void *)nn_fs_spaceUsed, NULL, "spaceUsed(): integer - Returns the amounts of bytes used.");
    nn_defineMethod(fsTable, "spaceTotal", false, (void *)nn_fs_spaceTotal, NULL, "spaceTotal(): integer - Returns the capacity of the filesystem.");
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
