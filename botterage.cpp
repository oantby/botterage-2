/*
@var twitch_channel: the channel on which this bot should operate
@var twitch_user: the user as whom this bot should log in
@var twitch_token*: the login token for the user
*/
#define MYSQLPP_SSQLS_NO_STATICS
#include <botterage-db.hpp>
#include <iostream>
#include <twitchIRCConnection.hpp>
#include <twitchState.hpp>
#include <twitchStructs.hpp>
#include <DBConnPool.hpp>
#include <thread>
#include <mutex>
#include <functional>
#include <set>
#include <utils.hpp>


using namespace std;

vector<function<void(twitchMessage)> > chatMsgListeners, subListeners,
                                    whisperListeners, raidListeners,
                                    ritualListeners, msgListeners;

// call init function for any modules implementing it.
void initAll() {
	invoke_hook("init", NULL);
}

int main(int argc, char *argv[]) {
	
	// collect the necessary environment variables.
	const char *DBNAME = getenv("DBNAME");
	const char *DBUSER = getenv("DBUSER");
	const char *DBPASS = getenv("DBPASS");
	const char *DBSERV = getenv("DBSERV");
	
	
	DBConnPool::initialize(DBNAME,DBUSER,DBPASS,DBSERV);
	
	FILE *popenFile = NULL;
	string logInfo = getVar("log_type");
	if (logInfo == "rotate") {
		logmsg(LVL2, "Switching to log rotation");
		popenFile = popen("rotatelogs -L botterage.log -n 3 botterage.out 10K","w");
		if (popenFile) {
			dup2(fileno(popenFile),1);
			dup2(fileno(popenFile),2);
		} else {
			logmsg(LVL1, "Couldn't open rotatelogs for writing");
		}
	}
	
	if ((logInfo = getVar("log_lvl")) != "") {
		Logger::setLogLvl(stoi(logInfo));
	}
	
	const string
		twitchChannel = getVar("twitch_channel"),
		twitchUser = getVar("twitch_user"),
		twitchToken = getVar("twitch_token");
	
	initAll();
	
	twitchState *state = twitchState::getInstance();
	
	//open the connection.  wait 5 mins between attempts
	while (!state->conn.openConnection(twitchChannel, twitchUser, twitchToken)) {
		sleep(300);
	}
	
	while (state->conn.getMessage(state->msg)) {
		state->msg.initialized = true;
		
		switch (state->msg.messageType) {
			case twitchMessage::CHATMESSAGE:
				for (const function<void(twitchMessage)> &i : chatMsgListeners) {
					thread(i,state->msg).detach();
				}
				break;
			case twitchMessage::NEWSUB:
			case twitchMessage::RESUB:
			case twitchMessage::SUBGIFT:
				for (const function<void(twitchMessage)> &i : subListeners) {
					thread(i,state->msg).detach();
				}
				break;
			case twitchMessage::WHISPER:
				for (const function<void(twitchMessage)> &i : whisperListeners) {
					thread(i,state->msg).detach();
				}
				break;
			case twitchMessage::RAID:
				for (const function<void(twitchMessage)> &i : raidListeners) {
					thread(i,state->msg).detach();
				}
				break;
			case twitchMessage::RITUAL:
				for (const function<void(twitchMessage)> &i : ritualListeners) {
					thread(i,state->msg).detach();
				}
				break;
			case twitchMessage::UNKNOWN:
				logmsg(LVL3, "Unknown message: %", state->msg.orig);
		}
		
		for (const function<void(twitchMessage)> &i : msgListeners) {
			thread(i,state->msg).detach();
		}
	}
	
	state->conn.closeConnection();
	
	twitchState::destroy();
	
	return 0;
}
