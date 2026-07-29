import PycpRuntime4Python as Pycp

Pycp.Initialize()

obj = Pycp.Object()
s = Pycp.String("Hello World")
Pycp.BuiltinFunction.print(s)
a = obj.__addition__(s)

Pycp.Finalize()
