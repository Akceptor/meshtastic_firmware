#include "RadioLib.h"

// UNIFIED_ESP32C3_LR1121_RX (BAYCKRC C3 900/2400 Dual Band Nano RX) RF switch mapping.
// Source: ExpressLRS LR1121Driver::SetDioAsRfSwitch() default branch (src/lib/LR1121Driver/LR1121.cpp:299-308),
// which is what this product uses — its Targets/targets.json entry has no radio_rfsw_ctrl override.
// ELRS: RfswEnable=0b1111 (DIO5-8), Rx=DIO7, Tx=DIO8, TxHP=DIO8, TxHF=DIO6, Wifi/HF-Rx=DIO5.
//
// RadioLib has no separate "HF receive" mode — it uses MODE_RX for both bands, so 2.4GHz RX is
// routed through the sub-GHz DIO7 path here. Sub-GHz (the normal region) is correct; 2.4GHz RX
// needs DIO5 instead and is not handled by a static table.

static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7,
                                             RADIOLIB_LR11X0_DIO8, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // mode                  DIO5  DIO6  DIO7  DIO8
    {LR11x0::MODE_STBY, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_RX, {LOW, LOW, HIGH, LOW}},
    {LR11x0::MODE_TX, {LOW, LOW, LOW, HIGH}},
    {LR11x0::MODE_TX_HP, {LOW, LOW, LOW, HIGH}},
    {LR11x0::MODE_TX_HF, {LOW, HIGH, LOW, LOW}},
    {LR11x0::MODE_GNSS, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_WIFI, {HIGH, LOW, LOW, LOW}},
    END_OF_MODE_TABLE,
};
