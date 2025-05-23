print(string.format("%d / %d", computer.usedMemory(), computer.totalMemory()))
print(string.format("Computer Address: %q, Tmp Address: %q", computer.address(), computer.tmpAddress()))

print(computer.getArchitecture())
print(table.unpack(computer.getArchitectures()))
