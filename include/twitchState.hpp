#ifndef TWITCH_STATE_HPP
#define TWITCH_STATE_HPP

#include <iostream>
#include <condition_variable>
#include <mutex>
#include <twitchStructs.hpp>
#include <twitchIRCConnection.hpp>

using namespace std;

struct twitchState {
	twitchMessage msg;
	
	uint64_t *liveTime = NULL;
	condition_variable liveTimeCV;
	mutex liveTimeMute;
	
	twitchIRCConnection conn;
	
	bool live();
	//there is a thread responsible for managing the liveTime pointer.  it will delete
	//the pointer and set it to NULL if the channel goes offline.
	
	static twitchState *getInstance();
	static void destroy();
	
	private:
	
	twitchState();
	~twitchState();
	
	static twitchState *instance;
};

#endif