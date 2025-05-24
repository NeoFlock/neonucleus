print(component.doc("debugPrint", "log"))
local a, b, c = component.invoke("debugPrint", "log", "Absolute cinema")
print(a, b, c)

computer.pushSignal("stuff", 123, "b", false, nil)

print(computer.popSignal())
