# For MVP functionality

- add brightness to screens, which power usage scales with, each different tier has a different max brightness as well
- write a tester OS, basically a menu with tests to run
- finish tmpfs (rework the whole thing)
- device info
- finish `computer` components
- userdata support

# To re-evaluate

- Exposing the internal non-resizing hashmap implementation.
- More stack manipulation functions to allow libraries to have better APIs. (rotate)
- Having a copy of the context stored directly in requests instead of having to use getComputerContext, as it simplifies the APIs and allows them
to be made more portable.
- Exposing more internal functions that may be useful to the user to prevent pointless rewriting and duplicate machine code.

# Vanilla components needed

Not everything OC has (as a few of them are really MC-centered) but most of it.

- `data` component (note: deflate, sha256 and aes impl are callbacks, to keep NN small and simple)
- `modem` component
- `tunnel` component
- `internet` component (note: NN does not handle internet requests, those are up to the emulator)
- `relay` component (note: OCDoc still refers to it as `access_point`)
- `computer` component
- `geolyzer` component
- `net_splitter` component
- `redstone` component
- `motion_sensor` component
- `printer3d` component (note: maybe add signal for when printer completes?)
- `sign` component
- `microcontroller` component
- `hologram` component
- `disk_drive` component
- `note_block` component

# Custom components needed

- `oled` component (OLED screen, a store of draw commands and resolution from NN's perspective)
- `ipu` component, an Image Processing Unit. Can bind with `oled`s, and issues said draw commands
- `vt`, a combination of a GPU, a screen and a keyboard with limited functionality. Offers direct operations on the video memory
- (maybe) `qpu` component, a Quantum Processing Unit for quantum computing
- `radio_controller` and `radio_tower` components, for radio telecommunications
- (maybe) `clock` component for arbitrary precision time-keeping
- `led` component for LED matrixes and/or LED lights
- `speaker` component, allows playing audio by asking for binary samples and pushing a signal when it needs more
- `microphone` component, allows reading audio from nearby sources
- `tape_drive` component, compatible with Computronics
- `cd_drive` to work with CDs (can be read-only, write-only or read-write)
- `serial` component, for serial communications with other devices (see Serial)
- `iron_noteblock` component
- `colorful_lamp` component
- OpenSolidState flash storage

# To make it good

- make more stuff const if it can be. Gotta help out the optimizer
- write a bunch of unit tests to ensure the public API works correctly
- ensure OOMs are recoverable
- do a hude audit for bugs at some point

# To make it fast

NOTE: we're mostly bottlenecked by the architecture (typically a Lua VM) and the intentional bottlenecking from call costs.

- make signals use a circular buffer instead of a simple array
- use more arenas if possible

# Component extensions

## filesystem

- `getMaxRead(): integer`, returns the maximum size of a read; effectively the ideal buffer size

## drive

- `readUByte(byte: integer): integer`, reads an unsigned byte


# Unique components

Subject to change, still being discussed with the other NeoFlock members.

## Serial

The `serial` component would have the methods:
- `read(len: integer): string`, to read data
- `write(data: string): boolean`, to send data
- `isConnected(): boolean`, to check if the port is currently connected
- `setProtocol(protocol: string): string`, sets the protocol string of our port, which may be truncated.
- `getProtocol(): string`, gets the currently registered protocol of our port. Default is `raw`.

It would also push the following signals:
- `serial_connected(portAddress: string, protocol: string)`, when connected
- `serial_protocol(portAddress: string, protocol: string)`, when other side changed protocol mid-connection.
- `serial_disconnected(portAddress: string)`, when the port is disconnected
- `serial_data(portAddress: string, amount: integer)`, when new data is available (and how much)

## Radio

For very large-distance telecommunications. Frequency is in Hz.

The `radio_controller` component, which enables listening to connected radio towers
- `isOpen(): boolean`, whether listening is enabled
- `open(): boolean`, to open for listening
- `close(): boolean`, to close the listener
- `setRange(minimum: number, maximum: number): boolean`, sets the frequency range we listen for.
- `getRange(): number, number`, gets the frequency range we listen for.

The `radio_tower` component, which actually receives and sends the waves
- `isEmitter(): boolean`, whether this tower can, in fact, send radio waves
- `minFrequency: number`, the minimum frequency it can detect and transmit (getter field)
- `maxFrequency: number`, the maximum frequency it can detect and transmit (getter field)
- `resolution: integer`, how many frequencies can be listened for in that range. The frequencies sent to the controller are rounded to one of
these (getter field)
- `getRangeOf(frequency: number): number`, to get the maximum range for a given frequency
- `maxPacketSize(): integer`, to get the maximum size that can be sent at once, typically 32KiB.
- `baseFrequency: number`, gets the base frequency of radio communications, typically 100 kHz. (getter field)
- `emit(frequency: number, data: string): boolean`, to emit a radio packet on a given frequency. The frequency must be in range, and will be rounded to one
within resolution

Radio waves travel at a (likely configurable) speed and have extremely large range on lower frequencies.
They can also have bit flips.

The range and speed are based off the frequency range. Given base frequency f0, base energy cost e0, base range r0, we can compute energy cost e, range r of a
given frequency f using:
- `r = r0 / (f / f0)`
- `e = e0 * (f / f0)`

When one is received, it will push:
- `radio_message(localControllerAddress: string, localTowerAddress: string, distance: number, frequency: number, data: string)`, where frequency is rounded to
within the resolution of the tower. It does ensure the frequency is within the controller range and the controller is open. Data may be corrupted by bit flips.
Distance is in meters. As you can notice, there is no sender address. It is the responsibility of the protocol to identify and verify senders.

## VT

A generic virtual terminal. Like a GPU + screen, but with no VRAM.
Has a 256-color palette representing ANSI 256 colors.
Subsequently, colors 0-15 represent ANSI escape colors.
The entire palette is editable.

The `vt` component has:
- `getResolution(): integer, integer`, to get the resolution. Cannot be changed
- `getDepth(): integer`, to get the color depth. Cannot be changed
- `getColor(index: integer): integer`, to get a color
- `setColor(index: integer, color: integer): integer`, sets a color and returns the old one. Characters who's colors were table indexes would be updated
- `setForeground(color: integer, fromTable?: boolean)`, sets a foreground color, optionally as a table index
- `getForeground(): integer, integer?`, returns what the foreground color was, and the palette index if applicable
- `setBackground(color: integer, fromTable?: boolean)`, sets a background color, optionally as a table index
- `getBackground(): integer, integer?`, returns what the background color was, and the palette index if applicable
- `set(idx: integer, data: string): boolean`, to write to the screen's unicode buffer. While `data` is UTF-8, the buffer stores codepoints. Pretend
the terminal does an internal conversion from UTF-8 to UTF-32
- `getColorOf(idx: integer): integer, integer, integer?, integer?`, to get the foreground color, background color, and palette indexes of a tile
- `get(idx: integer, len: integer): string`, to get the contents of part of the VT's unicode buffer, encoded as UTF-8

It also pushes the signals of keyboards and screens (no precise) for using input.

The unicode buffer is a 0-indexed buffer of codepoints alongside the color data. When writing to the unicode buffer, it also writes to the color buffer.
If `x` and `y` are 0-indexed, then `x + y * width` is the index in the buffer

## CD drives

The `cd_drive` component has:
- `hasDisk(): boolean`, to check whether a disk is in the drive
- `isReadonly(): boolean`, to check where the drive is read-only (often is)
- `isDiskErasable(): boolean`, to check whether the disk is erasable, which requires support from both the disk and the drive
- `tell(): integer`, current 0-indexed byte offset into the disk
- `seekTo(position: integer): boolean`, seek to a current 0-indexed byte offset in the disk
- `moveBy(delta: integer): boolean`, move the current position by some amount of bytes (+/-), can wrap around
- `maxReadSize(): integer`, to return the maximum size of a read
- `read(len: integer): string`, to read some data. Does wrap around
- `write(data: string): boolean`, to write some data. Does wrap around.

It also pushes the signals:
- `cd_added(driveAddress: string)`, for when a disk is added
- `cd_removed(driveAddress: string)`, for when a disk is removed

### Writing to non-erasable drives

Instead of erroring out, it instead ORs the bits between what was there and what is written. This means you can still write the initial contents,
as the CD starts out 0'd, but subsequent writes are likely to corrupt previous ones.

### CDs vs tape

Tape is slow. CDs are fast.
Tape is always fully writable. CDs may not always be erasable.
Tape has high capacity, CDs do not.

### CDs vs unmanaged floppies

CDs are slower for random reads as they have no cache.

## LED

The `led_matrix` component has:
- `getResolution(): integer, integer`, gets the resolution of the LED matrix.
- `getIntensity(x: integer, y: integer): number`, gets the intensity of an LED.
- `setIntensity(x: integer, y: integer, intensity: number): number`, sets the intensity of an LED and returns the old one.

Intensity is between 0 (off) and 1 (full brightness).
An LED matrix has really high call budget, thus it is ideal for frequency updating status updates.

## nandflash

Heavily inspired by ossm_flash

- `getWearLevel(): number`, returns a number from 0 to 100, where 0 means full life and 100 means dead
- `getSectorSize(): integer`, returns the logical sector size
- `getCapacity(): integer`, the capacity, in bytes, of the flash storage
- `getLayers(): integer`, returns the layering amount, for example 3 for TLC. Effectively an indication for lifetime, with higher being worse.
- `readSector(sec: integer): string`, read a sector
- `writeSector(sec: integer, data: string): boolean`, write a sector
- `readByte(byte: integer): integer`, reads a signed byte
- `readUByte(byte: integer): integer`, reads an unsigned byte
- `writeByte(byte: integer, val: integer): boolean`, writes a byte
- `isReadonly(): boolean`, check whether flash is read-only
- `getLabel(): string?`, get drive label
- `setLabel(label: string?): string?`, get drive label

## Speaker

TODO: interface

## Microphone

TODO: interface

## OLED/IPU

TODO: interface
