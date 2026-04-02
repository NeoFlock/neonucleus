# NeoNucleus

The core of NeoComputers.
This repository has both NeoNucleus itself (NN) and the NeoNucleus Component Library (NCL).

These libraries provides:
- the base architecture, with computer states (for running machines), components, architectures, etc.
- (NCL) base component implementations for common ones (GPU/FS/tmpfs/EEPROM/etc.)
- fine control over component limits, with default references

They do not provide:
- Networking implementation, so you need to pass vtables for all network-related components
- Default architecture. While the testing emulator has a test version of the common Lua architecture seen in OpenComputers,
it is not propely sandboxed nor safe. Neither NN nor NCL provide any architectures.
