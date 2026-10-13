#include "RadioLib.h"

// ELRS LR1121Driver default switch config (target JSON has no radio_rfsw_ctrl):
// DIO7 = sub-GHz RX, DIO8 = sub-GHz TX (external PA), DIO6 = 2.4G TX, DIO5 = 2.4G RX.
// RadioLib has one MODE_RX for both bands; this board runs sub-GHz, so RX selects DIO7.

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

// Second radio runs at 433 MHz, outside the 900 MHz external PA's band: keep DIO8 low on TX so the
// PA path stays off. Unverified that DIO8 gates the PA; measure 433.125/866.25 MHz output.
#define LR11X0_RFSWITCH_TABLE_2
static const Module::RfSwitchMode_t rfswitch_table_2[] = {
    // mode                  DIO5  DIO6  DIO7  DIO8
    {LR11x0::MODE_STBY, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_RX, {LOW, LOW, HIGH, LOW}},
    {LR11x0::MODE_TX, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_TX_HP, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_TX_HF, {LOW, HIGH, LOW, LOW}},
    {LR11x0::MODE_GNSS, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_WIFI, {HIGH, LOW, LOW, LOW}},
    END_OF_MODE_TABLE,
};
