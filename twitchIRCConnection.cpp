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
	
	server = gethostbyname("irc.chat.twitch.tv");
	if (server == NULL) {
		logmsg(LVL1, "Couldn't resolve irc.chat.twitch.tv: %", strerror(errno));
		return false;
	}
	
	//initialize the addr object.
	bzero((char *)&serv_addr,sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr,(char *)&serv_addr.sin_addr.s_addr,server->h_length);
	serv_addr.sin_port = htons(6667);//Twitch IRC port
	
	if (connect(twitchSocket,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) {
		logmsg(LVL1, "Error connecting to twitch: %", strerror(errno));
		return false;
	}
	
	//send authentication.  Note, this function doesn't care about the result; if your oauth token is invalid, that's your problem
	outBuffer = "PASS " + auth + "\r\nNICK " + user + "\r\nJOIN #" + channel + "\r\n";
	if (write(twitchSocket,outBuffer.c_str(),outBuffer.size()) < 0) {
		logmsg(LVL1, "Couldn't write to IRC socket: %", strerror(errno));
		return false;
	}
	
	//make sure something came back, then totally ignore it.
	bzero(inBuffer,sizeof(inBuffer));
	if (read(twitchSocket,inBuffer,sizeof(inBuffer)-1) < 0) {
		logmsg(LVL1, "Couldn't read from IRC socket: %", strerror(errno));
		return false;
	}
	
	//request capabilities to get more user info
	outBuffer = "CAP REQ :twitch.tv/commands\r\nCAP REQ :twitch.tv/tags\r\n";
	if (write(twitchSocket,outBuffer.c_str(),outBuffer.size()) < 0) {
		logmsg(LVL1, "Couldn't request tags or commands: %", strerror(errno));
		return false;
	}
	
	logmsg(LVL1, "Now connected");
	channelName = channel;
	return true;
}

void twitchIRCConnection::pong() {
	// twitch uses PING and PONG messages to maintain a heartbeat between client
	// and server.  this is the PONG.
	outBuffer = "PONG :tmi.twitch.tv\r\n";
	write(twitchSocket,outBuffer.c_str(),outBuffer.size());
	logmsg(LVL3, "PING:PONG");
}

void twitchIRCConnection::closeConnection() {
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
	fd_set rfds;
	struct timeval tv;
	FD_ZERO(&rfds);
	FD_SET(twitchSocket,&rfds);
	tv.tv_sec = 600;
	tv.tv_usec = 0;
	string in;
	// string, completion status (did I get a newline)
	static deque<pair<string, bool> > pending;
	
	while (pending.size() && pending[0].second) {
		bool ret = processString(pending[0].first, msg);
		pending.pop_front();
		if (ret) return true;
	}
	
	while (select(twitchSocket+1,&rfds,NULL,NULL,&tv) > 0) {
		//look for messages on a 10 minute cooldown.
		
		//in some implementations, select() updates tv, so we set it back.
		tv.tv_sec = 600;
		tv.tv_usec = 0;
		
		msg.clear();
		
		bzero(inBuffer,sizeof(inBuffer));
		int bytes = read(twitchSocket,inBuffer,sizeof(inBuffer)-1);
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
	return write(twitchSocket,msg.c_str(),msg.size()) > 0;
}
