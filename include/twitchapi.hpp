#ifndef TWITCHAPI_HPP
#define TWITCHAPI_HPP

#include <twitchStructs.hpp>
#include <set>
using namespace std;

namespace twitchapi {
	void uptimeThread();
	const string &getToken();
	void getBroadcasterId();
	void fillTwitchIDs(map<string,twitchUser> &users);
	set<string> getChatters();
	void init();
};

#endif