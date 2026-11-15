// Beat Commander USB audio conductor for a generic ESP32-S3 N16R8.
// Native USB (GPIO19/20) appears as a 48 kHz, 16-bit stereo speaker.
// Optional I2S DAC output passes the iPhone's music to external speakers.
// Requires esp32 by Espressif 3.3.10+, USB Mode: USB-OTG (TinyUSB),
// USB CDC On Boot: Disabled, PSRAM: OPI PSRAM, Flash Size: 16MB.

#if ARDUINO_USB_MODE
#error "Select Tools > USB Mode > USB-OTG (TinyUSB)"
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "ESP_I2S.h"
#include "USB.h"
#include "USBAudioCard.h"
#include "link.h"
#include "beat_engine.h"

// Most N16R8 DevKitC boards use GPIO48; some later boards use GPIO38.
#ifndef BEAT_LED_GPIO
#define BEAT_LED_GPIO 48
#endif
// Set to 1 with a PCM5102A or other 48 kHz stereo I2S DAC wired below.
#ifndef DAC_ENABLED
#define DAC_ENABLED 0
#endif
static constexpr int DAC_BCLK = 4;
static constexpr int DAC_LRCK = 5;
static constexpr int DAC_DOUT = 6;
static constexpr uint32_t AUDIO_TIMEOUT_MS = 300;
static constexpr uint8_t USB_AUDIO_FLAG = 0x04;  // direct CYD input on an LF_FIRE frame
static constexpr uint8_t BCAST[6] = {255, 255, 255, 255, 255, 255};

USBAudioCard audio(48000, UAC_BPS_16, UAC_SPK_STEREO, UAC_MIC_NONE);
#if DAC_ENABLED
I2SClass i2s;
#endif
struct Hop { int16_t samples[HOP]; uint32_t timeMs; };
QueueHandle_t audioQ;
int16_t pending[HOP];
uint16_t pendingCount = 0;
int32_t sum3 = 0;
uint8_t phase3 = 0;
volatile uint32_t lastAudioMs = 0;
volatile uint32_t droppedHops = 0;
volatile uint32_t audioChunks = 0;
volatile bool usbMounted = false;
volatile bool speakerStreaming = false;
uint32_t beatsSent = 0;
uint8_t beatSeq = 0;
uint8_t hue = 0;
bool radioReady = false;
bool locked = false;
uint32_t testUntil = 0;
uint32_t testNext = 0;
uint8_t testPos = 0;
uint32_t flashAt = 0;
uint8_t flashHue = 0;
bool flashDownbeat = false;

void wheel(uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b) {
  uint8_t region = h / 43;
  uint8_t step = (h - region * 43) * 6;
  uint8_t up = step, down = 255 - step;
  switch (region) {
    case 0: r = 255; g = up; b = 0; break;
    case 1: r = down; g = 255; b = 0; break;
    case 2: r = 0; g = 255; b = up; break;
    case 3: r = 0; g = down; b = 255; break;
    case 4: r = up; g = 0; b = 255; break;
    default: r = 255; g = 0; b = down; break;
  }
}

void updateLed(uint32_t now) {
  static uint32_t lastDraw = 0;
  static bool wasLit = false;
  if (now - lastDraw < 16) return;
  lastDraw = now;
  uint32_t age = now - flashAt;
  if (!flashAt || age >= 180) {
    if (wasLit) rgbLedWrite(BEAT_LED_GPIO, 0, 0, 0);
    wasLit = false;
    return;
  }
  wasLit = true;
  uint8_t r, g, b;
  if (flashDownbeat) { r = 255; g = 150; b = 28; }
  else wheel(flashHue, r, g, b);
  uint16_t brightness = (180 - age) * (flashDownbeat ? 190 : 115) / 180;
  rgbLedWrite(BEAT_LED_GPIO, r * brightness / 255, g * brightness / 255, b * brightness / 255);
}

void usbEvent(void *, esp_event_base_t base, int32_t id, void *data) {
  if (base == ARDUINO_USB_EVENTS) {
    if (id == ARDUINO_USB_STARTED_EVENT) usbMounted = true;
    if (id == ARDUINO_USB_STOPPED_EVENT) { usbMounted = false; speakerStreaming = false; }
  } else if (base == ARDUINO_USB_AUDIO_CARD_EVENTS &&
             id == ARDUINO_USB_AUDIO_CARD_INTERFACE_ENABLE_EVENT) {
    auto *event = static_cast<arduino_usb_audio_card_event_data_t *>(data);
    if (event->interface_enable.interface == UAC_INTERFACE_SPK)
      speakerStreaming = event->interface_enable.enable;
  }
}

// This callback runs in the USB receive task. Keep tempo analysis off this task.
void onAudio(void *data, uint16_t len) {
  ++audioChunks;
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  const uint16_t frames = len / 4;  // stereo, little-endian signed 16-bit
  for (uint16_t i = 0; i < frames; ++i) {
    uint16_t j = i * 4;
    int16_t left = (int16_t)(bytes[j] | (bytes[j + 1] << 8));
    int16_t right = (int16_t)(bytes[j + 2] | (bytes[j + 3] << 8));
    sum3 += ((int32_t)left + right) / 2;
    if (++phase3 != 3) continue;
    pending[pendingCount++] = (int16_t)(sum3 / 3);  // 48 kHz -> 16 kHz
    sum3 = 0;
    phase3 = 0;
    if (pendingCount == HOP) {
      Hop hop;
      memcpy(hop.samples, pending, sizeof(pending));
      hop.timeMs = millis();
      if (xQueueSend(audioQ, &hop, 0) != pdTRUE) ++droppedHops;
      pendingCount = 0;
    }
  }
#if DAC_ENABLED
  audio.applyVolume(data, len);
  i2s.write(static_cast<const uint8_t *>(data), len);
#endif
  lastAudioMs = millis();
}

bool startRadio() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(LINK_ESPNOW_CH, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, sizeof(BCAST));
  peer.channel = LINK_ESPNOW_CH;
  return esp_now_add_peer(&peer) == ESP_OK;
}

void sendBeat(uint16_t tempo10, uint8_t pos) {
  if (pos == 0) hue += 40;
  LinkFrame f = linkFrame(LF_FIRE);
  f.bpm10 = tempo10;
  f.beat = pos;
  f.hue = hue;
  f.seq = beatSeq++;
  f.flags = USB_AUDIO_FLAG;
  if (radioReady && esp_now_send(BCAST, (uint8_t *)&f, sizeof(f)) == ESP_OK) ++beatsSent;
  flashAt = millis();
  flashHue = hue;
  flashDownbeat = pos == 0;
}

void fireBeat(uint32_t now) {
  beatNum++;
  uint8_t pos = ((beatNum % 4) + 4) % 4;
  sendBeat((uint16_t)(bpm * 10.0f + 0.5f), pos);
  nextBeat += (uint32_t)(60000.0f / bpm);
  if ((int32_t)(now - nextBeat) >= 0) nextBeat = now + (uint32_t)(60000.0f / bpm);
}

void setup() {
  Serial.begin(115200);  // UART0; native USB is reserved for audio
  pinMode(0, INPUT_PULLUP);  // BOOT button: 10-second radio test
  rgbLedWrite(BEAT_LED_GPIO, 0, 0, 0);
  audioQ = xQueueCreate(16, sizeof(Hop));
  if (!audioQ) { Serial.println("audio queue allocation failed"); return; }
#if DAC_ENABLED
  i2s.setPins(DAC_BCLK, DAC_LRCK, DAC_DOUT);
  if (!i2s.begin(I2S_MODE_STD, 48000, I2S_DATA_BIT_WIDTH_16BIT,
                 I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
    Serial.println("I2S DAC startup failed");
    return;
  }
#endif
  radioReady = startRadio();
  Serial.printf("Beat Commander USB S3 | ESP-NOW %s | channel %d\n", radioReady ? "ready" : "FAILED", LINK_ESPNOW_CH);
  audio.onEvent(usbEvent);
  audio.onData(onAudio);
  if (!audio.begin()) { Serial.println("USB audio startup failed"); return; }
  USB.productName("Beat Commander Audio Conductor");
  USB.onEvent(usbEvent);
  if (!USB.begin()) Serial.println("USB startup failed");
  Serial.println("Tap BOOT or send 't' on UART for a 10-second 120 BPM CYD link test");
  nextBeat = millis() + 500;
}

void loop() {
  uint32_t now = millis();
  static bool bootWasDown = false;
  bool bootDown = digitalRead(0) == LOW;
  bool serialTest = false;
  while (Serial.available()) if (Serial.read() == 't') serialTest = true;
  if (serialTest || (bootDown && !bootWasDown)) {
    testUntil = now + 10000;
    testNext = now;
    testPos = 0;
    Serial.println("TEST: sending 120 BPM directly to CYD for 10 seconds");
  }
  bootWasDown = bootDown;
  Hop hop;
  while (audioQ && xQueueReceive(audioQ, &hop, 0) == pdTRUE) processHop(hop.samples, HOP, hop.timeMs);
  now = millis();
  bool fresh = lastAudioMs && now - lastAudioMs < AUDIO_TIMEOUT_MS;
  static bool wasFresh = false;
  if (!fresh && wasFresh) {
    // Do not broadcast the previous track's BPM when USB audio resumes.
    conf = 0;
    estN = 0;
    bpm = 120;
    level = 0;
    memset(oss, 0, sizeof(oss));
    ossPos = 0;
    phaseAdj = 0;
  }
  wasFresh = fresh;
  bool hasBeat = fresh && level > MIC_GATE && conf >= 0.08f && estN >= 3 && bpm >= 70 && bpm <= 190;
  if (hasBeat && !locked) {
    locked = true;
    beatNum = -1;
    nextBeat = now + (uint32_t)(60000.0f / bpm);
  } else if (!hasBeat && locked) {
    locked = false;
  }
  if ((int32_t)(testUntil - now) > 0) {
    if ((int32_t)(now - testNext) >= 0) {
      sendBeat(1200, testPos);
      testPos = (testPos + 1) & 3;
      testNext += 500;
    }
  } else if (locked) {
    int32_t adj = phaseAdj;
    if (adj) { phaseAdj = 0; nextBeat += adj; }
    if ((int32_t)(now - nextBeat) >= 0) fireBeat(now);
  }
  updateLed(now);
  static uint32_t logMs = 0;
  if (now - logMs >= 2000) {
    logMs = now;
    Serial.printf("USB %s | mounted %u | speaker %u | chunks %lu | BPM %.1f | confidence %.2f | level %.3f | beats %lu | dropped hops %lu\n",
                  fresh ? "audio" : "idle", usbMounted, speakerStreaming,
                  (unsigned long)audioChunks, bpm, conf, level,
                  (unsigned long)beatsSent, (unsigned long)droppedHops);
  }
  delay(1);
}
