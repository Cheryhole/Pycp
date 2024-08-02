#ifndef __PYCP_DEFINES_H__
#define __PYCP_DEFINES_H__

#include <cstdint>

class PycpObject;

#define PYCP_VERSION_MAJOR 0
#define PYCP_VERSION_MINOR 1
#define PYCP_LOG_FILE "pycp.log"

#define PycpCast(type, obj) dynamic_cast<type>(obj)
#define PycpCastobj(obj) reinterpret_cast<PycpObject*>(obj)
#define PycpCheckType(type, obj) (dynamic_cast<type>(obj) != nullptr)
#define PycpSize_c(num) UINT64_C(num)

typedef PycpObject* (*PycpCFunc)(PycpObject*);
typedef uint64_t PycpSize_t;

#endif // __PYCP_DEFINES_H__
