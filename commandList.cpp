#define MYSQLPP_SSQLS_NO_STATICS
#define NO_OUT_EDIT
#include <iostream>
#include <json.hpp>
#include <settings.hpp>
#include <DBConnPool.hpp>
#include <commands.hpp>
#include <botterage-db.hpp>

using namespace std;
using json = nlohmann::json;

void jsonOutput(json &j);
bool loadComms();

map<string,command> cmds;

int main() {
	//initialize a database connection for loadComms();
	DBConnPool::initialize(DBNAME,DBUSER);
	
	json j;
	if (!loadComms()) {
		j["code"] = 500;
		j["message"] = "Couldn't load commands due to internal error.";
		
		jsonOutput(j);
		return 1;
	}
	
	for (const pair<const string,command> &i : cmds) {
		json t;
		t["command"] = i.first;
		t["response"] = i.second.response;
		t["cooldown"] = i.second.cooldown;
		j["commands"].push_back(t);
	}
	
	jsonOutput(j);
	
	return 0;
}

void jsonOutput(json &j) {
	if (j["code"].is_null()) j["code"] = 0;
	
	if (j["code"] != 0) {
		cout << "Status: " << j["code"] << "\r\n";
	} else {
		cout << "Status: 200 OK\r\n";
	}
	cout << "Content-Type: application/json\r\n\r\n"
	     << j;
	
	
}

/*
This is an exact copy of loadComms from commands.cpp.  As such, it probably
shouldn't exist; this file should just require commands.o.  However, to manage
dependencies better, I should make commands.o into a library file or something;
it requires too many extra files to be feasibly added on at will.  This is now
on my to-do list.
*/
bool loadComms() {
	vector<commands> db_comms;
	try {
		db_wrap db(DBConnPool::getInstance());
		Query q = db.db->query("SELECT comm,resp,cooldown FROM commands");
		q.storein(db_comms);
	} catch (exception e) {
		cerr << ": Couldn't get commands from database: " << e.what() << endl;
		return false;
	}
	cmds.clear();
	for (commands c : db_comms) {
		cmds[c.comm] = command(c.comm,c.resp,c.cooldown);
	}
	
	//add in the "built-in" commands
	cmds["^!addcmd [^ ]+ [^ ]+"] = command("^!addcmd [^ ]+ [^ ]+","$(addCom)",0);
	cmds["^!editcmd [^ ]+ [^ ]+"] = command("^!editcmd [^ ]+ [^ ]+","$(editCom)",0);
	cmds["^!delcmd [^ ]+"] = command("^!delcmd [^ ]+","$(delCom)",0);
	return true;
}