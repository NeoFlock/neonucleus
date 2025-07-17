# Code quality stuff

- Use the same namespacing style everywhere, that being
`nn_<class>_<method>` for functions related to "classes",
`nn_<function>` for static functions,
`nn_<type>_t` for types.
- Rework to API to be much more future-proof to reduce potential breaking changes.

# Parity with Vanilla OC (only the stuff that makes sense for an emulator)

- in-memory version of `filesystem`
- complete the GPU implementation (screen buffers and missing methods)
- complete the screen implementation (bunch of missing methods)
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

# Bugfixes

- Do a huge audit at some point
- `nn_unicode_charWidth` appears to be bugged, look into that.

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
- `serial` component, for serial communications with other devices (USB?)

# Internal changes

- rework some interfaces to account for possibility of errors
- use dynamic arrays for signals (and maybe components), but still keep the maximums to prevent memory hogging
- setup an extensive testing system to find bugs easier
- check if ports are open in `nn_pushNetworkMessage`
