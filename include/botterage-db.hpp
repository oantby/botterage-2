#ifndef BOTTERAGE_DB
#define BOTTERAGE_DB
#include <mysql++.h>
#include <ssqls.h>

using namespace std;
using namespace mysqlpp;

sql_create_4(commands,1,2,sql_varchar,comm,sql_varchar,resp,sql_smallint,cooldown,sql_varchar,user_level);
sql_create_4(users,1,4,sql_varchar,userid,sql_varchar,displayname,sql_bigint_unsigned,glimmer,sql_varchar_null,greeting);
sql_create_3(inventories,3,2,sql_varchar,userid,sql_int,itemid,sql_bigint,id);
sql_create_6(loot,1,5,sql_varchar,name,sql_varchar,subtext,sql_varchar,link,sql_varchar,soundlink,sql_smallint,rarity,sql_int,id);
sql_create_3(tokens,1,2,sql_varchar,name,sql_varchar,token,sql_int,id);
sql_create_2(user_vars,1,2,sql_varchar,name,sql_varchar,value);


struct is_exotic {
	bool operator() (const loot &l) {
		return l.rarity > 5 && l.rarity < 500;
	}
};

#endif