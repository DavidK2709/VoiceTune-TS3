#pragma once
void RemoteStart(unsigned short port = 19470);
void RemoteStop();
// Wird aufgerufen, wenn die Schnittstelle nicht starten konnte (z.B. Port belegt). Vor RemoteStart setzen.
void RemoteSetUnavailableCallback(void (*fn)(unsigned short port));
