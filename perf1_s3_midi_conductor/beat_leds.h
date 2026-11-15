// Independent onboard RGB and WS2812B strip outputs, using Arduino-ESP32 RMT.
#pragma once
#include <Arduino.h>
#include <esp32-hal-rmt.h>

// GPIO48 onboard RGB plus 60 external WS2812B LEDs on GPIO18.
// Boards with the onboard RGB on GPIO38 can still override BEAT_LED_GPIO.
#ifndef BEAT_LED_GPIO
#define BEAT_LED_GPIO 48
#endif
#ifndef STRIP_GPIO
#define STRIP_GPIO 18
#endif
#ifndef STRIP_LED_COUNT
#define STRIP_LED_COUNT 60
#endif
static constexpr uint8_t STRIP_BRIGHTNESS_PERCENT = 50;
static constexpr uint16_t STRIP_LIMIT_MA = 900;
static_assert(STRIP_LED_COUNT > 0 && STRIP_LED_COUNT <= 300, "Use 1..300 strip LEDs");
static_assert(STRIP_GPIO != BEAT_LED_GPIO, "Strip and onboard LED need separate pins");
static_assert(STRIP_GPIO != 19 && STRIP_GPIO != 20 && STRIP_GPIO != 0,
              "Keep the strip off native USB and BOOT pins");

static rmt_data_t stripSymbols[STRIP_LED_COUNT * 24 + 1];
static bool stripReady = false;
static uint32_t stripWriteErrors = 0;

// Both outputs receive the same pulse; only the strip gets a separate power cap.
static bool writeBeatLeds(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(BEAT_LED_GPIO, r, g, b);
  if (!stripReady) return true;  // Keep the onboard LED working after init failure.
  // A timed-out transfer can still be using the buffer. Retry on the next frame.
  if (!rmtTransmitCompleted(STRIP_GPIO)) return false;

  r = uint16_t(r) * STRIP_BRIGHTNESS_PERCENT / 100;
  g = uint16_t(g) * STRIP_BRIGHTNESS_PERCENT / 100;
  b = uint16_t(b) * STRIP_BRIGHTNESS_PERCENT / 100;
  // Conservative estimate: 20 mA/channel at full scale plus 1 mA idle per LED.
  // This is a brightness limiter, not a measurement of the strip's actual current.
  uint32_t channelLoad = uint32_t(r + g + b) * STRIP_LED_COUNT * 20;
  uint32_t budget = uint32_t(STRIP_LIMIT_MA - STRIP_LED_COUNT) * 255;
  if (channelLoad > budget) {
    r = uint32_t(r) * budget / channelLoad;
    g = uint32_t(g) * budget / channelLoad;
    b = uint32_t(b) * budget / channelLoad;
  }

  // GRB, most significant bit first; one RMT symbol per bit at 10 MHz.
  uint8_t grb[3] = {g, r, b};
  size_t k = 0;
  for (int led = 0; led < STRIP_LED_COUNT; ++led) {
    for (uint8_t value : grb) {
      for (int bit = 7; bit >= 0; --bit) {
        bool one = value & (1 << bit);
        stripSymbols[k].level0 = 1;
        stripSymbols[k].duration0 = one ? 8 : 4;
        stripSymbols[k].level1 = 0;
        stripSymbols[k].duration1 = one ? 4 : 8;
        ++k;
      }
    }
  }
  // Explicit 300 us LOW reset/latch, including when clearing the strip at boot.
  stripSymbols[k].level0 = 0;
  stripSymbols[k].duration0 = 1500;
  stripSymbols[k].level1 = 0;
  stripSymbols[k].duration1 = 1500;
  bool ok = rmtWrite(STRIP_GPIO, stripSymbols, k + 1, 20);
  if (!ok) ++stripWriteErrors;
  return ok;
}

static void beginBeatLeds() {
  stripReady = rmtInit(STRIP_GPIO, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
  bool cleared = writeBeatLeds(0, 0, 0);
  Serial.printf("LEDs: onboard GPIO%d | WS2812B GPIO%d x%d | %u%% | estimated limit %u mA | %s\n",
                BEAT_LED_GPIO, STRIP_GPIO, STRIP_LED_COUNT, STRIP_BRIGHTNESS_PERCENT,
                STRIP_LIMIT_MA, !stripReady ? "strip INIT FAILED" : cleared ? "ready" : "strip WRITE FAILED");
}
