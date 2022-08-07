#ifndef BOTTERAGE_HPP
#define BOTTERAGE_HPP

#include <vector>
#include <functional>
#include <twitchStructs.hpp>

using namespace std;

extern vector<function<void(twitchMessage)> > chatMsgListeners, subListeners,
                                    whisperListeners, raidListeners,
                                    ritualListeners, msgListeners;

#endif