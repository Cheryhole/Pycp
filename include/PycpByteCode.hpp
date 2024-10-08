#ifndef __PYCP_BYTE_CODE_HPP__
#define __PYCP_BYTE_CODE_HPP__ 1

#include <cstdint>

enum class PycpByteCode : uint8_t{
	LOAD_LOCAL = 0x01,
	STORE_LOCAL,
	LOAD_GLOBAL,
	STORE_GLOBAL,
	LOAD_CONST,
	LOAD_ATTR,
	BUILD_LIST,
	BUILD_TUPLE,
	BUILD_OBJECT,
	STORE_SUBSCR,
	LOAD_SUBSCR,
	DELETE_SUBSCR,
	OPERATOR,
	IMPORT,
	JUMP_IF_FALSE,
	CALL,
	RETURN
};


#endif // __PYCP_BYTE_CODE_HPP__