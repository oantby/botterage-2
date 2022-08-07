/*
For the autodocumenter.
@var newsub_msg: The message to be used for a new subscriber. $(user) will be replaced with the user's name.
@var happy_emotes: The list of emotes that comprise a resub message, split by spaces. An n month resub gets n random emotes from the list.
@var subgift_msg: The message to be used when a sub is gifted. $(giver) and $(receiver) will be replaced in the message.
@var shoutout_initial (Go check out $(toUser) at twitch.tv/$(toUser)): First message to send for a shoutout. $(toUser) replaced with second word of message.
@var shoutout_followup: Subsequent message to be repeated for a shoutout. $(toUser) replaced with second word of message.
@var shoutout_followup_count (0): How many times to send shoutout_followup.
@var raid_initial (The stream is over, so we're heading to twitch.tv/$(toUser)!): First message to send for a raid command. $(toUser) replaced with second word of message.
@var raid_followup: Subsequent message to be repeated for a raid. $(toUser) replaced with the second word of message.
@var raid_followup_count (0): How many times to send raid_followup.
*/

#include <twitchStructs.hpp>
#include <botterage.hpp>
#include <twitchState.hpp>
#include <twitchEvents.hpp>
#include <utils.hpp>
#include <vector>
#include <commands.hpp>
#include <stringextensions.hpp>

using namespace std;

extern "C" void twitchevents_init(void *args) {
	subListeners.push_back(twitchEvents::subHype);
	cmdVars["$(shoutout)"].push_back(twitchEvents::shoutout);
	cmdVars["$(raid)"].push_back(twitchEvents::raid);
}

namespace twitchEvents {
	
	void subHype(twitchMessage msg) {
		twitchState *state = twitchState::getInstance();
		switch (msg.messageType) {
			case twitchMessage::NEWSUB:
				{
					string toSend = getVar("newsub_msg");
					for (size_t pos; (pos = toSend.find("$(user)")) != string::npos; pos++) {
						toSend.replace(pos, strlen("$(user)"), msg.user.displayName);
					}
					
					if (toSend.size()) state->conn.sendMessage(toSend);
				}
				break;
			case twitchMessage::RESUB:
				{
					string response = "/me ";
					vector<string> happyEmotes = split(getVar("happy_emotes"), ' ');
					if ((happyEmotes.size() == 1 && happyEmotes[0].empty()) || happyEmotes.empty()) {
						return;
					}
					uint16_t months = 0;
					try {
						months = stoi(msg.tags["msg-param-cumulative-months"]);
					} catch (invalid_argument &e) {
						logmsg(LVL1, "Couldn't convert value to integer: %", e.what());
						return;
					} catch (out_of_range &e) {
						logmsg(LVL1, "Months value \"%\" out of integer range", msg.tags["msg-param-cumulative-months"]);
						return;
					}
					for (uint16_t i = 0; i < months; i++) {
						response += happyEmotes[randomNumber()%happyEmotes.size()] + " ";
					}
					state->conn.sendMessage(response);
				}
				break;
			case twitchMessage::SUBGIFT:
				{
					string toSend = getVar("subgift_msg");
					for (size_t pos; (pos = toSend.find("$(giver)")) != string::npos; pos++) {
						toSend.replace(pos, strlen("$(giver)"), msg.tags["display-name"]);
					}
					for (size_t pos; (pos = toSend.find("$(receiver)")) != string::npos; pos++) {
						toSend.replace(pos, strlen("$(receiver)"), msg.tags["msg-param-recipient-display-name"]);
					}
					if (toSend.size()) state->conn.sendMessage(toSend);
				}
				break;
			default:
				logmsg(LVL1, "I don't know what's going on. Here's the text: %", msg.orig);
				break;
		}
	}
	
	void shoutout(const twitchMessage *msg, string *r, vector<command *> *matches) {
		r->clear();//we'll send our own messages
		if (!msg->user.mod) return;
		
		twitchState *state = twitchState::getInstance();
		
		vector<string> parts = split(msg->message,' ');
		if (parts.size() < 2) return;
		
		string person = parts[1];
		
		// now we determine the message to be sent.
		string toSend = getVar("shoutout_initial",
			"Go check out $(toUser) at twitch.tv/$(toUser)");
		
		for (size_t pos; (pos = toSend.find("$(toUser)")) != string::npos; pos++) {
			toSend.replace(pos, strlen("$(toUser)"), person);
		}
		
		if (toSend.size()) state->conn.sendMessage(toSend);
		
		// now see if we've got some repetition here for entertainment.
		toSend = getVar("shoutout_followup");
		if (!toSend.size()) return;
		int count = 0;
		try {
			count = stoi(getVar("shoutout_followup_count", "0"));
		} catch (...) {
			logmsg(LVL1, "shoutout_followup_count couldn't be interpreted as a number");
			count = 0;
		}
		
		for (size_t pos; (pos = toSend.find("$(toUser)")) != string::npos; pos++) {
			toSend.replace(pos, strlen("$(toUser)"), person);
		}
		
		for (int i = 0; i < count; i++) {
			state->conn.sendMessage(toSend);
		}
	}
	
	void raid(const twitchMessage *msg, string *r, vector<command *> *matches) {
		r->clear();
		if (!msg->user.mod) return;
		
		twitchState *state = twitchState::getInstance();
		
		vector<string> parts = split(msg->message,' ');
		if (parts.size() < 2) return;
		
		string person = parts[1];
		
		string toSend = getVar("raid_initial",
			"The stream is over, so we're heading to twitch.tv/$(toUser)!");
		
		for (size_t pos; (pos = toSend.find("$(toUser)")) != string::npos; pos++) {
			toSend.replace(pos, strlen("$(toUser)"), person);
		}
		
		if (toSend.size()) state->conn.sendMessage(toSend);
		
		int count = 0;
		toSend = getVar("raid_followup");
		try {
			count = stoi(getVar("raid_followup_count", "0"));
		} catch (...) {
			logmsg(LVL1, "raid_followup_count couldn't be interpreted as an int");
			count = 0;
		}
		
		for (size_t pos; (pos = toSend.find("$(toUser)")) != string::npos; pos++) {
			toSend.replace(pos, strlen("$(toUser)"), person);
		}
		
		if (!toSend.size()) return;
		
		for (int i = 0; i < count; i++) {
			state->conn.sendMessage(toSend);
		}
	}
}
