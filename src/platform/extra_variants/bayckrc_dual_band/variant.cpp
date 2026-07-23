#include "configuration.h"

#ifdef BAYCKRC_DUAL_BAND

#include "mesh/Router.h"
#include <Adafruit_NeoPixel.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static Adafruit_NeoPixel pixel(NEOPIXEL_COUNT, NEOPIXEL_DATA, NEOPIXEL_TYPE);

static void setLED(uint8_t r, uint8_t g, uint8_t b)
{
    pixel.setPixelColor(0, pixel.Color(r, g, b));
    pixel.show();
}

// Runs 2 seconds after lateInitVariant — by then router->addInterface() has been called.
static void radioCheckTask(void *param)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    bool radioOK = router && router->getRadioIface() != nullptr;
    if (radioOK) {
        setLED(0, 255, 0); // green = radio init OK
        LOG_INFO("BAYCKRC: radio OK, LED green");
    } else {
        setLED(255, 0, 0); // red = radio failed
        LOG_WARN("BAYCKRC: radio init FAILED, LED red");
    }
    vTaskDelete(nullptr);
}

void lateInitVariant()
{
    pixel.begin();
    pixel.setBrightness(50);
    setLED(0, 0, 255); // blue = startup
    LOG_INFO("BAYCKRC: LED blue (startup)");
    xTaskCreate(radioCheckTask, "bayckrcLED", 2048, nullptr, 1, nullptr);
}

#endif // BAYCKRC_DUAL_BAND
