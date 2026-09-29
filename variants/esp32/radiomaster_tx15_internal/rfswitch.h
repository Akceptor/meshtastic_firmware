#include "RadioLib.h"

// From Radiomaster TX15.json radio_rfsw_ctrl [15,0,4,12,12,2,0,1] (ELRS LR1121.cpp:282-310).
// RadioLib has no HF-RX mode: MODE_RX (DIO7) is used on both bands, so 2.4GHz RX is mis-routed (ELRS uses DIO5).
static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7,
                                             RADIOLIB_LR11X0_DIO8, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // mode                  DIO5  DIO6  DIO7  DIO8
    {LR11x0::MODE_STBY, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_RX, {LOW, LOW, HIGH, LOW}},
    {LR11x0::MODE_TX, {LOW, LOW, HIGH, HIGH}},
    {LR11x0::MODE_TX_HP, {LOW, LOW, HIGH, HIGH}},
    {LR11x0::MODE_TX_HF, {LOW, HIGH, LOW, LOW}},
    {LR11x0::MODE_GNSS, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_WIFI, {HIGH, LOW, LOW, LOW}},
    END_OF_MODE_TABLE,
};
