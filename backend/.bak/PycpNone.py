from . import *

class PycpNone(PycpObject):
	inst: PycpNone | None = None

	def __string__(self):
		return PycpString("None")
	
	__str__ = __repr__ = lambda self: "None"
	
	def __integer__(self):
		return PycpInteger(0)
	
PycpNone.inst = PycpNone()
