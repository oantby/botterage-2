#ifndef TWITCH_EVENTS_HPP
#define TWITCH_EVENTS_HPP

#include <twitchStructs.hpp>
#include <commands.hpp>

namespace twitchEvents {
	void subHype(twitchMessage msg);
	void shoutout(const twitchMessage *msg, string *r, vector<command *> *matches);
	void raid(const twitchMessage *msg, string *r, vector<command *> *matches);
}

#endif