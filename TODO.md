# Parity with Vanilla OC (only the stuff that makes sense for an emulator)

- rework literally all the costs to just be heat and amount per tick
- change more methods to be direct but with buffered indirects
- complete the GPU implementation (screen buffers and missing methods)
- complete the screen implementation (bunch of missing methods)
- in-memory version of `filesystem` and `drive`
- `hologram` component
- `computer` component
- `modem` component
- `tunnel` component
- `data` component (with error correction codes and maybe synthesizing audio)
- `redstone` component
- `internet` component
- `disk_drive` component
- `computer.getDeviceInfo()`, and subsequently, component device information
- `computer.beep(frequency?: number, duration?: number, volume?: number)`, frequency between 20 and 2000 Hz, duration up to 5 seconds, volume from 0 to 1.
Should use the *busy* machine state to let the sandbox suspend it until it is done playing
- ensure the recursive locks are used correctly to prevent race conditions
- use dynamic arrays for signals (and maybe components)
- support invalid UTF-8 for GPU set and fill, which should pretend the byte value is the codepoint.

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
- `speaker` component, allows playing audio by asking for binary samples and pushing a signal when it needs more
- `microphone` component, allows reading audio from nearby sources
- `tape_drive` component, compatible with Computronics, except maybe with proper seek times and support for multiple tapes
- `cd_reader` and `cd_writer` components, to work with CDs

# Internal stuff

- custom atomic, lock and improved custom clock support. Perhaps generalizing it to an nn_Context
- no longer depend on libc functions
- no longer depend on libc headers
- no longer link any libc when NN_BAREMETAL
