#ifndef PYCP_INTEGER_HPP
#define PYCP_INTEGER_HPP

#include "PycpObject.hpp"
#include <cstdint>
#include <string>

class PycpInteger : public PycpObject{
	private:
		int64_t _value;

	public:
		PycpInteger();
		PycpInteger(int);
		PycpInteger(const std::string&);
		PycpInteger(PycpString*);
		PycpInteger(PycpInteger*);
		virtual ~PycpInteger();

		int64_t get_value() const;

		PycpObject* __integer__() override;
		PycpObject* __string__() override;
		PycpObject* __addition__(PycpObject*) override;
		PycpObject* __subtraction__(PycpObject*) override;
		PycpObject* __multiplication__(PycpObject*) override;
		PycpObject* __division__(PycpObject*) override;

};

#endif //PYCP_INTEGER_HPP