#pragma once
void RemoteStart(unsigned short port = 19470);
void RemoteStop();
// Laeuft die Schnittstelle (Port frei und Server gestartet)? Fuer die Anzeige in den Einstellungen.
bool RemoteIsRunning();
unsigned short RemotePort();
// Wird aufgerufen, wenn die Schnittstelle nicht starten konnte (z.B. Port belegt). Vor RemoteStart setzen.
void RemoteSetUnavailableCallback(void (*fn)(unsigned short port));
