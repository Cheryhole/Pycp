from . import *

class ReturnException(Exception):
	def __init__(self, value):
		self.value = value

class PycpFunction(PycpObject):
	_builtin: bool = False

	def __init__(self, name: str, params: list[str], body, env):
		self.name = name
		self.params = params
		self.body = body
		self.env = env

	def __string__(self):
		return f"<function {self.name}>"
	
	__str__ = __repr__ = __string__
	
	def is_builtin(self):
		return self._builtin

	def __call__(self, *args):
		pass

class PycpBuiltinFunction(PycpFunction):
	print: "PycpBuiltinPrintFunction | None" = None

	def __init__(self, name: str, params: list[str]):
		self.name = name
		self.params = params
		self._builtin = True

	def __string__(self):
		return f"<builtin function {self.name}>"

	__str__ = __repr__ = __string__

	def __call__(self, *args):
		return PycpNone.inst

class PycpBuiltinPrintFunction(PycpBuiltinFunction):
	def __init__(self):
		super().__init__("print", ["arg"])

	def __call__(self, arg):
		print(arg)
		return PycpNone.inst

PycpBuiltinFunction.print = PycpBuiltinPrintFunction()

