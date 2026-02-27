from . import *

class PycpString(str, PycpObject):
	_value: str = ""

	def __init__(self, value):
		self._value = str(value)

	def __string__(self):
		return self
	
	def __integer__(self):
		return PycpInteger(self._value)
	
	def __addition__(self, other):
		match type(other):
			case _ if isinstance(other, PycpString):
				return PycpString(self._value + other._value)
			case _:
				raise TypeError(f"Unsupported type for addition: {type(other)}")
	
	def __multiplication__(self, other):
		match type(other):
			case _ if isinstance(other, PycpInteger):
				return PycpString(self._value * other._value)
			case _:
				raise TypeError(f"Unsupported type for multiplication: {type(other)}")

	