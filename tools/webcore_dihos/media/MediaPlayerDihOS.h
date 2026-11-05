#pragma once
#include "MediaPlayer.h"
#include "MediaStrategy.h"
namespace WebCore {
void registerDihOSMediaEngine(MediaEngineRegistrar);
MediaStrategy* createDihOSMediaStrategy();
void setDihOSMediaTestTracing(bool);
}
