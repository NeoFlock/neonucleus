# Parity with Vanilla OC (only the stuff that makes sense for an emulator)

- Unmanaged drives (the `drive` component)
- `computer` component
- `modem` component
- `tunnel` component
- `data` component (with error correction codes)
- `redstone` component
- `hologram` component
- `internet` component

# The extra components

- `oled` component (OLED screen, a store of draw commands and resolution from NN's perspective)
- `ipu` component, an Image Processing Unit. Can bind with `oled`s, and issues said draw commands.
- `vt`, a virtual terminal with ANSI-like escapes. (and a function to get its resolution)
- (maybe) `qpu` component, a Quantum Processing Unit for quantum computing.
- `radio_controller` and `radio_tower` components, for radio telecommunications.
- `clock` component for arbitrary precision time-keeping.

# Internal stuff

- custom atomic and lock support
- no longer depend on libc functions
- no longer depend on libc headers
- no longer link any libc when NN_BAREMETAL.
