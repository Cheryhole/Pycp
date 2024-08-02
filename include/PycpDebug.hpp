#ifndef __PYCP_DEBUG_H__
#define __PYCP_DEBUG_H__

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "PycpDefines.h"

namespace Pycp{

#define PYCP_DEBUG
#ifdef PYCP_DEBUG

#define PycpDebugBlock(block) block

void PycpLog(const char* msg){
	FILE* logfile = fopen(PYCP_LOG_FILE, "a");
	time_t now = time(NULL);
	tm* local = localtime(&now);
	fprintf(logfile, "[%d.%d %d:%d:%d] %s\n",
		local->tm_mon, local->tm_mday, 
		local->tm_hour, local->tm_min, local->tm_sec, 
		msg);
	fclose(logfile);
}

#else // not defined PYCP_DEBUG

#define PycpDebugBlock(block)

template <typename MsgType>
void PycpLog(MsgType msg){
	;
}

#endif // PYCP_DEBUG


} // namespace Pycp

#endif // __PYCP_DEBUG_H__