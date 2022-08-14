#include <regex>
#include <string>
#include <stringextensions.hpp>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <vector>
#include <iostream>
#include <twitchStructs.hpp>
#include <twitchIRCConnection.hpp>
#include <botterage-db.hpp>
#include <mysql++.h>
#include <DBConnPool.hpp>
#include <twitchState.hpp>
#include <commands.hpp>
#include <botterage.hpp>
#include <utils.hpp>
#include <limits>
using namespace std;
using namespace mysqlpp;

map<string,command> cmds;
map<string,vector<void (*)(const twitchMessage *, string *, vector<command *> *)> > cmdVars;

//these functions aren't in the header file because I don't want them available to
//other files for use; they are specific to commands.
void registerVars();
void replaceUser(const twitchMessage *msg, string *resp, vector<command *> *matches);
void replaceGreeting(const twitchMessage *msg, string *resp, vector<command *> *matches);
void addCom(const twitchMessage *msg, string *resp, vector<command *> *matches);
void editCom(const twitchMessage *msg, string *resp, vector<command *> *matches);
void delCom(const twitchMessage *msg, string *resp, vector<command *> *matches);
void toUser(const twitchMessage *msg, string *resp, vector<command *> *matches);
void declareVar(const twitchMessage *msg, string *resp, vector<command *> *matches);
void delVar(const twitchMessage *msg, string *resp, vector<command *> *matches);
bool loadUserVars();
void replaceUserVars(string &s);

void commsReadMsg(twitchMessage msg) {
	twitchState *state = twitchState::getInstance();
	
	vector<command *> matches = commCheck(msg.message);
	
	time_t now = time(NULL);
	for (command *i : matches) {
		// permission checks for this command.
		if (i->user_level == "owner" && msg.user != getVar("twitch_channel")) continue;
		if (i->user_level == "mod" && !msg.user.mod && msg.user != getVar("twitch_channel")) continue;
		if (i->user_level == "sub" && !msg.user.sub && !msg.user.mod && msg.user != getVar("twitch_channel")) continue;
		
		// make sure this command isn't on cooldown
		if (now < i->lastRun + i->cooldown) continue;
		
		string resp = i->response;
		i->lastRun = time(NULL);
		// update the db for record-keeping.
		try {
			db_wrap db(DBConnPool::getInstance());
			Query q = db.db->query();
			q << "UPDATE commands SET last_used = CURRENT_TIMESTAMP() WHERE comm = " << quote << i->call;
			SimpleResult res = q.execute();
			
			if (!res.rows()) {
				logmsg(LVL1, "Failed to update last used for command [%]", i->call);
			}
		} catch (exception &e) {
			logmsg(LVL1, "Couldn't update last_used for command [%]: %", i->call, e.what());
		}
		
		// user vars - simple string replacements.
		replaceUserVars(resp);
		
		//let any vars do their thing.
		for (pair<const string,vector<void (*)(const twitchMessage *, string *, vector<command *> *)> > j : cmdVars) {
			if (resp.find(j.first) != string::npos) {
				for (size_t k = 0; k < j.second.size(); k++) {
					j.second[k](&msg, &resp, &matches);
				}
			}
		}
		
		
		if (resp.size()) {
			//if a callback emptied it, we won't send anything.
			if (resp[0] == '!' || resp[0] == '/') {
				state->conn.sendMessage(resp);
			} else {
				state->conn.sendMessage("/me - " + resp);
			}
		}
	}
}

extern "C" void commands_init(void *args) {
	logmsg(LVL2, "Initializing commands module");
	if (!loadComms()) {
		throw "Couldn't load comms";
	}
	registerVars();
	loadUserVars();
	chatMsgListeners.push_back(commsReadMsg);
}

bool loadComms() {
	vector<commands> db_comms;
	try {
		db_wrap db(DBConnPool::getInstance());
		Query q = db.db->query("SELECT comm,resp,cooldown FROM commands");
		q.storein(db_comms);
	} catch (exception &e) {
		logmsg(LVL1, "Couldn't get commands from database: %", e.what());
		return false;
	}
	cmds.clear();
	for (commands c : db_comms) {
		cmds[c.comm] = command(c.comm,c.resp,c.cooldown);
	}
	
	logmsg(LVL2, "Loaded % commands", cmds.size());
	return true;
}

void registerVars() {
	// fill up the cmdVars variable.  This is static at compile time; it's just an easy
	// way to allow me to add/remove new callbacks for variables.
	
	cmdVars["$(user)"].push_back(replaceUser);
	cmdVars["$(toUser)"].push_back(toUser);
	cmdVars["$(greeting)"].push_back(replaceGreeting);
	cmdVars["$(addCom)"].push_back(addCom);
	cmdVars["$(editCom)"].push_back(editCom);
	cmdVars["$(delCom)"].push_back(delCom);
	cmdVars["$(random)"].push_back(replaceRandom);
	cmdVars["$(declare)"].push_back(declareVar);
	cmdVars["$(delVar)"].push_back(delVar);
}

#define USERVAR_INT 0
#define USERVAR_STRING 1
map<string, tuple<uint8_t, int64_t, string> > userVars;

// writes the value for userVars[key] into the db.
void userVarDbUpdate(const string &key) {
	user_vars val;
	val.name = key;
	
	if (!userVars.count(string("$(") + key + string(")"))) {
		// we're deleting this member.
		logmsg(LVL2, "Deleting userVar [%]", val.name);
		try {
			db_wrap db(DBConnPool::getInstance());
			Query q = db.db->query();
			// shockingly, mysqlpp doesn't have a Query::delete. yes, it's a
			// keyword, but something comparable would be expected.
			q << "DELETE FROM user_vars WHERE name = " << quote << key;
			SimpleResult res = q.execute();
			
			if (res.rows()) {
				logmsg(LVL1, "userVar [%] deleted", val.name);
			} else {
				logmsg(LVL1, "replace(userVar[%]) updated 0 rows", val.name);
			}
		} catch (BadQuery &e) {
			logmsg(LVL1, "Couldn't delete userVar [%]: %", val.name, e.what());
		} catch (exception &e) {
			logmsg(LVL1, "Couldn't delete userVar [%]: %", val.name, e.what());
		}
		return;
	}
	
	const tuple<uint8_t, int64_t, string> &entry = userVars[string("$(") + key + string(")")];
	
	if (get<0>(entry) == USERVAR_STRING) {
		val.value = get<2>(entry);
	} else {
		val.value = to_string(get<1>(entry));
	}
	
	logmsg(LVL2, "Updating value for [%] to [%]", val.name, val.value);
	try {
		db_wrap db(DBConnPool::getInstance());
		Query q = db.db->query();
		q.replace(val);
		SimpleResult res = q.execute();
		
		if (res.rows()) {
			logmsg(LVL1, "userVar [%] updated", val.name);
		} else {
			logmsg(LVL1, "replace(userVar[%]) updated 0 rows", val.name);
		}
	} catch (BadQuery &e) {
		logmsg(LVL1, "Couldn't update userVar [%]: %", val.name, e.what());
	} catch (exception &e) {
		logmsg(LVL1, "Couldn't update userVar [%]: %", val.name, e.what());
	}
}

void declareVar(const twitchMessage *msg, string *resp, vector<command *> *matches) {
	regex r = regex("^[^ ]+ ([^ ]+) (.*)$", regex_constants::ECMAScript|regex_constants::icase);
	smatch m;
	if (!regex_match(msg->message, m, r)) {
		logmsg(LVL1, "Declare string [%] didn't match search regex", msg->message);
		*resp = "It seems like you're trying to declare a variable, but I don't understand you.";
		return;
	}
	
	if (isNumeric(m[2])) {
		int64_t val;
		try {
			val = stol(m[2]);
		} catch (out_of_range &e) {
			*resp = "Sorry, that number's a bit too big for me";
			return;
		}
		userVars[string("$(") + m[1].str() + string(")")] = make_tuple(USERVAR_INT, val, "");
	} else {
		userVars[string("$(") + m[1].str() + string(")")] = make_tuple(USERVAR_STRING, 0, m[2]);
	}
	userVarDbUpdate(m[1].str());
	
	*resp = "I've set $(" + m[1].str() + ") to " + m[2].str();
}

void delVar(const twitchMessage *msg, string *resp, vector<command *> *matches) {
	regex r = regex("^[^ ]+ ([^ ]+)", regex_constants::ECMAScript|regex_constants::icase);
	smatch m;
	if (!regex_match(msg->message, m, r)) {
		logmsg(LVL1, "delVar string [%] didn't match search regex", msg->message);
		*resp = "It seems like you're trying to delete a variable, but I don't understand you.";
		return;
	}
	
	userVars.erase(string("$(") + m[1].str() + string(")"));
	userVarDbUpdate(m[1].str());
	
	*resp = "I've deleted $(" + m[1].str() + ")";
}

bool loadUserVars() {
	vector<user_vars> db_vars;
	try {
		db_wrap db(DBConnPool::getInstance());
		Query q = db.db->query("SELECT name,value FROM user_vars");
		q.storein(db_vars);
	} catch (exception &e) {
		logmsg(LVL1, "Couldn't get user vars from database: %", e.what());
		return false;
	}
	logmsg(LVL2, "Found % user_vars in the database", db_vars.size());
	userVars.clear();
	for (user_vars v : db_vars) {
		if (isNumeric(v.value)) {
			try {
				int64_t val = stol(v.value);
				userVars[string("$(") + v.name + string(")")] = make_tuple(USERVAR_INT, val, "");
			} catch (out_of_range &e) {
				logmsg(LVL1, "Ignoring user_vars entry [%] with out-of-range value", v.name);
			}
		} else {
			userVars[string("$(") + v.name + string(")")] = make_tuple(USERVAR_STRING, 0, v.value);
		}
	}
	return true;
}

void replaceUserVars(string &s) {
	size_t spot = string::npos;
	for (pair<const string, tuple<uint8_t, int64_t, string> > &var : userVars) {
		if ((spot = s.find(var.first)) != string::npos) {
			if (get<0>(var.second) == USERVAR_STRING) {
				s.replace(spot, var.first.size(), get<2>(var.second));
			} else {
				// integers we give the opportunity for a ++ operator.
				// we'll only do prefix ++, because I want to return the
				// updated value and not to explain to people the difference
				// between ++$(i) and $(i)++.
				if (spot > 1 && s.substr(spot - 2, 2) == "++") {
					if (get<1>(var.second) == numeric_limits<int64_t>::max()) {
						twitchState *state = twitchState::getInstance();
						state->conn.sendMessage("Congrats on the overflow");
					}
					s.replace(spot - 2, var.first.size() + 2,
						to_string(++get<1>(var.second)));
					// update the value in the db.
					userVarDbUpdate(var.first.substr(2, var.first.size() - 3));
				} else if (spot > 1 && s.substr(spot - 2, 2) == "--") {
					if (get<1>(var.second) == numeric_limits<int64_t>::min()) {
						twitchState *state = twitchState::getInstance();
						state->conn.sendMessage("Congrats on the overflow");
					}
					s.replace(spot - 2, var.first.size() + 2,
						to_string(--get<1>(var.second)));
					// update the value in the db.
					userVarDbUpdate(var.first.substr(2, var.first.size() - 3));
				} else {
					s.replace(spot, var.first.size(), to_string(get<1>(var.second)));
				}
			}
		}
	}
}

void replaceRandom(const twitchMessage *msg, string *resp, vector<command *> *matches) {
	
	regex r = regex("\\(\\$\\(random\\) (-?[0-9]+) (-?[0-9]+)\\)",regex_constants::ECMAScript|regex_constants::icase);
	smatch m;
	while (regex_search(*resp,m,r)) {
		int64_t min, max, t;
		min = stoi(m[1]);
		max = stoi(m[2]);
		if (min > max) {
			int64_t t = min;
			min = max;
			max = t;
		}
		
		if (min == max) {
			t = min;
		} else {
			t = randomNumber()%(max - min);
			t += min;
		}
		
		*resp = m.prefix().str();
		resp->append(to_string(t));
		resp->append(m.suffix().str());
	}
}

void replaceUser(const twitchMessage *msg, string *resp, vector<command *> *matches) {
	size_t spot = 0;
	
	//replace $(user) with the name of the user who called this command
	while ((spot = resp->find("$(user)",spot)) != string::npos) {
		resp->replace(spot,7,msg->user.displayName);
		spot += 7;
	}
}

void replaceGreeting(const twitchMessage *msg, string *resp, vector<command *> *matches) {
	static map<string,string> greetings = loadGreetings();
	
	if (!greetings.count(msg->user.userid)) {
		*resp = "Hi there " + msg->user.displayName + " casHey //";
	} else {
		//this person has a custom greeting.  Let's use it.
		*resp = greetings[msg->user.userid];
	}
}

void toUser(const twitchMessage *msg, string *resp, vector<command *> *matches) {
	size_t spot = 0;
	string toUser;
	
	//figure out who is the toUser - the first person to be @'d in the message.
	vector<string> words = split(msg->message,' ');
	for (string &i : words) {
		if (i[0] == '@') {
			toUser = i;
			break;
		}
	}
	
	//if no toUser is found, just use the user.
	if (toUser.empty()) toUser = msg->user.displayName;
	
	//replace $(toUser) with the toUser.
	while ((spot = resp->find("$(toUser)",spot)) != string::npos) {
		resp->replace(spot,9,toUser);
		spot += 9;
	}
}

vector<command *> commCheck(string msg) {
	vector<command *> matches;
	regex cmd;
	for (pair<const string,command> &i : cmds) {
		cmd = regex(("^(.*\\s)?" + i.second.call + "(\\W.*\\s*)?$").c_str(),regex_constants::ECMAScript|regex_constants::icase);
		if (regex_match(msg.c_str(),cmd)) {
			matches.push_back(&i.second);
		}
	}
	return matches;
}

void addCom(const twitchMessage *msg, string *resp, vector<command *> *matches) {
	if (!msg->user.mod) {
		//not a mod calling it.  We don't want to respond.
		resp->clear();
		return;
	}
	
	vector<string> m = split(msg->message,' ');
	
	command newC;
	//0 is !addcmd, so start at 1.
	size_t i = 1;
	if (m[i].size() > 4 && m[i].substr(0,4) == "-cd=") {
		newC.cooldown = stoi(m[i].substr(4));
		i++;
	}
	if (m[i].size() > 4 && m[i].substr(0,4) == "-ul=") {
		// user level for this command.
		if (m[i].substr(4) == "owner") {
			newC.user_level = "owner";
		} else if (m[i].substr(4) == "mod") {
			newC.user_level = "mod";
		} else if (m[i].substr(4) == "sub") {
			newC.user_level = "sub";
		}
		i++;
	}
	try {
		regex r(m[i]);
		newC.call = m[i];
		i++;
	} catch (...) {
		logmsg(LVL1, "Invalid new command: %", m[i]);
		*resp = msg->user.displayName + ", Couldn't create command.  Invalid regex.  "
			"See syntax on wikipedia https://en.wikipedia.org/wiki/Regular_expression#Syntax";
		return;
	}
	for (; i < m.size(); i++) {
		newC.response.append(m[i] + " ");
	}
	if (newC.response.size()) newC.response.pop_back();
	
	
	commands dbNew;
	dbNew.comm = newC.call;
	dbNew.resp = newC.response;
	dbNew.cooldown = newC.cooldown;
	dbNew.user_level = newC.user_level;
	
	try {
		//add the command to the database.
		db_wrap db(DBConnPool::getInstance());
		Query q = db.db->query();
		q.insert(dbNew);
		q.execute();
		
		//add the command to the in-memory list.
		cmds[newC.call] = newC;
		logmsg(LVL1, "Added new command [%]", newC.call);
		*resp = msg->user.displayName + ", command \"" + newC.call + "\" added successfully";
	} catch (BadQuery &e) {
		if (e.errnum() == 1062) {
			logmsg(LVL1, "Tried to add duplicate command [%]", newC.call);
			*resp = msg->user.displayName + ", command \"" + newC.call + "\" already exists";
			return;
		}
		logmsg(LVL1, "Couldn't add command [%]: %", newC.call, e.what());
		*resp = msg->user.displayName + ", failed to add command \"" + newC.call + "\" - internal error";
		return;
	} catch (exception &e) {
		logmsg(LVL1, "Couldn't add command [%]: %", newC.call, e.what());
		*resp = msg->user.displayName + ", failed to add command \"" + newC.call + "\" - internal error";
		return;
	}
}

void editCom(const twitchMessage *msg, string *resp, vector<command *> *matches) {
	if (!msg->user.mod) {
		resp->clear();
		return;
	}
	
	command editC;
	
	vector<string> m = split(msg->message,' ');
	
	size_t i = 1;
	command *cmd = NULL;
	short cooldown = -1;
	string user_level;
	if (m[i].size() > 4 && m[i].substr(0,4) == "-cd=") {
		cooldown = stoi(m[i].substr(4));
		i++;
	}
	if (m[i].size() > 4 && m[i].substr(0,4) == "-ul=") {
		if (m[i].substr(4) == "owner" || m[i].substr(4) == "mod" || m[i].substr(4) == "sub") {
			user_level = m[i].substr(4);
		}
		
		i++;
	}
	
	if (cmds.count(m[i])) {
		cmd = &cmds[m[i]];
	}
	
	if (cmd == NULL) {
		vector<command *> matches = commCheck(m[i]);
		if (matches.size() > 1) {
			*resp = msg->user.displayName + ", More than one command matched.  You'll have to be more specific";
			return;
		} else if (matches.size()) {
			cmd = matches[0];
		}
	}
	
	if (cmd == NULL) {
		logmsg(LVL1, "User [%] tried to edit a command, but no match was found: %",
			msg->user.displayName, msg->message);
		*resp = msg->user.displayName + ", No such command found";
		return;
	}
	
	editC = *cmd;
	
	if (cooldown != -1) editC.cooldown = cooldown;
	if (user_level.size()) editC.user_level = user_level;
	
	i++;
	
	editC.response.clear();
	for (; i < m.size(); i++) {
		editC.response += m[i] + " ";
	}
	if (editC.response.size()) editC.response.pop_back();
	
	try {
		commands newc, oldc;
		newc.comm = editC.call;
		newc.resp = editC.response;
		newc.cooldown = editC.cooldown;
		newc.user_level = editC.user_level;
		oldc.comm = cmd->call;
		oldc.resp = cmd->response;
		oldc.cooldown = cmd->cooldown;
		oldc.user_level = cmd->user_level;
		
		db_wrap db(DBConnPool::getInstance());
		
		Query q = db.db->query();
		q.update(oldc,newc);
		SimpleResult res = q.execute();
		
		if (res.rows()) {
			logmsg(LVL1, "User [%] updated command [%]", msg->user.displayName, cmd->call);
			cmds[cmd->call] = editC;
			*resp = msg->user.displayName + ", Command \"" + cmd->call + "\" updated successfully";
			return;
		}
		logmsg(LVL1, "User [%] tried to update command [%], but it wasn't found in the db",
			msg->user.displayName, cmd->call);
		*resp = msg->user.displayName + ", Couldn't find command \"" + cmd->call + "\"";
		
	} catch (BadQuery &e) {
		*resp = msg->user.displayName + ", Couldn't update command due to internal error";
		logmsg(LVL1, "Couldn't update command [%]: %", editC.call, e.what());
	} catch (exception &e) {
		*resp = msg->user.displayName + ", Couldn't update command due to internal error";
		logmsg(LVL1, "Couldn't update command [%]: %", editC.call, e.what());
	}
	
}

void delCom(const twitchMessage *msg, string *resp, vector<command *> *matches) {
	if (!msg->user.mod) {
		resp->clear();
		return;
	}
	
	
	vector<string> m = split(msg->message,' ');
	
	size_t i = 1;
	command *cmd = NULL;
	
	if (cmds.count(m[i])) {
		cmd = &cmds[m[i]];
	}
	
	if (cmd == NULL) {
		vector<command *> matches = commCheck(m[i]);
		if (matches.size() > 1) {
			*resp = msg->user.displayName + ", More than one command matched.  You'll have to be more specific";
			return;
		} else if (matches.size()) {
			cmd = matches[0];
		}
	}
	
	if (cmd == NULL) {
		*resp = msg->user.displayName + ", No such command found";
		logmsg(LVL1, "User [%] tried to delete a command, but no match was found: %",
			msg->user.displayName, msg->message);
		return;
	}
	
	try {
		
		db_wrap db(DBConnPool::getInstance());
		Query q = db.db->query();
		q << "DELETE FROM commands WHERE comm = " << quote << cmd->call;
		SimpleResult res = q.execute();
		
		cmds.erase(cmd->call);
		for (size_t i = 0; i < matches->size(); i++) {
			if ((*matches)[i] == cmd) matches->erase(matches->begin() + i--);
		}
		// *cmd is now invalid
		
		if (res.rows()) {
			logmsg(LVL1, "User [%] deleted command [%]", msg->user.displayName, cmd->call);
			*resp = msg->user.displayName + ", Command deleted successfully";
			return;
		}
		*resp = msg->user.displayName + ", command deleted, but something also went wrong";
		logmsg(LVL1, "Database was desynchronized from in-memory commands");
	} catch (BadQuery &e) {
		*resp = msg->user.displayName + ", couldn't delete command due to internal error";
		logmsg(LVL1, "Couldn't delete command [%]: %", cmd->call, e.what());
	} catch (exception &e) {
		*resp = msg->user.displayName + ", couldn't delete command due to internal error";
		logmsg(LVL1, "Couldn't delete command [%]: %", cmd->call, e.what());
	}
}

map<string,string> loadGreetings() {
	map<string,string> greetings;
	db_wrap db(DBConnPool::getInstance());
	Query q = db.db->query("SELECT userid, greeting FROM users WHERE greeting IS NOT NULL");
	try {
		UseQueryResult res = q.use();
		while (Row row = res.fetch_row()) {
			greetings[row["userid"].c_str()] = row["greeting"].c_str();
		}
	} catch (exception &e) {
		logmsg(LVL1, "Couldn't get greetings: %", e.what());
	}
	return greetings;
}