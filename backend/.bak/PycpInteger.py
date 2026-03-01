from . import *

class PycpInteger(int, PycpObject):
	_value: int = 0

	def __init__(self, value):
		self._value = int(value)

	def __string__(self):
		return PycpString(self._value)
	
	def __integer__(self):
		return self
	
	def __negation__(self):
		return PycpInteger(-self._value)

	def __addition__(self, other):
		match type(other):
			case _ if isinstance(other, PycpInteger):
				return PycpInteger(self._value + other._value)
			case _:
				raise TypeError(f"Unsupported type for addition: {type(other)}")
	
	def __subtraction__(self, other):
		match type(other):
			case _ if isinstance(other, PycpInteger):
				return PycpInteger(self._value - other._value)
			case _:
				raise TypeError(f"Unsupported type for subtraction: {type(other)}")
	
	def __multiplication__(self, other):
		match type(other):
			case _ if isinstance(other, PycpInteger):
				return PycpInteger(self._value * other._value)
			case _ if isinstance(other, PycpString):
				return PycpString(self._value * other._value)
			case _:
				raise TypeError(f"Unsupported type for multiplication: {type(other)}")
	
	def __division__(self, other):
		match type(other):
			case _ if isinstance(other, PycpInteger):
				return PycpInteger(self._value / other._value)
			case _:
				raise TypeError(f"Unsupported type for division: {type(other)}")

	