# NeoNucleus

The core of NeoComputers.

It provides:
- the computer model and state implementation
- architecture system
- (NOT NOW) basic component implementations
- (NOT NOW) standard emulator
- (NOT NOW) some extra components

The library does not provide:
- The sandbox (equivalent to OpenComputer's `machine.lua`)
- Default architectures
- Default host interop (as in, the vtables that control the basic component's internals, such as the filesystem implementation)

The emulator *will* (as its gonna be made after the engine is functional) provide:
- A simple Lua sandbox
- Very simple workspaces
- Ocelot components for debug
- Headless mode (single computer, uses actual terminal for a teletypewriter).
