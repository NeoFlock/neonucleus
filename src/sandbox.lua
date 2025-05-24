print(component.doc("debugPrint", "log"))
print(component.invoke("debugPrint", "log", "Absolute cinema"))

computer.pushSignal("stuff", 123, "a", false, nil)
computer.pushSignal("stuf2", 456, "b", true, "shit")
computer.pushSignal("stuf3", 789, "c", false, -13)

print(computer.popSignal())
print(computer.popSignal())
print(computer.popSignal())
print(computer.popSignal())
