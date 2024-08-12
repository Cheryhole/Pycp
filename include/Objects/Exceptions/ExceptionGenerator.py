ExceptionsPrefix = [
	"Index",
	"Syntax",
	"Type",
	"Value",
	"Runtime",
	"Import",
	"System",
]

ExceptionTemplate = """
#ifndef __PYCP_{excup}_EXCEPTION_HPP__
#define __PYCP_{excup}_EXCEPTION_HPP__

#include "PycpException.hpp"

namespace Pycp{{

class Pycp{exc}Exception : public PycpException{{
	public:
		Pycp{exc}Exception(const std::string& message, PycpException* traceback=nullptr, PycpSize_t line=UINT64_C(0)): 
			PycpException(message, traceback, line, "{exc}Exception"){{
		}}

}}; /* class Pycp{exc}Exception */

}} /* namespace Pycp */

#endif /* __PYCP_{excup}_EXCEPTION_HPP__ */
""".strip()

for exc in ExceptionsPrefix:
	filename = f"Pycp{exc}Exception.hpp"

	excfile = open(filename, "w", encoding="utf-8")
	exctext = ExceptionTemplate.format(excup=exc.upper(), exc=exc)
	excfile.write(exctext)
	excfile.close()

