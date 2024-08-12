#ifndef __PYCP_LOG_H__
#define __PYCP_LOG_H__

#include <iostream>
#include <cstdio>
#include <ctime>
#include <cstdarg>
#include "PycpDefines.hpp"

namespace Pycp{
namespace PycpLog{

int LogLevel = 0;
FILE* LogFile = stdout;

// "yy.mm.dd hh:mm:ss"
char* GetFormatedTime();

void debug(const char* msg, ...); // level 0
void info(const char* msg, ...);	// level 1
void warn(const char* msg, ...); // level 2
void error(const char* msg, ...); // level 3

} // namespace PycpLog
} // namespace Pycp

#endif // __PYCP_LOG_H__