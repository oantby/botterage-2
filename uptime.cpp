/* SHOULD be loaded AFTER the commands module for good measure */
/*
@var uptime_offline (The stream is currently offline): The message to display for uptime when offline
@var puptime_offline (Shhhhh, the puppers are sleeping <3): The message to display for puptime when offline
*/
#include <uptime.hpp>
#include <iostream>
#include <twitchState.hpp>
#include <commands.hpp>
#include <twitchStructs.hpp>
#include <utils.hpp>

using namespace std;


namespace uptime {
	void replacePuptime(const twitchMessage *msg, string *resp, vector<command *> *matches);
	void replaceUptime(const twitchMessage *msg, string *resp, vector<command *> *matches);
	
	void replaceUptime(const twitchMessage *msg, string *resp, vector<command *> *matches) {
		size_t spot = 0;
		
		twitchState *state = twitchState::getInstance();
		uint64_t startTime;
		{
			//using a lock just to grab a pointer is a bit of overkill, but I'll
			//just use the term "good practice" instead
			lock_guard<mutex> lk(state->liveTimeMute);
			
			if (!state->liveTime) {
				*resp = getVar("uptime_offline", "The stream is currently offline");
				return;
			}
			
			startTime = *state->liveTime;
		}
		
		string uptime;
		
		startTime = time(NULL) - startTime;
		unsigned days, hours, minutes, seconds;
		days = startTime/86400;
		startTime -= days*86400;
		hours = startTime/3600;
		startTime -= hours*3600;
		minutes = startTime/60;
		startTime -= minutes*60;
		seconds = startTime;
		startTime = 0;
		
		if (days) {
			uptime += to_string(days);
			uptime += (days > 1 ? " days" : " day");
		}
		if (hours) {
			if (days) {
				uptime += ", ";
			}
			uptime += to_string(hours);
			uptime += (hours > 1 ? " hours" : " hour");
		}
		if (minutes) {
			if ((days)||(hours)) {
				uptime += ", ";
			}
			uptime += to_string(minutes);
			uptime += (minutes > 1 ? " minutes" : " minute");
		}
		if ((days)||(hours)||(minutes)) {
			if ((((bool)days) + ((bool)hours) + ((bool)minutes)) > 1) {
				uptime += ",";
			}
			uptime += " and ";
		}
		uptime += to_string(seconds) + (seconds == 1 ? " second" : " seconds");
		
		while ((spot = resp->find("$(uptime)",spot)) != string::npos) {
			resp->replace(spot,9,uptime);
			spot += 9;
		}
	}
	
	void replacePuptime(const twitchMessage *msg, string *resp, vector<command *> *matches) {
		//this is identical to uptime, except that I multiply the uptime by 7 just
		//before calculating it.
		size_t spot = 0;
		
		twitchState *state = twitchState::getInstance();
		uint64_t startTime;
		{
			//using a lock just to grab a pointer is a bit of overkill, but I'll
			//just use the term "good practice" instead
			lock_guard<mutex> lk(state->liveTimeMute);
			
			if (!state->liveTime) {
				*resp = getVar("puptime_offline", "Shhhhh, the puppers are sleeping <3");
				return;
			}
			
			startTime = *state->liveTime;
		}
		
		string uptime;
		
		startTime = 7*(time(NULL) - startTime);
		unsigned days, hours, minutes, seconds;
		days = startTime/86400;
		startTime -= days*86400;
		hours = startTime/3600;
		startTime -= hours*3600;
		minutes = startTime/60;
		startTime -= minutes*60;
		seconds = startTime;
		startTime = 0;
		
		if (days) {
			uptime += to_string(days);
			uptime += (days > 1 ? " days" : " day");
		}
		if (hours) {
			if (days) {
				uptime += ", ";
			}
			uptime += to_string(hours);
			uptime += (hours > 1 ? " hours" : " hour");
		}
		if (minutes) {
			if ((days)||(hours)) {
				uptime += ", ";
			}
			uptime += to_string(minutes);
			uptime += (minutes > 1 ? " minutes" : " minute");
		}
		if ((days)||(hours)||(minutes)) {
			if ((((bool)days) + ((bool)hours) + ((bool)minutes)) > 1) {
				uptime += ",";
			}
			uptime += " and ";
		}
		uptime += to_string(seconds) + (seconds == 1 ? " second" : " seconds");
		
		while ((spot = resp->find("$(puptime)",spot)) != string::npos) {
			resp->replace(spot,10,uptime);
			spot += 9;
		}
	}
	
};


extern "C" void uptime_init(void *args) {
	cmdVars["$(uptime)"].push_back(uptime::replaceUptime);
	cmdVars["$(puptime)"].push_back(uptime::replacePuptime);
}