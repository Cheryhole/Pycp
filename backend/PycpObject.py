class PycpObject(object):
	def __init__(self):
		pass
	
	def __string__(self):
		return "<PycpObject>"
	
	def __integer__(self):
		raise TypeError(f"Unsupported to convert to integer.")
	
	def __addition__(self, other):
		raise TypeError(f"Unsupported for addition.")

	def __subtraction__(self, other):
		raise TypeError(f"Unsupported for subtraction.")
	
	def __multiplication__(self, other):
		raise TypeError(f"Unsupported for multiplication.")
	
	def __division__(self, other):
		raise TypeError(f"Unsupported for division.")