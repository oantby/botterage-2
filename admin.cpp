// facilitates admins managing the bot through whispers.
/*
@var bot_admins: pipe-delimited list of users who are allowed to administer bot through whispers. Primarily for developers
@var settings_link: The URL to visit the settings page for the bot. Changing this URL does NOT move the page; just makes the bot wrong.
*/

#include <admin.hpp>
#include <utils.hpp>
#include <stringextensions.hpp>
#include <algorithm>
#include <botterage.hpp>
#include <twitchState.hpp>

extern "C" void admin_init(void *args) {
	whisperListeners.push_back(admin::manage);
}

namespace admin {
	void manage(twitchMessage msg) {
		string user = toLower(msg.user.displayName);
		string channel = toLower(getVar("twitch_channel"));
		vector<string> admins = split(toLower(getVar("bot_admins")), '|');
		
		logmsg(LVL3, "Handling whisper from user %", user);
		
		if (user != channel
			&& find(admins.begin(), admins.end(), user) == admins.end()) {
			// we could log this as someone trying to sneak in, but we haven't
			// even verified this is actually a management request, so could
			// just be someone having fun whispering a bot.
			return;
		}
		
		vector<string> words = split(msg.message, ' ');
		
		if (words.size() == 2 && toLower(words[0]) == "loglvl" && isNumeric(words[1])) {
			Logger::setLogLvl(stoi(words[1]));
			setVar("log_lvl", words[1]);
		} else if (string resp = getVar("settings_link");
			resp.size() && words.size() && words[0] == "settings") {
			
			twitchState *state = twitchState::getInstance();
			state->conn.sendMessage("/w " + msg.user.displayName + " " + resp);
		}
	}
};