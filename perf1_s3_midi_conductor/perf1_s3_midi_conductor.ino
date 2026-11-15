// PERF-1 USB MIDI clock -> ESP-NOW -> CYD Beat Console.
// Generic ESP32-S3 N16R8: native USB port is HOST; USB-UART port is for power/logs.
// PERF-1 must be in USB device mode with MIDI out enabled. Both boards may have
// independent power, but the USB connection still needs host VBUS and CC wiring.
// Arduino ESP32 core 3.3.10, board ESP32S3 Dev Module, USB CDC On Boot disabled.
// v1.2: onboard RGB on GPIO48 and 60 WS2812B LEDs on GPIO18 pulse together.

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <usb/usb_host.h>
#include "link.h"
#include "beat_leds.h"

static constexpr uint8_t DIRECT_BEAT_FLAG = 0x04;
static constexpr uint8_t PERF_MIDI_FLAG = 0x08;
static constexpr uint8_t BCAST[6] = {255, 255, 255, 255, 255, 255};
static constexpr uint16_t PERF_VID = 0x1209;
static constexpr uint16_t PERF_PID = 0x0001;

static usb_host_client_handle_t client = nullptr;
static usb_device_handle_t device = nullptr;
static usb_transfer_t *midiIn = nullptr;
static uint8_t newAddress = 0;
static uint8_t midiInterface = 0;
static uint8_t midiEndpoint = 0;
static bool gone = false;
static bool transferInFlight = false;
static bool radioReady = false;
static bool running = false;
static uint32_t ticks = 0;
static uint32_t periodUs = 500000;
static bool havePeriod = false;
static int64_t lastBeatUs = 0;
static int64_t lastTickUs = 0;
static uint8_t beatPos = 0, beatSeq = 0, hue = 0;
static uint32_t sent = 0, midiPackets = 0, midiClocks = 0, transferErrors = 0;
static uint32_t testUntil = 0, testNext = 0;
static uint8_t testPos = 0;
static uint32_t flashAt = 0;
static uint8_t flashHue = 0;
static bool flashDownbeat = false;

static void wheel(uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b) {
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

static void updateLed(uint32_t now) {
  static uint32_t lastDraw = 0;
  static bool wasLit = false;
  if (now - lastDraw < 16) return;
  lastDraw = now;
  uint32_t age = now - flashAt;
  if (!flashAt || age >= 180) {
    if (wasLit && writeBeatLeds(0, 0, 0)) wasLit = false;
    return;
  }
  wasLit = true;
  uint8_t r, g, b;
  if (flashDownbeat) { r = 255; g = 150; b = 28; }
  else wheel(flashHue, r, g, b);
  uint16_t brightness = (180 - age) * (flashDownbeat ? 190 : 115) / 180;
  writeBeatLeds(r * brightness / 255, g * brightness / 255, b * brightness / 255);
}

static void sendBeat(uint16_t bpm10, uint8_t pos) {
  LinkFrame f = linkFrame(LF_FIRE);
  f.seq = beatSeq++;
  f.bpm10 = bpm10;
  f.beat = pos;
  if (pos == 0) hue += 40;
  f.hue = hue;
  f.flags = DIRECT_BEAT_FLAG | PERF_MIDI_FLAG;
  if (radioReady && esp_now_send(BCAST, reinterpret_cast<const uint8_t *>(&f), sizeof(f)) == ESP_OK) ++sent;
  flashAt = millis();
  flashHue = hue;
  flashDownbeat = pos == 0;
}

static void midiByte(uint8_t value, int64_t whenUs) {
  if (value == 0xFA || value == 0xFB) {
    running = true;
    ticks = 0;
    beatPos = 0;
    lastBeatUs = 0;
    lastTickUs = 0;
    havePeriod = false;
    Serial.printf("PERF-1 MIDI %s\n", value == 0xFA ? "START" : "CONTINUE");
    return;
  }
  if (value == 0xFC) {
    running = false;
    Serial.println("PERF-1 MIDI STOP");
    return;
  }
  if (value != 0xF8) return;
  ++midiClocks;
  // PERF-1 sends clock only while playing. This also handles hot-plug mid-song.
  if (!running || (lastTickUs && whenUs - lastTickUs > 1500000)) {
    running = true;
    ticks = 0;
    beatPos = 0;
    lastBeatUs = 0;
    havePeriod = false;
  }
  lastTickUs = whenUs;
  if ((ticks++ % 24) != 0) return;
  if (lastBeatUs) {
    int64_t measured = whenUs - lastBeatUs;
    if (measured >= 300000 && measured <= 1000000) {
      periodUs = havePeriod ? (periodUs * 3 + measured) / 4 : measured;
      havePeriod = true;
    }
  }
  lastBeatUs = whenUs;
  uint16_t bpm10 = (uint16_t)(600000000ULL / periodUs);
  if ((int32_t)(testUntil - millis()) <= 0) sendBeat(bpm10, beatPos);
  beatPos = (beatPos + 1) & 3;
}

static void onTransfer(usb_transfer_t *transfer) {
  transferInFlight = false;
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
    for (int i = 0; i + 3 < transfer->actual_num_bytes; i += 4) {
      const uint8_t *packet = transfer->data_buffer + i;
      if ((packet[0] & 0x0F) != 0x0F) continue;  // single-byte USB MIDI system message
      ++midiPackets;
      midiByte(packet[1], esp_timer_get_time());
    }
  } else if (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE &&
             transfer->status != USB_TRANSFER_STATUS_CANCELED) {
    ++transferErrors;
  }
  if (!gone && transfer->status != USB_TRANSFER_STATUS_NO_DEVICE &&
      transfer->status != USB_TRANSFER_STATUS_CANCELED &&
      usb_host_transfer_submit(transfer) == ESP_OK) transferInFlight = true;
}

static void onClientEvent(const usb_host_client_event_msg_t *event, void *) {
  if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) newAddress = event->new_dev.address;
  if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE && event->dev_gone.dev_hdl == device) gone = true;
}

static void closeDevice() {
  if (transferInFlight) return;  // wait for canceled transfer callback
  if (midiIn) { usb_host_transfer_free(midiIn); midiIn = nullptr; }
  if (device) {
    if (midiEndpoint) usb_host_interface_release(client, device, midiInterface);
    usb_host_device_close(client, device);
  }
  device = nullptr;
  midiEndpoint = 0;
  gone = false;
  running = false;
  Serial.println("PERF-1 disconnected");
}

static void openDevice(uint8_t address) {
  if (device || usb_host_device_open(client, address, &device) != ESP_OK) return;
  const usb_device_desc_t *devDesc = nullptr;
  if (usb_host_get_device_descriptor(device, &devDesc) != ESP_OK) { closeDevice(); return; }
  Serial.printf("USB device %04x:%04x\n", devDesc->idVendor, devDesc->idProduct);
  if (devDesc->idVendor != PERF_VID || devDesc->idProduct != PERF_PID) {
    Serial.println("Not PERF-1 running application firmware");
    closeDevice(); return;
  }
  const usb_config_desc_t *config = nullptr;
  if (usb_host_get_active_config_descriptor(device, &config) != ESP_OK) { closeDevice(); return; }
  const uint8_t *raw = reinterpret_cast<const uint8_t *>(config);
  bool inMidi = false;
  uint16_t packetSize = 0;
  for (int offset = 0; offset + 2 <= config->wTotalLength;) {
    const uint8_t *d = raw + offset;
    if (d[0] < 2 || offset + d[0] > config->wTotalLength) break;
    if (d[1] == 4 && d[0] >= 9) {
      inMidi = d[3] == 0 && d[5] == 1 && d[6] == 3;  // Audio / MIDIStreaming, alt 0
      if (inMidi) midiInterface = d[2];
    } else if (inMidi && d[1] == 5 && d[0] >= 7 &&
               (d[2] & 0x80) && (d[3] & 3) == 2) {
      midiEndpoint = d[2];
      packetSize = d[4] | (d[5] << 8);
      break;
    }
    offset += d[0];
  }
  if (!midiEndpoint || !packetSize || packetSize > 64 ||
      usb_host_interface_claim(client, device, midiInterface, 0) != ESP_OK ||
      usb_host_transfer_alloc(packetSize, 0, &midiIn) != ESP_OK) {
    Serial.println("PERF-1 USB MIDI interface unavailable");
    closeDevice(); return;
  }
  midiIn->device_handle = device;
  midiIn->bEndpointAddress = midiEndpoint;
  midiIn->callback = onTransfer;
  midiIn->num_bytes = packetSize;
  transferInFlight = usb_host_transfer_submit(midiIn) == ESP_OK;
  Serial.printf("PERF-1 MIDI ready: interface %u endpoint %02x\n", midiInterface, midiEndpoint);
}

static void hostTask(void *) {
  for (;;) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
  }
}

void setup() {
  Serial.begin(115200);  // USB-UART port; the native USB port is used for PERF-1
  pinMode(0, INPUT_PULLUP);
  Serial.println("PERF-1 S3 MIDI Conductor v1.2");
  beginBeatLeds();
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(LINK_ESPNOW_CH, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() == ESP_OK) {
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BCAST, 6);
    peer.channel = LINK_ESPNOW_CH;
    radioReady = esp_now_add_peer(&peer) == ESP_OK;
  }
  usb_host_config_t host = {};
  host.intr_flags = ESP_INTR_FLAG_LEVEL1;
  esp_err_t result = usb_host_install(&host);
  if (result != ESP_OK) { Serial.printf("USB host install failed: %d\n", result); return; }
  xTaskCreatePinnedToCore(hostTask, "usbHost", 4096, nullptr, 4, nullptr, 0);
  usb_host_client_config_t cfg = {};
  cfg.max_num_event_msg = 4;
  cfg.async.client_event_callback = onClientEvent;
  result = usb_host_client_register(&cfg, &client);
  if (result != ESP_OK) Serial.printf("USB client failed: %d\n", result);
  Serial.printf("PERF-1 host ready | ESP-NOW %s | ch %d\n", radioReady ? "ready" : "FAILED", LINK_ESPNOW_CH);
  Serial.println("Tap BOOT or send 't' on UART to test both LED outputs and the CYD link at 120 BPM");
}

void loop() {
  if (client) usb_host_client_handle_events(client, pdMS_TO_TICKS(5));
  if (gone) closeDevice();
  if (newAddress) { uint8_t address = newAddress; newAddress = 0; openDevice(address); }
  if (midiIn && !transferInFlight && !gone)
    transferInFlight = usb_host_transfer_submit(midiIn) == ESP_OK;
  uint32_t now = millis();
  static bool wasDown = false;
  bool down = digitalRead(0) == LOW, serialTest = false;
  while (Serial.available()) if (Serial.read() == 't') serialTest = true;
  if (serialTest || (down && !wasDown)) {
    testUntil = now + 10000; testNext = now; testPos = 0;
    Serial.println("TEST: 120 BPM on both LED outputs and direct to CYD for 10 seconds");
  }
  wasDown = down;
  if ((int32_t)(testUntil - now) > 0 && (int32_t)(now - testNext) >= 0) {
    sendBeat(1200, testPos);
    testPos = (testPos + 1) & 3;
    testNext += 500;
  }
  if (running && lastTickUs && esp_timer_get_time() - lastTickUs > 1500000) running = false;
  updateLed(millis());
  static uint32_t logAt = 0;
  if (now - logAt > 2000) {
    logAt = now;
    Serial.printf("USB %s | MIDI %lu packets %lu clocks | %s | %.1f BPM | beats %lu | errors %lu | strip %s errors %lu\n",
                  device ? "connected" : "waiting", (unsigned long)midiPackets,
                  (unsigned long)midiClocks, running ? "playing" : "stopped",
                  60000000.0f / periodUs, (unsigned long)sent, (unsigned long)transferErrors,
                  stripReady ? "ready" : "FAILED", (unsigned long)stripWriteErrors);
  }
}
