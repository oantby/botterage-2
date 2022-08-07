#ifndef DB_CONNECTION_POOL
#define DB_CONNECTION_POOL

#include <iostream>
#include <mysql++/mysql++.h>
#include <vector>
#include <tuple>
#include <unistd.h>
#include <signal.h>
#include <mutex>

using namespace std;
using namespace mysqlpp;

class DBConnPool {
	vector<pair<Connection,bool> > conns;
	string db, uname, pass, db_server;
	mutex m;
	
	static DBConnPool *instance;
	DBConnPool(string dbname, string usr, string p, string server) : db(dbname), uname(usr), pass(p), db_server(server) {}
	~DBConnPool() {disconnect();}
	
	public:
	
	static void initialize(string dbname, string usr, string pass, string server);
	
	static void destroy();
	
	static DBConnPool *getInstance();
	//get a connection from the database.  open a new one, if necessary.
	//additionally, remove any dead connections while looking for a good one.
	Connection *get();
	
	//release a connection
	void release(Connection *c);
	
	//close all existing connections
	void disconnect();
};

//wrapper class for dbconnpool to get a connection and guarantee
//the connection is returned when leaving scope.
class db_wrap {
	DBConnPool *dbpool;
	
	public:
	
	Connection *db;
	db_wrap(DBConnPool *pool) {dbpool = pool; db = dbpool->get();}
	~db_wrap() {dbpool->release(db);}
	
};
#endif