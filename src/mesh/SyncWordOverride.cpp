#include "SyncWordOverride.h"

#ifdef EMAX_900_TX_OLED

#include "RadioLibInterface.h" // MESHTASTIC_LORA_SYNCWORD default
#include <Preferences.h>

uint8_t emaxSyncWord = MESHTASTIC_LORA_SYNCWORD;

void loadEmaxSyncWord()
{
    Preferences prefs;
    prefs.begin("meshtastic", true);
    emaxSyncWord = prefs.getUChar("loraSyncWord", MESHTASTIC_LORA_SYNCWORD);
    prefs.end();
}

void saveEmaxSyncWord(uint8_t value)
{
    emaxSyncWord = value;
    Preferences prefs;
    prefs.begin("meshtastic", false);
    prefs.putUChar("loraSyncWord", value);
    prefs.end();
}

#endif
