#ifndef CHATBOTCOMMANDS_HPP
#define CHATBOTCOMMANDS_HPP

#include <twitchIRCConnection.hpp>
#include <map>
#include <vector>
#include <twitchStructs.hpp>

struct command {
	std::string call, response, user_level;
	time_t lastRun = 0;
	unsigned short cooldown = 10;
	
	command(string c, string r, unsigned cd) : call(c), response(r), cooldown(cd) {}
	command() {}
};

extern map<string,command> cmds;
extern map<string,vector<void (*)(const twitchMessage *, string *, vector<command *> *)> > cmdVars;

vector<command *> commCheck(string msg);
bool loadComms();
void commsReadMsg(twitchMessage msg);
extern "C" void commands_init(void *args);
map<string,string> loadGreetings();
void replaceRandom(const twitchMessage *msg, string *resp, vector<command *> *matches);

#endif