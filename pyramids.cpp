/*
@var pyramid_response (/me - No pyramids): Message to send to break pyramids
@var pyramid_timeout (150): How long to time out pyramid makers
*/
#include <pyramids.hpp>
#include <botterage.hpp>
#include <twitchState.hpp>
#include <utils.hpp>
#include <mutex>

using namespace std;

extern "C" void pyramids_init(void *args) {
	pyramids::init();
}

namespace pyramids {
	void init() {
		chatMsgListeners.push_back(pyramidHandler);
	}
	
	void pyramidHandler(twitchMessage msg) {
		static string last3[3];
		static string resp = getVar("pyramid_response", "/me - No pyramids");
		static string timeout = getVar("pyramid_timeout", "150");
		bool doIt = false;
		{
			mutex m;
			lock_guard<mutex> lk(m);
			last3[0] = last3[1];
			last3[1] = last3[2];
			last3[2] = msg.message;
			
			
			if (last3[1] == last3[0] + " " + last3[0]
				&& last3[2] == last3[1] + " " + last3[0]) {
				doIt = true;
			}
		}
		
		if (doIt) {
			twitchState *state = twitchState::getInstance();
			state->conn.sendMessage(resp);
			if (!msg.user.mod && timeout.size()) {
				state->conn.sendMessage("/timeout " + msg.user.displayName + " " + timeout + " " + resp);
			}
		}
	}
}