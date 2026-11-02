#include "voicechanger.h"

bool VoiceChanger::Process(int16_t* io, int frames)
{
    (void)io;
    (void)frames;
    if (!enabled_.load(std::memory_order_relaxed)) return false;
    return false; // TODO (Voicechanger-Entwickler): hier die Stimme verfremden
}
