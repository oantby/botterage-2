#include <iostream>
#include <iomanip>
#include <utils.hpp>
#include <random>
#include <DBConnPool.hpp>
#include <mysql++.h>
#include <map>
#include <tuple>
#include <dlfcn.h>
#include <sstream>

using namespace std;

unsigned randomNumber() {
	static mt19937_64 r;
	static bool init = false;
	if (!init) {
		init = true;
		r.seed(time(NULL));
	}
	return r();
}

string htmlEntities(const string &data) {
	string buffer;
	
	//reserve 5 times the string size.  This is to account for the idea that
	//capital letters, numbers, etc will take 5 characters.  lowercase will
	//take 5-6 (generally 6 (everything after c is 6)).
	buffer.reserve(data.size()*5);
	for(size_t pos = 0; pos < data.size(); pos++) {
		buffer.append("&#" + to_string((int)data[pos]) + ";");
	}
	return buffer;
}

string urlEncode(const string &data) {
	// keeps [a-zA-Z0-9_.-]. everything else percent-encoded.
	stringstream ret;
	ret << setfill('0') << hex;
	for (const char &i : data) {
		if (isalnum(i) || i == '_' || i == '.' || i == '-') {
			ret << i;
			continue;
		}
		ret << '%' << uppercase << setw(2) << (int)(unsigned char)i << nouppercase;
	}
	return ret.str();
}

// a cache to hold values from the db for 10 minutes.
map<string, pair<time_t, string>> varCache;
const string getVar(const string &varName, const char *fallback) {
	
	time_t now = time(NULL);
	
	if (varCache.count(varName) && varCache[varName].first + 600 > now) {
		return varCache[varName].second;
	}
	
	try {
		db_wrap db(DBConnPool::getInstance());
		Query q = db.db->query();
		q << "SELECT value FROM variables WHERE name = " << quote << varName;
		StoreQueryResult res = q.store();
		if (!res.num_rows()) {
			return fallback ? fallback : "";
		}
		// positive result. add it, with the current(ish) time, to the cache.
		varCache[varName] = make_pair(now, (const char *)res[0].at(0));
		return varCache[varName].second;
	} catch (BadQuery &e) {
		logmsg(LVL1, "Couldn't get variable [%]: %", varName, e.what());
		return fallback ? fallback : "";
	} catch (char const *e) {
		logmsg(LVL1, "Something went wrong: %", e);
		return fallback ? fallback : "";
	}
}

bool setVar(const string &varName, const string &varVal) {
	// we're just going to wipe it from the cache, and let the next
	// getVar put it back.
	varCache.erase(varName);
	try {
		db_wrap db(DBConnPool::getInstance());
		Query q = db.db->query();
		q << "INSERT INTO variables (name, value) VALUES (" << quote << varName
			<< ", " << quote << varVal << ") ON DUPLICATE KEY UPDATE "
			"value = " << quote << varVal;
		StoreQueryResult res = q.store();
		return true;
	} catch (BadQuery &e) {
		logmsg(LVL1, "Couldn't update variable [%]: %", varName, e.what());
		return false;
	} catch (char const *e) {
		logmsg(LVL1, "Failed to update variable [%]: %", varName, e);
		return false;
	}
}

bool deleteVar(const string &varName) {
	varCache.erase(varName);
	try {
		db_wrap db(DBConnPool::getInstance());
		Query q = db.db->query();
		q << "DELETE FROM variables WHERE name = " << quote << varName;
		q.execute();
		return true;
	} catch (BadQuery &e) {
		logmsg(LVL1, "Couldn't delete variable [%]: %", varName, e.what());
		return false;
	} catch (char const *e) {
		logmsg(LVL1, "Failed to delete variable [%]: %", varName, e);
		return false;
	}
}

vector<string> &get_modules() {
	static vector<string> resp;
	if (resp.size()) return resp;
	
	logmsg(LVL1, "Loading active modules");
	
	try {
		db_wrap db(DBConnPool::getInstance());
		Query q = db.db->query();
		q << "SELECT name FROM modules WHERE enabled ORDER BY priority";
		StoreQueryResult res = q.store();
		if (!res.num_rows()) {
			return resp;
		}
		for (const Row &r : res) {
			logmsg(LVL2, "Found active module: %", (const char *)r.at(0));
			resp.push_back((const char *)r.at(0));
		}
		return resp;
	} catch (BadQuery &e) {
		logmsg(LVL1, "Couldn't get variable: %", e.what());
		throw "Couldn't get active modules";
	} catch (char const *e) {
		logmsg(LVL1, "Something went wrong: %", e);
		throw "Couldn't get active modules";
	}
}

void invoke_hook(string hook, void *args) {
	vector<string> modules = get_modules();
	
	for (const string &mod : modules) {
		void (*f)(void *) = (void (*)(void *))dlsym(RTLD_DEFAULT, (mod + "_" + hook).c_str());
		logmsg(LVL1, "% %_%", f ? "Found" : "Didn't find", mod, hook);
		if (f) {
			f(args);
		}
	}
}