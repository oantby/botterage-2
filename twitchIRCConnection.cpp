/*
@var bot_mute: "true" to prevent bot from sending any chat messages. Primarily for development.
*/

#include <iostream>
#include <twitchStructs.hpp>
#include <twitchIRCConnection.hpp>
#include <twitchState.hpp>
#include <vector>
#include <map>
#include <cstring>
#include <signal.h>
#include <utils.hpp>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <fcntl.h>
#include <poll.h>

bool twitchIRCConnection::openConnection(string channel, string user, string auth) {
	struct sockaddr_in serv_addr;
	struct hostent *server;
	//channel names are always lowercase in twitch irc.
	channel = toLower(channel);
	
	twitchSocket = socket(AF_INET,SOCK_STREAM,0);
	if (twitchSocket < 0) {
		logmsg(LVL1, "Couldn't create socket: %", strerror(errno));
		return false;
	}
	
	// set socket as nonblocking for proper openssl select() usage.
	int flags = fcntl(twitchSocket, F_GETFL, 0);
	flags |= O_NONBLOCK;
	fcntl(twitchSocket, F_SETFL, flags);
	
	server = gethostbyname("irc.chat.twitch.tv");
	if (server == NULL) {
		logmsg(LVL1, "Couldn't resolve irc.chat.twitch.tv: %", strerror(errno));
		return false;
	}
	
	//initialize the addr object.
	bzero((char *)&serv_addr,sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr,(char *)&serv_addr.sin_addr.s_addr,server->h_length);
	serv_addr.sin_port = htons(6697);//Twitch IRC port
	
	pollfd fdpoll;
	memset(&fdpoll, 0, sizeof(fdpoll));
	fdpoll.fd = twitchSocket;
	fdpoll.events = POLLOUT;
	
	if (int ret = connect(twitchSocket,(struct sockaddr *)&serv_addr,sizeof(serv_addr));
		errno == EINPROGRESS) {
		// wait for the connection to finish.
		errno = 0;
		if (poll(&fdpoll, 1, 10000) <= 0) {
			logmsg(LVL1, "Connection to twitch timed out (%)", strerror(errno));
			return false;
		}
	} else if (ret < 0) {
		logmsg(LVL1, "Failed to connect to twitch: %", strerror(errno));
	}
	
	if ((ctx = SSL_CTX_new(TLS_client_method())) == NULL) {
		logmsg(LVL1, "Couldn't initialize tls context: %", strerror(errno));
		return false;
	}
	
	SSL_CTX_load_verify_locations(ctx, NULL, "/etc/ssl/certs");
	
	ssl = SSL_new(ctx);
	SSL_set_fd(ssl,twitchSocket);
	int ret;
	while ((ret = SSL_connect(ssl)) == -1) {
		bool fail = false;
		switch (SSL_get_error(ssl, ret)) {
			case SSL_ERROR_ZERO_RETURN:
			case SSL_ERROR_SYSCALL:
			case SSL_ERROR_SSL:
				// that's a failure.
				fail = true;
				break;
			// all other codes are success
		}
		if (fail) {
			logmsg(LVL1, "Failed to connect secure socket: %", ERR_error_string(ERR_get_error(), NULL));
			close(twitchSocket);
			SSL_free(ssl);
			SSL_CTX_free(ctx);
			return false;
		}
		
		if (poll(&fdpoll, 1, 10000) <= 0) {
			logmsg(LVL1, "Failed to securely connect");
			close(twitchSocket);
			SSL_free(ssl);
			SSL_CTX_free(ctx);
			return false;
		}
	}
	
	logmsg(LVL1, "Secure IRC connected");
	
	//send authentication.  Note, this function doesn't care about the result; if your oauth token is invalid, that's your problem
	outBuffer = "PASS " + auth + "\r\nNICK " + user + "\r\nJOIN #" + channel + "\r\n";
	if (SSL_write(ssl,outBuffer.c_str(),outBuffer.size()) < 0) {
		logmsg(LVL1, "Couldn't write to IRC socket: %", strerror(errno));
		return false;
	}
	fdpoll.events = POLLIN;
	poll(&fdpoll, 1, 10000);
	
	//make sure something came back, then totally ignore it.
	bzero(inBuffer,sizeof(inBuffer));
	if (SSL_read(ssl,inBuffer,sizeof(inBuffer)-1) < 0) {
		logmsg(LVL1, "Couldn't read from IRC socket: %", strerror(errno));
		return false;
	}
	fdpoll.events = POLLOUT;
	poll(&fdpoll, 1, 10000);
	
	//request capabilities to get more user info
	outBuffer = "CAP REQ :twitch.tv/commands\r\nCAP REQ :twitch.tv/tags\r\n";
	if (SSL_write(ssl,outBuffer.c_str(),outBuffer.size()) < 0) {
		logmsg(LVL1, "Couldn't request tags or commands: %", strerror(errno));
		return false;
	}
	poll(&fdpoll, 1, 10000);
	
	logmsg(LVL1, "Now connected");
	channelName = channel;
	return true;
}

void twitchIRCConnection::pong() {
	// twitch uses PING and PONG messages to maintain a heartbeat between client
	// and server.  this is the PONG.
	outBuffer = "PONG :tmi.twitch.tv\r\n";
	SSL_write(ssl,outBuffer.c_str(),outBuffer.size());
	logmsg(LVL3, "PING:PONG");
}

void twitchIRCConnection::closeConnection() {
	SSL_shutdown(ssl);
	SSL_free(ssl);
	SSL_CTX_free(ctx);
	close(twitchSocket);
	twitchSocket = 0;
	channelName.clear();
}

map<string,string> readMessageTags(string in) {
	map<string,string> r;
	string t;
	size_t spot = in.find('@');//likely 0
	if (spot != string::npos) in = in.substr(spot + 1);
	spot = in.find("tmi.twitch.tv");
	if (spot == string::npos) {
		return r;
	} else {
		t = in.substr(0,spot);
		spot = in.find(':',spot + 5);//this colon is right before the message (if one exists);
		if (spot != string::npos) {
			r["message"] = in.substr(spot + 1);
			while (r["message"].size() && r["message"].back() < 32) r["message"].pop_back();
		}
	}
	
	vector<string> parts = split(t,';');
	
	for (string &i : parts) {
		try {
			spot = i.find('=');
			r[i.substr(0,spot)] = i.substr(spot + 1);
		} catch (out_of_range&) {
			//don't actually care.
			logmsg(LVL2, "Didn't find an equal in one of the message tags: %", i);
		}
	}
	
	return r;
}

bool twitchIRCConnection::processString(const string &in, twitchMessage &msg) {
	msg.clear();
	
	
	msg.orig = in;
	msg.tags = readMessageTags(in);
	msg.message = msg.tags["message"];
	if (msg.message.substr(0,8) == "\x01" "ACTION ") {
		msg.message = msg.message.substr(8);
	}
	
	if (in.substr(0,4) == "PING") {
		pong();
		return false;
	} else if (in.find("tmi.twitch.tv PRIVMSG") != string::npos) {
		msg.messageType = twitchMessage::CHATMESSAGE;
		msg.user.userid = msg.tags["user-id"];
		msg.user.displayName = msg.tags["display-name"];
		if (msg.user.displayName.empty()) {
			msg.user.displayName = msg.tags["display-name"] = msg.tags["login"];
		}
		msg.user.mod = msg.tags["mod"] == "1" || toLower(msg.user.displayName) == channelName;
		msg.user.sub = msg.tags["subscriber"] == "1";
		
		logmsg(LVL2, "%: %", msg.user.displayName, msg.message);
		
		return true;
	} else if (in.find("tmi.twitch.tv WHISPER") != string::npos) {
		msg.messageType = twitchMessage::WHISPER;
		msg.user.userid = msg.tags["user-id"];
		msg.user.displayName = msg.tags["display-name"];
		if (msg.user.displayName.empty()) {
			msg.user.displayName = msg.tags["display-name"] = msg.tags["login"];
		}
		
		logmsg(LVL2, "[Whisper] %: %", msg.user.displayName, msg.message);
		
		return true;
	} else if (in.find("USERNOTICE") != string::npos) {
		//Usernotice can refer to a lot of things: sub of any kind, raid, or ritual (which I don't know much about)
		
		msg.user.userid = msg.tags["user-id"];//general case.  some types may overwrite this.
		msg.user.displayName = msg.tags["display-name"];
		if (msg.user.displayName.empty()) {
			msg.user.displayName = msg.tags["display-name"] = msg.tags["login"];
		}
		
		if (msg.tags["msg-id"] == "sub") {
			
			msg.messageType = twitchMessage::NEWSUB;
			return true;
		} else if (msg.tags["msg-id"] == "resub") {
			
			msg.messageType = twitchMessage::RESUB;
			return true;
		} else if (msg.tags["msg-id"] == "subgift") {
			if (msg.tags["msg-param-recipient-display-name"].empty()) {
				msg.tags["msg-param-recipient-display-name"] = msg.tags["msg-param-recipient-user-name"];
			}
			msg.messageType = twitchMessage::SUBGIFT;
			return true;
		} else if (msg.tags["msg-id"] == "raid") {
			if (msg.tags["msg-param-displayName"].empty()) {
				msg.tags["msg-param-displayName"] = msg.tags["msg-param-login"];
			}
			msg.user.displayName = msg.tags["msg-param-displayName"];
			
			msg.messageType = twitchMessage::RAID;
			return true;
		} else if (msg.tags["msg-id"] == "ritual") {
			msg.messageType = twitchMessage::RITUAL;
			return true;
		} else {
			msg.messageType = twitchMessage::UNKNOWN;
			return true;
		}
	} else if (in.find(":tmi.twitch.tv 421") != string::npos) {
		msg.messageType = twitchMessage::UNKNOWN;
		return true;
	} else {
		msg.messageType = twitchMessage::UNKNOWN;
		return true;
	}
}

bool twitchIRCConnection::getMessage(twitchMessage &msg) {
	string in;
	pollfd fdpoll;
	memset(&fdpoll, 0, sizeof(fdpoll));
	fdpoll.fd = twitchSocket;
	fdpoll.events = POLLIN;
	// string, completion status (did I get a newline)
	static deque<pair<string, bool> > pending;
	
	while (pending.size() && pending[0].second) {
		bool ret = processString(pending[0].first, msg);
		pending.pop_front();
		if (ret) return true;
	}
	
	while (poll(&fdpoll, 1, 600000) > 0) {
		//look for messages on a 10 minute cooldown.
		
		msg.clear();
		
		bzero(inBuffer,sizeof(inBuffer));
		
		// here we do some stupid shenanigans for ssl
		bool keep_reading = true;
		bool have_data = false;
		int bytes;
		
		do {
			bytes = SSL_read(ssl,inBuffer,sizeof(inBuffer)-1);
			switch (SSL_get_error(ssl, bytes)) {
				case SSL_ERROR_NONE:
					keep_reading = false;
					have_data = true;
					break;
				case SSL_ERROR_ZERO_RETURN:
					logmsg(LVL1, "Exiting due to a 0-byte read");
					return false;
					break;
				case SSL_ERROR_WANT_READ:
					// data wasn't ready; try again when more network data comes.
					keep_reading = false;
					have_data = false;
					break;
				case SSL_ERROR_WANT_WRITE:
					// doesn't make a whole lot of sense here, but ok.
					keep_reading = false;
					have_data = false;
					break;
				case SSL_ERROR_SYSCALL:
					logmsg(LVL1, "Exiting due to syscall error");
				default:
					return false;
					break;
			}
			
			
		} while (SSL_pending(ssl) && keep_reading);
		if (!have_data) continue;
		
		if (bytes == -1) {
			logmsg(LVL1, "Failed to read data from socket: %", strerror(errno));
			continue;
		} else if (bytes == 0) {
			//Twitch may have disconnected.  Just leave and we'll come back.
			logmsg(LVL1, "Exiting due to a 0-byte read");
			return false;
		}
		
		logmsg(LVL3, "Data read: %", inBuffer);
		
		in = inBuffer;
		
		for (size_t i = 0; i < in.size(); i++) {
			if (in[i] == '\r') in[i] = '\n';
		}
		
		while (in.size()) {
			if (!pending.size() || pending.back().second) pending.push_back(make_pair(string(), false));
			
			size_t pos = in.find('\n');
			pending.back().first.append(in.substr(0, pos));
			if (pos != string::npos) {
				pending.back().second = true;
			} else {
				// exhausted input without finishing this message.
				break;
			}
			
			while (in[pos] == '\n' && ++pos < in.size()) {}
			if (pos >= in.size()) {
				// input exhausted.
				break;
			}
			in = in.substr(pos);
		}
		
		while (pending.size() && pending[0].second) {
			bool ret = processString(pending[0].first, msg);
			pending.pop_front();
			if (ret) return true;
		}
		
	}
	logmsg(LVL1, "Connection lost");
	close(twitchSocket);
	twitchSocket = 0;
	return false;
	
}

bool twitchIRCConnection::sendMessage(string msg) {
	// ensure no other thread is writing a message at the same time.
	mutex m;
	lock_guard<mutex> lk(m);
	
	// log our message, so stdout holds a nice conversation view.
	if (getVar("bot_mute") == "true") {
		logmsg(LVL2, "[Me][Mute]: %", msg);
		return true;
	}
	logmsg(LVL2, "[Me]: %", msg);
	
	msg = "JOIN #" + channelName + "\r\nPRIVMSG #" + channelName + " :" + msg + "\r\n";
	return SSL_write(ssl,msg.c_str(),msg.size()) > 0;
}
