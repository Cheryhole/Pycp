#ifndef __PYCP_DEFINES_H__
#define __PYCP_DEFINES_H__

#include <cstdint>

class PycpObject;

#define PYCP_VERSION_MAJOR 0
#define PYCP_VERSION_MINOR 1
#define PYCP_LOG_FILE "Pycp.log"

#define PycpCast(type, obj) (reinterpret_cast<type>(obj))
#define PycpCastobj(obj) (reinterpret_cast<PycpObject*>(obj))
#define PycpCheckType(type, obj) (dynamic_cast<type>(obj) != nullptr)

#define PycpSize_c(num) (UINT64_C(num))
#define PycpSize_0 (PycpSize_c(0))
#define PycpSize_1 (PycpSize_c(1))
#define PycpSize_2 (PycpSize_c(2))

typedef PycpObject* (*PycpCFunc)(PycpObject*);
typedef uint64_t PycpSize_t;

#endif // __PYCP_DEFINES_H__
