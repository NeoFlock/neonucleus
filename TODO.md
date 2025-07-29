# Code quality stuff

- Use the same namespacing style everywhere, that being
`nn_<class>_<method>` for functions related to "classes",
`nn_<function>` for static functions,
`nn_<type>_t` for types.
- Get rid of `nn_address`
- Make a lot more stuff const
- Rework to API to be much more future-proof to reduce potential breaking changes.

# Parity with Vanilla OC (only the stuff that makes sense for an emulator)

- `hologram` component
- `computer` component
- `data` component (with error correction codes and maybe synthesizing audio)
- `redstone` component
- `internet` component
- `computer.beep(frequency?: number, duration?: number, volume?: number)`, frequency between 20 and 2000 Hz, duration up to 5 seconds, volume from 0 to 1.

# Bugfixes

- Do a huge audit at some point
- `nn_unicode_charWidth` appears to be bugged, look into that.
- validate against moving `a/b` into `a` or moving `a` into `a/b` in the filesystem component.

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

- use more arenas!!!!!!!
- make sure OOMs are recoverable
- rework some interfaces to use pre-allocated or stack-allocated memory more
- use dynamic arrays for signals (and maybe components), but still keep the maximums to prevent memory hogging
- setup an extensive testing system to find bugs easier
- optimize the codebase by using globals instead of universe userdata
- use compiler hints to let the optimizer make the code even faster
- (maybe) rework a bunch of internal structures to use tagged unions over vtables (such as components). This may make certain APIs unnecessary or slightly
different. This can improve performance at the cost of making the codebase more complex
