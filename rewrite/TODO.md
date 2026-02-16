# For MVP functionality

- `drive` component
- volatile filesystem
- volatile drive
- device info

# To re-evaluate

- The current system has the computer store the components and the component *types* store the state. Perhaps the ComponentType struct should be renamed
to ComponentState.
- More stack manipulation functions to allow libraries to have more APIs.
- Having a copy of the context stored directly in requests instead of having to use getComputerContext, as it simplifies the APIs and allows them
to be made more portable.
- Exposing more context wrappers than just the memory allocation functions.
- Exposing more internal functions that may be useful to the user to prevent pointless rewriting and duplicate machine code.

# Vanilla components needed

Not everything OC has (as a few of them are really MC-centered) but most of it.

- `data` component (note: maybe add hamming code support?)
- `modem` component
- `tunnel` component
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
- `cd_reader` and `cd_writer` components, to work with CDs
- `serial` component, for serial communications with other devices (USB?)
- `iron_noteblock` component
- `colorful_lamp` component

# To make it good

- make more stuff const if it can be. Gotta help out the optimizer
- write a bunch of unit tests to ensure the public API works correctly
- ensure OOMs are recoverable
- do a hude audit for bugs at some point

# To make it fast

NOTE: we're mostly bottlenecked by the architecture (typically a Lua VM) and the intentional bottlenecking from call costs.

- make signals use a circular buffer instead of a simple array
- use a hashmap for components (and device info), this may require reworking how iterating over them is handled
