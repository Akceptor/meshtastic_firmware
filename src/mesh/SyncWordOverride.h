#pragma once

#ifdef EMAX_900_TX_OLED

// Runtime override for the LoRa sync word, set from the on-device LoRa > Sync Word menu.
// Unlike FanMode (FanControl.h) this MUST match every other node in the mesh to communicate
// at all, so it is persisted in NVS Preferences and survives reboot instead of resetting.
extern uint8_t emaxSyncWord;

// Loads the saved override from NVS into emaxSyncWord (falls back to MESHTASTIC_LORA_SYNCWORD
// if nothing was ever saved). Call once before the radio is first configured.
void loadEmaxSyncWord();

// Updates emaxSyncWord and persists it to NVS.
void saveEmaxSyncWord(uint8_t value);

#endif
