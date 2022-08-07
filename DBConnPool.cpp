#include <DBConnPool.hpp>
#include <iostream>
#include <vector>
#include <tuple>
#include <unistd.h>
#include <signal.h>
#include <mutex>

using namespace std;
using namespace mysqlpp;

DBConnPool *DBConnPool::instance = NULL;

void DBConnPool::initialize(string dbname, string usr, string pass, string server) {
	if (DBConnPool::instance) delete DBConnPool::instance;
	
	DBConnPool::instance = new DBConnPool(dbname,usr,pass,server);
	atexit(DBConnPool::destroy);
}

void DBConnPool::destroy() {
	if (DBConnPool::instance) {
		delete DBConnPool::instance;
		DBConnPool::instance = NULL;
	}
}

DBConnPool *DBConnPool::getInstance() {
	if (!DBConnPool::instance) {
		throw "Tried to get instance of DBConnPool when uninstantiated";
	}
	return DBConnPool::instance;
}


//get a connection from the database.  open a new one, if necessary.
//additionally, remove any dead connections while looking for a good one.
Connection *DBConnPool::get() {
	
	//use a mutex to ensure no 2 threads get the same connection
	lock_guard<mutex> lk(m);
	for (size_t i = 0; i < conns.size(); i++) {
		if (conns[i].second) {
			continue;
		}
		
		if (!conns[i].first.ping()) {
			logmsg(LVL3, "Removing dead connection %", i);
			conns.erase(conns.begin() + i--);
			continue;
		}
		
		conns[i].second = true;
		return &conns[i].first;
	}
	
	if (conns.size() > 999) {
		logmsg(LVL1, "Way too many db connections. Exiting");
		kill(getpid(),SIGTERM);
	}
	
	Connection c;
	try {
		c.connect(db.c_str(),db_server.c_str(),uname.c_str(),pass.c_str());
	} catch (exception &e) {
		logmsg(LVL1, "Couldn't add connection: %", e.what());
		logmsg(LVL1, "DB reports: %", c.error());
		kill(getpid(),SIGTERM);
	} catch (...) {
		logmsg(LVL1, "Couldn't add connection");
		kill(getpid(),SIGTERM);
	}
	conns.push_back(make_pair(c,true));
	return &conns.back().first;
}

//release a connection, and mark it available for use.
//if the connection isn't live anymore, remove it.
void DBConnPool::release(Connection *c) {
	lock_guard<mutex> lk(m);
	for (size_t i = 0; i < conns.size(); i++) {
		if (&conns[i].first == c) {
			try {
				c->ping();
			} catch (BadQuery &e) {
				logmsg(LVL3, "Removing broken connection %: %", i, e.what());
				conns.erase(conns.begin() + i);
				return;
			} catch (exception &e) {
				logmsg(LVL3, "Removing broken connection %: %", i, e.what());
				conns.erase(conns.begin() + i);
				return;
			}
			
			conns[i].second = false;
			return;
		}
	}
}

//close all existing connections
void DBConnPool::disconnect() {
	while (conns.size()) {
		conns.back().first.disconnect();
		conns.pop_back();
	}
}