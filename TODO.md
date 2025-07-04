# Parity with Vanilla OC (only the stuff that makes sense for an emulator)

- rework literally all the costs to just be heat and amount per tick
- change more methods to be direct but with buffered indirects
- complete the GPU implementation (screen buffers and missing methods)
- `computer` component
- `modem` component
- `tunnel` component
- `data` component (with error correction codes)
- `redstone` component
- `hologram` component
- `internet` component
- use dynamic arrays for signals

# Bugfixes

- Rework filesystem component to pre-process paths to ensure proper sandboxing and not allow arbitrary remote file access
- Do a huge audit at some point

# The extra components

- `oled` component (OLED screen, a store of draw commands and resolution from NN's perspective)
- `ipu` component, an Image Processing Unit. Can bind with `oled`s, and issues said draw commands
- `vt`, a virtual terminal with ANSI-like escapes. (and a function to get its resolution)
- (maybe) `qpu` component, a Quantum Processing Unit for quantum computing
- `radio_controller` and `radio_tower` components, for radio telecommunications
- (maybe) `clock` component for arbitrary precision time-keeping
- `led` component for LED matrixes and/or LED lights

# Internal stuff

- custom atomic, lock and improved custom clock support. Perhaps generalizing it to an nn_Context
- no longer depend on libc functions
- no longer depend on libc headers
- no longer link any libc when NN_BAREMETAL
