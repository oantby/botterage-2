#ifndef TWITCHHEADERS
#define TWITCHHEADERS
#include <iostream>
#include <map>
using namespace std;

struct twitchUser {
	string userid, displayName;
	uint64_t glimmer = 0;
	bool sub = false, mod = false;
	
	bool operator==(const std::string userInfo);
	bool operator!=(const std::string userInfo);
	twitchUser(std::string name, std::string id);
	twitchUser();
	void clear();
};

struct twitchMessage {
	static const uint8_t CHATMESSAGE = 1,
	                     WHISPER     = 2,
	                     NEWSUB      = 3,
	                     RESUB       = 4,
	                     SUBGIFT     = 5,
	                     RAID        = 6,
	                     RITUAL      = 7,
	                     UNKNOWN     = 8;
	
	bool initialized = false;
	twitchUser user;
	std::string message;
	std::string orig;
	map<string,string> tags;
	int messageType = CHATMESSAGE;
	
	void clear();
};


//the following doesn't actually have anything to do with twitch, but I'm not making a new header just for it
struct lootitem {
	int id;
	string name, flavortext, imglink, soundlink;
	short type;//1=legendary 2=exotic 0=worthless (casLife)
	
};


#endif