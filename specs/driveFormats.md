# Common

## Context

For emulators, or for users, it may be useful to have standard formats for making OS images.
These formats would effectively be snapshots of filesystems.
These formats can also be used for other things, such as NNFS being usable for RAMFSes, and NNISO for backups of storage devices.

Do note that NNFS is designed to be a read-only filesystem, and to encode data for loading RAMFSes. It is not at all intended to be used as a primary
filesystem.

## Integers

### Signedness

All integers in this spec are *UNSIGNED* unless specified otherwise.

### Variable size

Ints can take up a variable amount of bytes, using the highest bit of each byte to determine whether it is a continuation (1) or the final byte (0).
This leaves 7 bits per byte to store 7 bits of the integer. They are stored in little-endian order, with the least significant bits being in the first byte.

## Conventions

The structures in this spec will be written as C structs. A `char` is 1 byte, a `varint_t` is a variable-size integer, and there is no padding.

# NNFS

## Version 0

### Header

```c
struct nnfs_header {
    char header[5] = "NNFS\0"; // stores a 5 byte NULL-terminated string as the header of the entire file.
    varint_t version = 0; // 0 for the current version
    char label[]; // NULL-terminated string for the label
    varint_t capacity;
    varint_t flags;
    varint_t compression;
    // everything after this is compressed based off compression, unless its NNFS_UNCOMPRESSED, in which case it is as-is.
    varint_t rootEntries;
    nnfs_managedNode nodes[];
};

enum nnfs_flags {
  NNFS_READONLY = 1, // drive is meant to be read-only. This is for basic metadata
};

enum nnfs_compression {
  NNFS_UNCOMPRESSED = 0, // no compression, stored as is
  NNFS_DEFLATE = 1, // this means the stuff after compression is deflated, using the same deflate algorithm as the data card
};

struct nnfs_managedNode {
  char name[]; // if the name ends with a /, delete that / and this file is actually a directory
  varint_t timestamp;
  varint_t len;
  union {
    char data[len]; // if file
    void nothing; // not file
  };
}
```

nnfs_header stores the amount of entries in the root directory and then an array of nodes.
Each node represents an entry within the filesystem. They work similarly to tmpfs nodes.
The `len` of a directory is in entries, but an entry can take multiple nodes (because of directories).
`timestamp` is a UNIX timestamp, counting the amount of **milliseconds** since the UNIX epoch.

Given the file structre
```
/a
/a/foo.txt
/a/b
/a/b/test.txt
```

The length of `a` would be 2, as it has `foo.txt` and `b`, and `b`'s would be 1.

# NNISO

## Version 0

### Header

```c
struct nniso_header {
    char header[6] = "NNISO\0"; // stores a 5 byte NULL-terminated string as the header of the entire file.
    varint_t version = 0; // 0 for the current version
    char label[]; // NULL-terminated string for the label
    varint_t capacity;
    varint_t sectorSize;
    varint_t flags;
    varint_t compression;
    // everything after this is compressed based off compression, unless its NNFS_UNCOMPRESSED, in which case it is as-is.
    // if the chunks do not reach capacity, the rest is assumed to be 0.
    nniso_chunk chunks[];
};

enum nniso_flags {
  NNFS_READONLY = 1, // drive is meant to be read-only. This is for basic metadata
};

enum nniso_compression {
  NNFS_UNCOMPRESSED = 0, // no compression, stored as is
  NNFS_DEFLATE = 1, // this means the stuff after compression is deflated, using the same deflate algorithm as the data card
};

struct nniso_chunk {
    varint_t padding; // amount of 0s to add after data
    varint_t datalen;
    char data[datalen];
};
```

