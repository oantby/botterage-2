/*
@var twitchapi_client_id*: client id for twitch API
@var twitchapi_client_secret*: client secret for twitch API
@var twitchapi_redirect_uri: redirect uri registered with bot for twitch api
@var permanent-broadcaster_id: For development purposes. Override broadcaster ID for twitch api calls
*/
#define MYSQLPP_SSQLS_NO_STATICS
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <future>
#include <vector>
#include <string>
#include <json.hpp>
#include <stringextensions.hpp>
#include <twitchState.hpp>
#include <twitchStructs.hpp>
#include <botterage-db.hpp>
#include <mysql++.h>
#include <DBConnPool.hpp>
#include <curl/curl.h>
#include <commands.hpp>
#include <twitchapi.hpp>
#include <utils.hpp>

using json = nlohmann::json;
using namespace std;
using namespace mysqlpp;

extern "C" void twitchapi_init(void *args) {
	if (getVar("twitch_channel").empty() ||
		getVar("twitchapi_client_id").empty() ||
		getVar("twitchapi_client_secret").empty()) {
		throw "broadcaster_id, twitchapi_client_id, and "
			"twitchapi_client_secret must be set for twitchapi module";
	}
	twitchapi::init();
}

namespace twitchapi {
	
	const char *TOKEN_URL = "https://id.twitch.tv/oauth2/token";
	// string vs char* because something will almost always be appended to this one.
	const string API_BASE = "https://api.twitch.tv/helix/";
	
	size_t twitchCallback(char *data, size_t size, size_t n, void *u);
	void getUptime();
	
	void init() {
		// at startup, we'll always make sure broadcaster_id matches the
		// twitch_channel, UNLESS permanent-broadcaster_id is set, the value
		// of which overrides the lookup.
		if (getVar("permanent-broadcaster_id") == "") {
			getBroadcasterId();
		} else {
			setVar("broadcaster_id", getVar("permanent-broadcaster_id"));
		}
		thread(twitchapi::uptimeThread).detach();
	}
	
	void getBroadcasterId() {
		// looks up the ID for twitch_channel.
		// good news: we've already got a function for that.
		
		logmsg(LVL1, "Looking up broadcaster id");
		
		map<string, twitchUser> users;
		users[getVar("twitch_channel")] = twitchUser();
		fillTwitchIDs(users);
		twitchUser &u = users[getVar("twitch_channel")];
		if (u.userid.empty()) {
			throw runtime_error(string("Failed to find userid for ") + getVar("twitch_channel"));
		}
		
		logmsg(LVL2, "Broadcaster id: %", u.userid);
		
		setVar("broadcaster_id", u.userid);
	}
	
	void fillTwitchIDs(map<string,twitchUser> &users) {
		set<string> needIDs;
		for (pair <const string,twitchUser> &user : users) {
			if (user.second.userid.size()) continue;
			
			needIDs.insert(user.first);
		}
		
		logmsg(LVL1, "Searching for % user(s)", needIDs.size());
		
		const string USERS_URL = API_BASE + "users?";
		
		vector<string> requests;
		requests.push_back(USERS_URL);
		size_t i = 0;
		for (const string &username : needIDs) {
			requests.back() += "login=" + username + "&";
			
			//we can send 100 names at a time to twitch's api.
			if (++i%100 == 0) {
				requests.back().pop_back();
				requests.push_back(USERS_URL);
			}
		}
		if (requests.back() == USERS_URL) {
			//no names on the last request
			requests.pop_back();
		} else {
			requests.back().pop_back();
		}
		
		CURL *curl = curl_easy_init();
		string response;
		
		curl_slist *headers = NULL;
		headers = curl_slist_append(headers, ("Client-ID: " + getVar("twitchapi_client_id")).c_str());
		headers = curl_slist_append(headers, ("Authorization: Bearer " + getToken()).c_str());
		curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
		curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,twitchCallback);
		curl_easy_setopt(curl,CURLOPT_WRITEDATA,(void *)&response);
		
		for (size_t i = 0; i < requests.size(); i++) {
			response.clear();
			logmsg(LVL3, "Requesting [%]", requests[i]);
			curl_easy_setopt(curl,CURLOPT_URL,requests[i].c_str());
			curl_easy_perform(curl);
			
			json j;
			try {
				logmsg(LVL3, "API Response: %", response);
				j = json::parse(response);
			} catch (nlohmann::detail::parse_error &e) {
				logmsg(LVL1, "Couldn't parse usernames data: %", e.what());
				logmsg(LVL1, "Data surrounding error:");
				if (e.byte < 100) {
					logmsg(LVL1, "%", response.substr(0, 200));
				} else {
					logmsg(LVL1, "%", response.substr(e.byte - 100, 200));
				}
				throw "Invalid usernames data";
			}
			
			for (auto &element : j["data"]) {
				for (const string &username : needIDs) {
					if (toLower(element["login"]) == toLower(username)) {
						users[username].userid = element["id"];
						needIDs.erase(username);
						break;
					}
				}
			}
			
			if (needIDs.size()) {
				logmsg(LVL1, "Failed to find % users", needIDs.size());
			}
		}
		
		curl_easy_cleanup(curl);
		curl_slist_free_all(headers);
	}
	
	set<string> getChatters() {
		set<string> users;
		string response;
		CURL *curl = curl_easy_init();
		
		curl_easy_setopt(curl,CURLOPT_URL,("https://tmi.twitch.tv/group/user/" + getVar("twitch_channel") + "/chatters").c_str());
		curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,twitchCallback);
		curl_easy_setopt(curl,CURLOPT_WRITEDATA,(void *)&response);
		
		curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		
		json j;
		try {
			j = json::parse(response);
		} catch (nlohmann::detail::parse_error &e) {
			logmsg(LVL1, "Couldn't parse JSON data: %", e.what());
			logmsg(LVL1, "Surrounding data:");
			
			if (e.byte < 100) {
				logmsg(LVL1, "%", response.substr(0, 200));
			} else {
				logmsg(LVL1, "%", response.substr(e.byte - 100, 200));
			}
			
			throw "Invalid chatter data";
		}
		if (j["chatters"].is_null()) {
			logmsg(LVL1, "Invalid response from tmi.twitch.tv:");
			logmsg(LVL1, "%", response);
			throw "Invalid chatter data";
		}
		response.clear();
		
		for (auto &element : j["chatters"]) {
			if (!element.is_array())
				continue;
			
			for (auto &user : element) {
				users.insert(user.get<string>());
			}
		}
		return users;
	}
	
	size_t twitchCallback(char *data, size_t size, size_t n, void *u) {
		//while it's not explicitly explained, I believe size is the number of
		//bytes per member of n (usually 1), and n is the number of members
		//(usually number of bytes)
		string *str = (string *)u;
		str->reserve(str->size() + (size * n));
		str->append(data,(size * n));
		
		return (size * n);
	}
	
	void getUptime() {
		string token;
		try {
			token = getToken();
		} catch (exception &e) {
			logmsg(LVL1, "Failed to get twitch API token: %", e.what());
			return;
		}
		
		CURL *curl = curl_easy_init();
		curl_easy_setopt(curl,CURLOPT_URL, (API_BASE + "streams?user_id=" + getVar("broadcaster_id")).c_str());
		curl_slist *headers = NULL;
		headers = curl_slist_append(headers,("Client-ID: " + getVar("twitchapi_client_id")).c_str());
		headers = curl_slist_append(headers,("Authorization: Bearer " + token).c_str());
		
		curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
		curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,twitchCallback);
		
		string response;
		curl_easy_setopt(curl,CURLOPT_WRITEDATA,(void *)&response);
		
		curl_easy_perform(curl);
		
		long ret = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);
		
		curl_easy_cleanup(curl);
		curl_slist_free_all(headers);
		
		if (ret / 100 != 2) {
			logmsg(LVL1, "Failed to connect to twitch API. Status: %s", ret);
			logmsg(LVL2, "Response data: %", response);
			throw "Failed to connect to twitch API";
		}
		
		json j;
		try {
			j = json::parse(response);
		} catch (nlohmann::detail::parse_error &e) {
			logmsg(LVL1, "Couldn't parse uptime JSON: %", e.what());
			if (response.size() < 200) {
				logmsg(LVL2, "Data read: %", response);
			} else {
				size_t start;
				if (e.byte < 100) {
					start = 0;
				} else {
					start = e.byte - 100;
				}
				logmsg(LVL2, "Surrounding data: %", response.substr(start, 200));
			}
			return;
		}
		
		//we now have a valid JSON object.  find the start time, convert it to a timestamp.
		twitchState *state = twitchState::getInstance();
		
		if (j["data"].is_null()) {
			logmsg(LVL2, "No stream data found. Not changing status");
			return;
		}
		
		if (j["data"].size()) {
			string liveTime = j["data"][0]["started_at"];
			
			struct tm startTime;
			strptime(liveTime.c_str(),"%Y-%m-%dT%H:%M:%S%z",&startTime);
			
			if (state->liveTime) {
				// was already online.  just set the time to ensure it's right.
				*state->liveTime = timegm(&startTime);
			} else {
				logmsg(LVL1, "Marking stream online, as of %", timegm(&startTime));
				//just went online.  notify any listeners
				{
					lock_guard<mutex> lk(state->liveTimeMute);
					state->liveTime = new uint64_t;
					*state->liveTime = timegm(&startTime);
				}
				state->liveTimeCV.notify_all();
			}
			
		} else {
			// no streams specified. offline.
			if (state->liveTime) {
				logmsg(LVL1, "Marking stream offline");
				{
					lock_guard<mutex> lk(state->liveTimeMute);
					delete state->liveTime;
					state->liveTime = NULL;
				}
				// because stream just went offline, send notification.
				state->liveTimeCV.notify_all();
			}
		}
	}
	
	void uptimeThread() {
		
		do {
			getUptime();
		} while (sleep(300) || true);
		// (loop forever)
	}
	
	const string &getToken() {
		static string token;
		static time_t expires = 0;
		
		time_t now = time(NULL);
		// make sure this token still has at least 10 seconds on it.
		if (now + 10 < expires) {
			// if it does, we can just use it from cache.
			return token;
		}
		
		// otherwise, we need to get a new token using client credentials
		// note: twitch's auth is a little bit... wrong, so it's just client
		// credentials every time to get a new token.
		CURL *curl = curl_easy_init();
		string response;
		
		string postdata = "client_id=" + getVar("twitchapi_client_id") +
			"&client_secret=" + getVar("twitchapi_client_secret") +
			"&grant_type=client_credentials";
		
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata.c_str());
		
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, twitchCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
		curl_easy_setopt(curl, CURLOPT_URL, TOKEN_URL);
		
		curl_easy_perform(curl);
		
		long ret = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &ret);
		double tot_time = 0;
		curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &tot_time);
		
		curl_easy_cleanup(curl);
		
		json j;
		try {
			j = json::parse(response);
		} catch (nlohmann::detail::parse_error &e) {
			logmsg(LVL1, "Couldn't parse token data: %", e.what());
			logmsg(LVL2, "Data surrounding error:");
			if (e.byte < 100) {
				logmsg(LVL2, "%", response.substr(0, 200));
			} else {
				logmsg(LVL2, "%", response.substr(e.byte - 100, 200));
			}
			throw "Invalid token data";
		}
		
		token = j["access_token"];
		// set the expiration time, accounting for time the request itself took.
		// we could get as exact as figuring out total time vs time since
		// first byte sent, but it probably comes down to <1s granularity,
		// and we just want to be safe here.
		expires = time(NULL) + j["expires_in"].get<int>() - ((int)tot_time + 1);
		
		return token;
	}
};