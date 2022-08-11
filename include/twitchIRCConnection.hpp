#ifndef TWITCHIRCCONNECTION
#define TWITCHIRCCONNECTION
#include "twitchStructs.hpp"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <netdb.h>
#include "stringextensions.hpp"
#include <map>
using namespace std;

class twitchIRCConnection {
	private:
	
	string outBuffer;
	char inBuffer[1024];
	int twitchSocket;
	string channelName;
	SSL *ssl;
	SSL_CTX *ctx;
	
	void pong();
	bool processString(const string &in, twitchMessage &msg);
	
	public:
	
	bool openConnection(string channel, string user, string auth);
	
	void closeConnection();
	
	bool getMessage(twitchMessage &msg);
	
	bool sendMessage(string msg);
	
	string channel() {return channelName;}
	
};


#endif
