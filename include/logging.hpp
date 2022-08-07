#ifndef LOGGING_HPP
#define LOGGING_HPP

#define LVL1 1
#define LVL2 2
#define LVL3 4
#define LVL4 8
#include <iostream>
#include <iomanip>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdarg.h>

using namespace std;

#define logmsg(...) Logger::sendLog(__FILE__, __LINE__, __VA_ARGS__);

class Logger {
	inline static int logLvl = LVL1;
	static void sendLog2(const char *fmt) {cout << fmt;}
	
	template <typename T, typename ...Ts>
	static void sendLog2(const char *fmt, T val, Ts ...args) {
		// now we go through the format, replacing % with the next arg.
		for (; *fmt; fmt++) {
			if (*fmt == '%') {
				cout << val;
				sendLog2(fmt + 1, args...);
				return;
			}
			cout << *fmt;
		}
	}
	
	public:
	
	template <typename ...Ts>
	static void sendLog(const char *file, int line, unsigned level, const char *fmt, Ts ...args) {
		if (!(logLvl & level)) return;
		
		static int_fast8_t logTime = -1;
		if (logTime == -1) {
			// check if we're outputting to journalctl, which timestamps for us.
			logTime = getenv("JOURNAL_STREAM") == NULL;
		}
		if (logTime) {
			time_t t = time(NULL);
			tm *t2 = localtime(&t);
			cout << '[' << put_time(t2, "%Y-%m-%dT%H:%M:%S") << "] ";
		}
		
		cout << '[' << file << ':' << line << "] ";
		// format and print the rest.
		sendLog2(fmt, args...);
		
		cout << endl;
	}
	
	static void setLogLvl(int lvl) {
		logLvl = lvl;
		logmsg(LVL1, "Set log level to %", lvl);
	}
	
};

#endif