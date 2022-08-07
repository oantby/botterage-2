#ifndef PYRAMID_BLOCKER_HPP
#define PYRAMID_BLOCKER_HPP

#include <twitchStructs.hpp>
using namespace std;

extern "C" void pyramids_init(void *);

namespace pyramids {
	void init();
	void pyramidHandler(twitchMessage msg);
}

#endif