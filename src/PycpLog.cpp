#include "PycpLog.hpp"

namespace Pycp {
namespace PycpLog{

char* GetFormatedTime(){
	char* buffer;
	time_t tick = time(NULL);
	tm* timetable = localtime(&tick);
	int year = timetable->tm_year + 1900;
	int month = timetable->tm_mon + 1;
	int monthday = timetable->tm_mday;
	int hour = timetable->tm_hour;
	int minute = timetable->tm_min;
	int second = timetable->tm_sec;

	sprintf(buffer, "[%04d-%02d-%02d %02d:%02d:%02d]", year, month, monthday, hour, minute, second);
}

static void baseprint(int level, const char* propmt, const char* msg, ...){
	va_list args;
	va_start(args, msg);
	if (level >= LogLevel){
		char* fmt;
		sprintf(fmt, "[%s] %s %s\n",propmt, GetFormatedTime(), msg);
		fprintf(LogFile, fmt, args);
	}
	va_end(args);
}

void debug(const char* msg, ...){
	va_list args;
	va_start(args, msg);
	baseprint(0, "DEBUG", msg, args);
	va_end(args);
}

void info(const char* msg, ...){
	va_list args;
	va_start(args, msg);
	baseprint(1, "INFO", msg, args);
	va_end(args);
}

void warn(const char* msg, ...){
	va_list args;
	va_start(args, msg);
	baseprint(2, "WARN", msg, args);
	va_end(args);
}

void error(const char* msg, ...){
	va_list args;
	va_start(args, msg);
	baseprint(3, "DEBUG", msg, args);
	va_end(args);
}

} // namespace PycpLog
} // namespace Pycp