/*
@var session_cookie_name: (Required) What to name the cookie used for administration sessions (recommended: botname_sid)
@var cookie_insecure: If set, session cookie will be allowed to transfer over plaintext-http.
*/
/*
An FCGI that presents pages for updating the variables table,
displaying commands list, etc.
*/

#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <tuple>
#include <vector>
#include <json.hpp>
#include <utils.hpp>
#include <stringextensions.hpp>
#include <DBConnPool.hpp>
#include <botterage-db.hpp>
#include <stdexcept>
#include <mysql++.h>
#include "fcgio.h"
#include <openssl/sha.h>
#include <string.h>
#include <curl/curl.h>

// a string that, by virtue of length and randomness, ought to never
// appear as a legitimate value for any variable.
#define UNSET_VAR_VALUE "MQP9VOSUF1PhPpxzPE3hVk2lma4fPjDLD44ksN8N"

using namespace std;
using json = nlohmann::json;

struct Var {
	string name;
	string description;
	string default_val;
	bool secret;
	
	// initializers
	
	Var() {}
	Var(string n) : name(n) {}
	Var(string n, string desc, string def, bool s) : name(n), description(desc),
		default_val(def), secret(s) {}
	
	// allows inclusion in a set
	bool operator<(const Var &b) const {
		return name < b.name;
	}
};

// a VERY simple little class to make it easier to add headers after
// adding body content. by default simply buffers the input, then
// shoots it out as a string on request.
struct ResponseWriter {
	string status = "200 OK";
	multimap<string, string> headers;
	map<string, string> info; // random crap functions might want to pass to each other.
	stringstream internal;
	ostream *external = NULL;
	
	ResponseWriter(ostream *stream) : external(stream) {}
	ResponseWriter() {}
	
	void addHeader(const string &k, const string &v) {
		headers.insert({k, v});
	}
	
	void setHeader(const string &k, const string &v) {
		headers.erase(k);
		headers.insert({k, v});
	}
	
	void setStatus(const string &s) {
		status = s;
	}
	
	string toString() const {
		if (external) {
			throw runtime_error("Tried to convert external ostream to string");
		}
		
		stringstream ret;
		logmsg(LVL3, "Writing:");
		ret << "Status: " << status << "\r\n";
		logmsg(LVL3, "Status: %", status);
		for (auto &i : headers) {
			logmsg(LVL3, "%: %", i.first, i.second);
			ret << i.first << ": " << i.second << "\r\n";
		}
		ret << "\r\n";
		ret << internal.str();
		return ret.str();
	}
	
	template <typename T>
	ostream &operator<<(T val) {
		if (external) {
			return *external << val;
		} else {
			return internal << val;
		}
	}
};

set<Var> vars;

template <int x> void print_code(FCGX_Request &request, ResponseWriter &writer) {
	const pair<const string, const string> response = map<int, const pair<const string, const string> > {
		{401, {"Unauthorized", "You must provide authentication to access this page"}},
		{403, {"Forbidden", "You do not have access to this page"}},
		{404, {"Not Found", "I'm sorry, but that page could not be found. If you believe you "
		"have received this message in error, please contact an administrator."}},
		{500, {"Internal Server Error", "An error occurred and the server was unable to process your request"}},
		{501, {"Not Implemented", "I'm sorry, this feature has not yet been implemented"}}
	}[x];
	
	fcgi_streambuf rbuf_out(request.out);
	ostream rout(&rbuf_out);
	
	rout << "Status: " << x << ' ' << response.first << "\r\n";
	rout << "Content-Type: text/plain\r\n\r\n";
	rout << response.second << endl;
}

void var_request(FCGX_Request &request, ResponseWriter &writer) {
	json jvars;
	for (const Var &v : vars) {
		jvars.push_back({
			{"name", v.name},
			{"description", v.description},
			{"default", v.default_val},
			{"secret", v.secret},
			{"value", getVar(v.name, UNSET_VAR_VALUE)}
		});
	}
	fcgi_streambuf rbuf_in(request.in);
	fcgi_streambuf rbuf_out(request.out);
	fcgi_streambuf rbuf_err(request.err);
	
	ostream rout(&rbuf_out);
	rout << "Content-Type: application/json\r\n\r\n";
	
	rout << jvars.dump(4) << endl;
}

void set_vars(FCGX_Request &request, ResponseWriter &writer) {
	fcgi_streambuf rbuf_in(request.in);
	fcgi_streambuf rbuf_out(request.out);
	fcgi_streambuf rbuf_err(request.err);
	
	ostream rout(&rbuf_out);
	istream rin(&rbuf_in);
	
	string data(istreambuf_iterator<char>(rin), {});
	
	json j;
	try {
		j = json::parse(data);
	} catch (...) {
		logmsg(LVL2, "Failed to parse request data");
		rout << "Status: 400 Bad Usage\r\nContent-Type: application/json\r\n\r\n";
		rout << R"({"status": 400, "message": "Data in request was invalid"})" "\n";
		return;
	}
	
	for (auto &[k, v] : j.items()) {
		if (!vars.count(Var(k))) {
			logmsg(LVL2, "Invalid requested variable [%]", k);
			rout << "Status: 400 Bad Usage\r\nContent-Type: application/json\r\n\r\n";
			rout << R"({"status": 400, "message": "Invalid variable requested"})" "\n";
			return;
		}
	}
	// now we go again, knowing that they're all valid.
	for (auto &[k, v] : j.items()) {
		if (v == UNSET_VAR_VALUE) {
			deleteVar(k);
		} else {
			setVar(k, v);
		}
	}
	
	rout << "Status: 200 OK\r\nContent-Type: application/json\r\n";
	for (auto &i : writer.headers) {
		rout << i.first << ": " << i.second << "\r\n";
	}
	rout << "\r\n" R"({"status": 200, "message": "Updated )" << j.size() << R"( variables"})" "\n";
}

void app_settings(FCGX_Request &request, ResponseWriter &writer) {
	static json j {
		{"unset_var_value", UNSET_VAR_VALUE}
	};
	
	fcgi_streambuf rbuf_out(request.out);
	ostream rout(&rbuf_out);
	
	rout << "Content-Type: application/json\r\n\r\n";
	rout << j.dump(4) << endl;
}

void get_commands(FCGX_Request &request, ResponseWriter &writer) {
	vector<commands> db_comms;
	try {
		db_wrap db(DBConnPool::getInstance());
		Query q = db.db->query("SELECT comm, resp, cooldown, user_level FROM commands");
		q.storein(db_comms);
	} catch (exception &e) {
		logmsg(LVL1, "Couldn't get commands from database: %", e.what());
		print_code<500>(request, writer);
		return;
	}
	json j;
	for (commands c : db_comms) {
		j[c.comm] = {
			{"command", c.comm},
			{"response", c.resp},
			{"cooldown", c.cooldown},
			{"user_level", c.user_level}
		};
	}
	
	fcgi_streambuf rbuf_out(request.out);
	ostream rout(&rbuf_out);
	
	rout << "Content-Type: application/json\r\n\r\n";
	rout << j.dump(4) << endl;
}

void commands_page(FCGX_Request &request, ResponseWriter &writer) {
	print_code<501>(request, writer);
}

// ready for me to bastardize b64? I am!
namespace b64 {
	// the base64 alphabet, which I don't know the correct order of and didn't look up.
	// also the last two characters vary by medium. I just chose a couple I like.
	// update: it seems I accidentally did a pretty solid job here.
	const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890_/";
	
	// encode a string to base64.
	string encode(const string &val) {
		string ret;
		for (size_t i = 0; i < val.size(); i += 3) {
			uint8_t ind;
			
			ind = (val[i] >> 2) & 0x3f;
			
			ret += alphabet[ind];
			
			if (i + 1 == val.size()) {
				
				ind = (val[i] << 4) & 0x3f;
				
				ret += alphabet[ind];
				ret += "==";
				break;
			}
			
			ind = (val[i] << 4) & 0x30;
			ind += (val[i+1] >> 4) & 0x0f;
			
			ret += alphabet[ind];
			
			if (i + 2 == val.size()) {
				ind = (val[i + 1] << 2) & 0x3c;
				
				ret += alphabet[ind];
				ret += "=";
				break;
			}
			
			ind = (val[i + 1] << 2) & 0x3c;
			ind += (val[i + 2] >> 6) & 0x03;
			
			ret += alphabet[ind];
			
			ind = (val[i + 2]) & 0x3f;
			
			ret += alphabet[ind];
		}
		return ret;
	}
	
	// decode a base64 string.
	string decode(const string &val) {
		string ret;
		
		for (size_t pos = 0; pos < val.size(); pos += 4) {
			const char *c = strchr(alphabet, val[pos]);
			if (!c) {
				// that's no bueno.
				throw runtime_error("Invalid b64 character");
			}
			
			char i;
			i = (c - alphabet) << 2;
			
			if (pos + 1 == val.size() || !(c = strchr(alphabet, val[pos + 1]))) {
				// there should always be at least 2. this one is another error.
				throw runtime_error("Invalid b64 string");
			}
			
			i += (c - alphabet) >> 4;
			
			ret += i;
			
			i = (c - alphabet) << 4;
			
			if (pos + 2 == val.size() || val[pos + 2] == '=') {
				// we'll accept a string that's missing padding.
				// if this is a padding byte, we'll assume the rest is padding.
				ret += i;
				break;
			} else if (!(c = strchr(alphabet, val[pos + 2]))) {
				throw runtime_error("Invalid b64 string");
			}
			
			i += (c - alphabet) >> 2;
			
			ret += i;
			
			i = (c - alphabet) << 6;
			
			if (pos + 3 == val.size() || val[pos + 3] == '=') {
				ret += i;
				break;
			} else if (!(c = strchr(alphabet, val[pos + 3]))) {
				throw runtime_error("Invalid b64 string");
			}
			
			i += (c - alphabet);
			ret += i;
		}
		
		return ret;
	}
}

// implements an HMAC SHA-512 of data with skey.
string get_signature(const string &data, string skey) {
	string skey_r(skey.rbegin(), skey.rend());
	
	SHA512_CTX ctx;
	char buffer[65];
	
	SHA512_Init(&ctx);
	SHA512_Update(&ctx, skey_r.c_str(), skey_r.size());
	SHA512_Update(&ctx, data.c_str(), data.size());
	SHA512_Final((unsigned char *)buffer, &ctx);
	
	SHA512_Init(&ctx);
	SHA512_Update(&ctx, skey.c_str(), skey.size());
	SHA512_Update(&ctx, buffer, 64);
	SHA512_Final((unsigned char *)buffer, &ctx);
	
	stringstream ret;
	ret.write(buffer, 64);
	return ret.str();
}

string create_signature(const string &data) {
	return get_signature(data, getVar("cookie_signing_key"));
}

bool verify_signature(const string &sig, const string &data) {
	// backup_signing_key allows for e.g. key rotation
	string skey = getVar("cookie_signing_key");
	string bkey = getVar("backup_signing_key");
	
	return (sig == get_signature(data, skey) || sig == get_signature(data, bkey));
}

enum AuthResp {
	UNAUTHORIZED,
	FORBIDDEN,
	OK
};

AuthResp validate_token(string token, ResponseWriter &writer) {
	// first 128 characters should be a hash.
	string decoded;
	try {
		decoded = b64::decode(token);
	} catch (runtime_error &e) {
		cerr << "Failed to decode auth token: " << e.what() << endl;
		return UNAUTHORIZED;
	}
	
	// first 64 bytes are a keyed hash of the rest of the data (theoretically).
	string hash = decoded.substr(0, 64);
	
	string data = decoded.substr(64);
	
	if (!verify_signature(hash, data)) {
		logmsg(LVL1, "Signature didn't match data");
		return UNAUTHORIZED;
	}
	
	logmsg(LVL3, "Data (decoded): %", data);
	
	vector<string> allowed = split(getVar("bot_admins"), '|');
	allowed.push_back(getVar("twitch_channel"));
	
	try {
		json j = json::parse(data);
		if (time(NULL) > j["expires"]) {
			// expired.
			return UNAUTHORIZED;
		}
		string user = j["user"];
		for (const string &s : allowed) {
			if (toLower(s) == toLower(user)) {
				writer.info["user"] = user;
				return OK;
			}
		}
	} catch (...) {
		logmsg(LVL1, "Failed to parse cookie data");
	}
	
	return FORBIDDEN;
}

string create_token(const string &user) {
	json j{
		{"expires", time(NULL) + (86400 * 365)},
		{"user", user}
	};
	string data = j.dump();
	string cookie = create_signature(data);
	cookie += data;
	cookie = b64::encode(cookie);
	return cookie;
}

// checks if the current request is authorized. if so, adds a new cookie
// extending the user's session lifetime.
AuthResp check_auth(FCGX_Request &request, ResponseWriter &writer) {
	// if this bot is not yet initialized, the first person is allowed in.
	// as soon as we know a channel and how to authenticate, authentication kicks in.
	if (!getVar("twitch_channel").size() || !getVar("session_cookie_name").size()
		|| !getVar("twitchapi_client_id").size() || !getVar("twitchapi_client_secret").size()
		|| !getVar("twitchapi_redirect_uri").size()) {
		return OK;
	}
	// see about getting a current token.
	string auth;
	char *t = FCGX_GetParam("HTTP_COOKIE", request.envp);
	if (t) auth = t;
	if (!auth.size()) {
		logmsg(LVL1, "No cookie present");
		return UNAUTHORIZED;
	}
	
	fcgi_streambuf rbuf_out(request.out);
	ostream rout(&rbuf_out);
	
	bool collecting_name = true;
	string name, val;
	for (size_t pos = 0; pos < auth.size(); pos++) {
		if (auth[pos] == ';') {
			// done collecting this one.
			logmsg(LVL3, "Processing name % val [%]", name, val);
			
			if (name == getVar("session_cookie_name")) {
				if (AuthResp ret = validate_token(val, writer); ret == OK) {
					writer.addHeader("Set-Cookie", getVar("session_cookie_name") + "=" +
						create_token(writer.info["user"]) + (getVar("cookie_insecure").size() ? "" : "; Secure") + "; HttpOnly");
					return OK;
				} else if (ret == FORBIDDEN) {
					// this was the right token, and had our signature,
					// but the user wasn't allowed.
					return FORBIDDEN;
				}
			}
			
			val.clear();
			name.clear();
			collecting_name = true;
			continue;
		}
		if (collecting_name) {
			if (auth[pos] == '=') {
				collecting_name = false;
				continue;
			}
			if (auth[pos] != ' ') {
				name += auth[pos];
			}
		} else {
			val += auth[pos];
		}
	}
	if (name == getVar("session_cookie_name")) {
		if (AuthResp ret = validate_token(val, writer); ret == OK) {
			writer.addHeader("Set-Cookie", getVar("session_cookie_name") + "=" +
				create_token(writer.info["user"]) + (getVar("cookie_insecure").size() ? "" : "; Secure") + "; HttpOnly");
			return OK;
		} else if (ret == FORBIDDEN) {
			// this was the right token, and had our signature,
			// but the user wasn't allowed.
			return FORBIDDEN;
		}
	}
	
	return UNAUTHORIZED;
}

// redirects the user to the twitch auth endpoint.
void auth_redirect(FCGX_Request &req, ResponseWriter &writer) {
	
	fcgi_streambuf rbuf_out(req.out);
	ostream rout(&rbuf_out);
/*
https://id.twitch.tv/oauth2/authorize?client_id={{twitchapi_client_id}}
&force_verify=true&redirect_uri={{twitchapi_redirect_uri}}&response_type=code&scope=
*/
	rout << "Status: 302 Found\r\n";
	rout << "Location: https://id.twitch.tv/oauth2/authorize?client_id="
		<< getVar("twitchapi_client_id") << "&force_verify=true&"
		"redirect_uri=" << urlEncode(getVar("twitchapi_redirect_uri"))
		<< "&response_type=code&scope=\r\n";
	rout << "Content-Type: text/plain\r\n\r\n";
	rout << "You must login through twitch to use the requested page. To do so, "
		"please visit https://id.twitch.tv/oauth2/authorize?client_id="
		<< getVar("twitchapi_client_id") << "&force_verify=true&"
		"redirect_uri=" << urlEncode(getVar("twitchapi_redirect_uri"))
		<< "&response_type=code&scope=\n";
}

size_t twitchCallback(char *data, size_t size, size_t n, void *u) {
	string *str = (string *)u;
	str->reserve(str->size() + (size * n));
	str->append(data,(size * n));
	
	return (size * n);
}

// takes an auth code from twitch, reaches out to verify, gets the username,
// and provides a 1-year token from us.
// note: I considered just including twitchapi module and putting most of this
// in there, but it didn't have functionality for that, is unlikely to
// benefit from it soon, and is a lot of unorganized bloat to bring in here
// (e.g. expects one or more external variables to exist).
void authorize(FCGX_Request &req, ResponseWriter &writer) {
	const char TOKEN_URL[] = "https://id.twitch.tv/oauth2/token";
	const char USERS_URL[] = "https://api.twitch.tv/helix/users";
	// first off, we need to get information from the url.
	// need to pull the "code" parameter.
	fcgi_streambuf rbuf_out(req.out);
	ostream rout(&rbuf_out);
	string query = FCGX_GetParam("QUERY_STRING", req.envp);
	// now we search the query string for a code.
	size_t pos;
	if ((pos = query.find("code=")) == string::npos) {
		
		writer.setStatus("401 Unauthorized");
		writer << "No authorization was provided.\n";
		rout << writer.toString();
		return;
	}
	
	pos += strlen("code=");
	string code;
	while (pos < query.size() && query[pos] != '&') {
		code += query[pos++];
	}
	
	// time to set up our request to turn this code into a token.
	CURL *curl = curl_easy_init();
	string response;
	
	string postdata = "client_id=" + getVar("twitchapi_client_id") +
		"&client_secret=" + getVar("twitchapi_client_secret") +
		"&code=" + code +
		"&grant_type=authorization_code&redirect_uri=" +
		urlEncode(getVar("twitchapi_redirect_uri"));
	
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata.c_str());
	
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, twitchCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&response);
	curl_easy_setopt(curl, CURLOPT_URL, TOKEN_URL);
	
	curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	
	string accessToken;
	
	json j;
	try {
		j = json::parse(response);
		accessToken = j["access_token"];
	} catch (...) {
		logmsg(LVL1, "Failed to parse users response json");
		logmsg(LVL2, "Data: %", response);
		
		writer.setStatus("403 Forbidden");
		writer << "No valid authorization was provided.\n";
		rout << writer.toString();
		return;
	}
	
	// now we have to take that token and request the user's information.
	response.clear();
	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, USERS_URL);
	curl_slist *headers = NULL;
	headers = curl_slist_append(headers, ("Client-ID: " + getVar("twitchapi_client_id")).c_str());
	headers = curl_slist_append(headers, ("Authorization: Bearer " + accessToken).c_str());
	curl_easy_setopt(curl,CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION, twitchCallback);
	curl_easy_setopt(curl,CURLOPT_WRITEDATA, (void *)&response);
	
	curl_easy_perform(curl);
	curl_easy_cleanup(curl);
	curl_slist_free_all(headers);
	
	try {
		j = json::parse(response);
		writer.info["user"] = j["data"][0]["login"];
	} catch (...) {
		logmsg(LVL1, "Failed to parse users response json");
		logmsg(LVL2, "Data: %", response);
		
		writer.setStatus("403 Forbidden");
		writer << "Could not obtain valid authorization. Please try again";
		rout << writer.toString();
		return;
	}
	
	writer.setStatus("302 Found");
	writer.setHeader("Location", "/settings");
	writer.setHeader("Content-Type", "text/html");
	writer.addHeader("Set-Cookie", getVar("session_cookie_name") + "=" +
		create_token(writer.info["user"]) + (getVar("cookie_insecure").size() ? "" : "; Secure") + "; HttpOnly");
	writer << "Login successful. Please visit <a href=\"/settings\">the settings page</a> to continue";
	rout << writer.toString();
}

function<void(FCGX_Request&, ResponseWriter&)> print_template(const string &tname) {
	
	return [=](FCGX_Request &request, ResponseWriter &writer) {
		ifstream ifile(string("templates/") + tname);
		if (!ifile) {
			logmsg(LVL1, "Failed to open template [%]: %", tname, strerror(errno));
			print_code<404>(request, writer);
			return;
		}
		
		fcgi_streambuf rbuf_out(request.out);
		ostream rout(&rbuf_out);
		
		char buf[1024];
		char *p;
		while (ifile.read(buf, sizeof(buf))) {
			if ((p = strstr(buf, "{headers}"))) {
				rout.write(buf, p - buf);
				for (auto &i : writer.headers) {
					rout << i.first << ": " << i.second << "\r\n";
				}
				rout.write(p + strlen("{headers}"),
					ifile.gcount() - (p + strlen("{headers}") - buf));
				continue;
			}
			rout.write(buf, ifile.gcount());
		}
		rout.write(buf, ifile.gcount());
		ifile.close();
		ifile.clear();
	};
}

int main(void) {
	// Backup the stdio streambufs
	streambuf * cin_streambuf  = cin.rdbuf();
	streambuf * cout_streambuf = cout.rdbuf();
	streambuf * cerr_streambuf = cerr.rdbuf();
	
	// collect the necessary environment variables.
	const char *DBNAME = getenv("DBNAME");
	const char *DBUSER = getenv("DBUSER");
	const char *DBPASS = getenv("DBPASS");
	const char *DBSERV = getenv("DBSERV");
	
	
	DBConnPool::initialize(DBNAME,DBUSER,DBPASS,DBSERV);
	
	if (string logInfo = getVar("log_lvl"); logInfo != "") {
		Logger::setLogLvl(stoi(logInfo));
	}
	if (getenv("DEBUG")) Logger::setLogLvl(~0);

	FCGX_Request request;

	FCGX_Init();
	FCGX_InitRequest(&request, 0, 0);
	
	ifstream ifile("vars.tsv");
	if (!ifile) {
		cerr << "FATAL: Can't open vars.tsv" << endl;
		return 1;
	}
	
	string line;
	getline(ifile, line); // throw away the header.
	while (getline(ifile, line)) {
		Var v;
		vector<string> parts = split(line, '\t');
		if (parts.size() != 4) {
			logmsg(LVL1, "Line had incorrect number of parts [%] [%]", parts.size(), line);
			continue;
		}
		v.name = parts[0];
		v.default_val = parts[1];
		v.description = parts[3];
		v.secret = (parts[2] == "*");
		vars.insert(v);
	}
	
	ifile.close();
	ifile.clear();
	
	logmsg(LVL1, "Loaded % variables", vars.size());
	
	// name, methods, callback, requires auth, has interface (not ajax)
	multimap<string, tuple<set<string>, function<void(FCGX_Request&, ResponseWriter&)>, bool, bool> > routes {
		{"/vars", {{"GET"}, var_request, true, false}},
		{"/vars", {{"POST"}, set_vars, true, false}},
		{"/settings", {{"GET"}, print_template("settings.html"), true, true}},
		{"/app_settings", {{"GET"}, app_settings, true, false}},
		{"/commands", {{"GET"}, get_commands, false, false}},
		{"/commands_list", {{"GET"}, print_template("commands_list.html"), false, true}},
		{"/oauth", {{"GET"}, authorize, false, true}}
	};

	while (FCGX_Accept_r(&request) == 0) {
		ResponseWriter writer;
		
		char *p;
		string uri = (p = FCGX_GetParam("REQUEST_URI", request.envp)) ? p : "";
		string method = (p = FCGX_GetParam("REQUEST_METHOD", request.envp)) ? p : "";
		string query = (p = FCGX_GetParam("QUERY_STRING", request.envp)) ? p : "";
		string sname = (p = FCGX_GetParam("SCRIPT_NAME", request.envp)) ? p : "";
		logmsg(LVL2, "Handling [%] request for [%]", method, uri);
		logmsg(LVL2, "Query string: [%]", query);
		logmsg(LVL2, "Script name: [%]", sname);
		
		bool found = false;
		for (auto [cur, end] = routes.equal_range(sname); cur != end; cur++) {
			if (!get<0>(cur->second).count(method)) continue;
			
			found = true;
			AuthResp ret;
			
			if (get<2>(cur->second) && (ret = check_auth(request, writer)) != OK) {
				if (ret == FORBIDDEN) {
					print_code<403>(request, writer);
				} else if (ret == UNAUTHORIZED) {
					// if this is a page with an interface, we send them
					// to get authorized. otherwise, we just print the 401.
					if (get<3>(cur->second)) {
						auth_redirect(request, writer);
					} else {
						print_code<401>(request, writer);
					}
				}
			} else {
				get<1>(cur->second)(request, writer);
			}
			break;
		}
		if (!found) {
			print_code<404>(request, writer);
		}
	}

	// restore stdio streambufs
	cin.rdbuf(cin_streambuf);
	cout.rdbuf(cout_streambuf);
	cerr.rdbuf(cerr_streambuf);

	return 0;
}

