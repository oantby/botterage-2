#include "twitchStructs.hpp"
#include <iostream>

using namespace std;

bool twitchUser::operator==(const std::string userInfo) {
	return ((userInfo == displayName)||(userInfo == userid));
}
bool twitchUser::operator!=(const std::string userInfo) {
	return !(*this == userInfo);
}

twitchUser::twitchUser(std::string name, std::string id) {
	userid = id;
	displayName = name;
}

twitchUser::twitchUser() {}

void twitchUser::clear() {
	userid.clear();
	displayName.clear();
	glimmer = 0;
	sub = false;
	mod = false;
}

void twitchMessage::clear() {
	user.clear();
	message.clear();
	tags.clear();
	messageType = twitchMessage::CHATMESSAGE;
	initialized = false;
}