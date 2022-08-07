#include <iostream>
#include <condition_variable>
#include <twitchState.hpp>
#include <twitchStructs.hpp>

twitchState *twitchState::instance = NULL;

twitchState *twitchState::getInstance() {
	if (!twitchState::instance) {
		twitchState::instance = new twitchState();
	}
	return twitchState::instance;
}

void twitchState::destroy() {
	delete twitchState::instance;
	twitchState::instance = NULL;
}

twitchState::~twitchState() {
	delete liveTime;
	liveTime = NULL;
}

twitchState::twitchState() {
	atexit(twitchState::destroy);
}

bool twitchState::live() {
	//there is a thread responsible for the liveTime pointer.  When the stream
	//goes live, that thread should allocate it and set its value.  When the stream goes
	//offline, that thread should delete it and set it to NULL
	return liveTime;
}