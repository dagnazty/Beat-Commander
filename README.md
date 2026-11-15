# 𓃣 Beat Commander

**Website: [beatcommander.online](https://beatcommander.online)** · **Flash any device: [Flash Station](https://anubisdarkwatch.github.io/flash/#beat)**

One beat, every light in the room. Beat Commander is a family of small boards that share a single beat over
LoRa and ESP-NOW: a **conductor** listens to the music and sets the tempo, **followers** flash in sync across the
venue, and booth controllers can **take command** of the beat at any moment.

It started on the [Meshnology W10 LoRa AIoT Dev Kit](https://meshnology.com/products/meshnology-w10-lora-aiot-dev-kit-esp32-s3-long-range-iot-development-board)
(ESP32-S3), which still makes the best conductor: it listens to the room through the onboard mic, locks onto the
BPM, flashes its 1.54" screen and RGB LED on every beat, and broadcasts each beat over LoRa.

**The crew**
- **Conductor:** Meshnology W10 (mic + LoRa), or the M5StickS3 Pocket Conductor (at the mic, relayed by the W10)
- **USB audio conductor:** generic ESP32-S3 N16R8 receives digital audio from a phone/computer and sends beats over ESP-NOW
- **PERF-1 conductor:** ESP32-S3 USB host reads PERF-1's MIDI clock and sends exact beats to the CYD
- **Followers:** Heltec V3, the Piglet beat badge (and the XIAO kits, coming)
- **Bridge:** Heltec V3, linking LoRa and ESP-NOW for the booth gear
- **Booth controllers:** CYD console, M5Dial, M5Stack Tab5 Command Center, Cardputer ADV terminal, LilyGo T-Deck Plus Beat Deck (its own LoRa radio)
- **Visualizers:** TTGO T-Display, CYD screens and the Rabbit-Labs Phantom, turning every beat into visuals
- **Gateway:** XIAO ESP32-S3 on USB: MIDI clock, Ableton Link, OSC, and the Tab5's radio; or the T-Dongle S3 with a screen
- **Beat Light:** T-Dongle C5, a USB beat light for any charger or power bank
- **Stage lighting:** Beat Strip, an ESP32-C5 board driving a WS2812B / SK6812 LED strip on the beat

Formerly "W10 Beat Conductor". Companion project: [W10-ISM-Waterfall](https://github.com/anubisdarkwatch/W10-ISM-Waterfall).

**New:** the [Beat Command Center](#beat-command-center-m5stack-tab5) on the Tab5's 5" screen, and the [Beat Gateway](#beat-gateway-xiao-esp32-s3--beat-hub): a XIAO ESP32-S3 on USB that turns the beat link into MIDI clock, Ableton Link and OSC.
Also: a touchscreen [booth console](#booth-console-cyd--heltec-v3-bridge) on a CYD that shows the beat link live and can take over the beat.

## PERF-1 USB MIDI conductor (generic ESP32-S3 N16R8)

Flash [PERF-1 S3 v1.2: GPIO48 onboard LED + GPIO18 strip](firmware/PERF1-S3-MIDI-Conductor-v1.2-GPIO48-Strip18-60LED.bin) to the S3 and [CYD console v1.2.0](firmware/CYD-Beat-Console-v1.2.0.bin) to the CYD at address **0x0**. If your board's onboard RGB LED is on GPIO38, use the [GPIO38 + GPIO18 strip build](firmware/PERF1-S3-MIDI-Conductor-v1.2-GPIO38-Strip18-60LED.bin) instead. The older CYD v1.1.0 also follows these beats but labels the source **USB S3** instead of **PERF-1**. Source: [`perf1_s3_midi_conductor/`](perf1_s3_midi_conductor/). Build with **esp32 by Espressif 3.3.10**, **ESP32S3 Dev Module**, **USB Mode: USB-OTG (TinyUSB)**, **USB CDC On Boot: Disabled**, **16 MB flash**, **QIO flash**, **OPI PSRAM**. The sketch itself uses ESP-IDF's USB host library and does not start the TinyUSB device stack.

PERF-1 must have **MIDI out ON** and its own **USB host mode OFF**. Its normal USB device mode sends MIDI Start/Continue/Stop and 24 clock ticks per beat. The S3 host reads those ticks, calculates BPM from quarter-note intervals, and sends beat frames over ESP-NOW channel 1 directly to the CYD. The CYD shows **PERF-1** as the source. Both devices can have separate power; this setup does not route audio through the S3.

**Two LED outputs at once (v1.2):** the onboard RGB on **GPIO48** and a **60-LED WS2812B strip on GPIO18** pulse on the same beats: brighter amber on beat 1, colored pulses on beats 2-4, and a new hue each bar. Both fade to black over 180 ms and stay off without beats. The strip has a separate 50% brightness setting and an estimated 900 mA current cap. The BOOT-button / UART `t` test pulses both outputs at 120 BPM for 10 seconds, even without PERF-1 connected. The older v1.1 builds only drive the onboard LED; they send no strip data on GPIO18.

Connect strip **DIN** to the GPIO18 data signal and strip **GND** to S3 GND. Power the strip from a suitable **5 V supply**, sharing its ground with the S3; GPIO18 supplies data only. For a reliable 5 V strip connection, use a **74AHCT125 level shifter** between GPIO18 and DIN and a **330 ohm series resistor** near DIN. See [Adafruit's NeoPixel wiring guidance](https://learn.adafruit.com/adafruit-neopixel-uberguide/best-practices). Keep native USB GPIO19/20 for PERF-1.

The UART startup log identifies v1.2, both LED pins, the strip length, and whether its driver initialized. Periodic logs include strip write errors. Source settings live in `perf1_s3_midi_conductor/beat_leds.h`: `BEAT_LED_GPIO` (48 or 38), `STRIP_GPIO` (18), `STRIP_LED_COUNT` (60), brightness, and estimated current cap. No extra LED library is needed. Ready-made v1.2 images are merged for address **0x0**; source builds use the **16M Flash (3MB APP/9.9MB FATFS)** partition scheme (`app3M_fat9M_16MB`).

Use the S3's **native USB** port for PERF-1; its other USB-C port is the USB-to-UART port for flashing, power, and 115200 baud logs. The native USB connection must establish the S3 as USB host and present VBUS to the USB link for enumeration, even when PERF-1 powers itself. Generic two-port S3 boards may wire their USB-C receptacle as device-only, so a plain C-to-C cable may not enumerate; a correctly wired powered USB host adapter or breakout may be needed. The firmware log says `USB waiting` until a device enumerates, then prints the VID:PID and `PERF-1 MIDI ready`. `clocks` should rise while PERF-1 plays. Tap BOOT on the running S3 or send `t` on UART for a 10-second 120 BPM test to the CYD independent of USB.

## USB audio conductor (generic ESP32-S3 N16R8)

An analog AUX-to-USB-C cord plugged into the S3's built-in USB-C port does not produce USB audio packets. The firmware below requires a digital USB audio source. The S3's USB D−/D+ pins (GPIO19/20) are also ADC2-capable, but that alone does not provide a verified, safe analog AUX input: the cord's analog ground and USB-C audio accessory wiring must be established, the signal needs bias and protection, and ADC2 conflicts with Wi-Fi used for ESP-NOW. For analog AUX input without an external ADC module, use an ADC1 GPIO with proper AC coupling, bias, and attenuation.

### Experimental analog USB-C pin capture

The [GPIO48 build](firmware/Analog-USB-C-S3-Beat-Conductor-v0.2-GPIO48.bin) and [GPIO38 build](firmware/Analog-USB-C-S3-Beat-Conductor-v0.2-GPIO38.bin) sample **GPIO19 / USB D− as ADC2 channel 8** instead of running a USB protocol. They are merged 16 MB images for address **0x0**. Source: [`analog_usb_s3_beat_conductor/`](analog_usb_s3_beat_conductor/). The sketch takes 8 kHz mono samples, feeds the existing W10 beat engine, sends direct beat frames to the CYD over ESP-NOW, and flashes the S3 RGB LED. Use [CYD v1.2.0](firmware/CYD-Beat-Console-v1.2.0.bin) at address **0x0**. The UART USB-C port supplies the S3 and carries 115200 baud logs; the native USB-C port is treated solely as an analog input.

This build is a **signal-path experiment**, not a verified plug-and-play AUX input. It can read a passive USB-C analog audio accessory only if its left channel reaches D− and its audio ground reaches the S3 ground, with the input voltage held within the S3 GPIO's safe range and biased for its ADC. USB-C analog audio accessory mode normally puts audio ground on SBU, which may be unconnected on a generic S3 board; an unconditioned audio waveform may go below ground. ADC2 may also lose samples while ESP-NOW's Wi-Fi radio is active. Do not apply an unknown analog signal directly to the USB-C port expecting the firmware to protect the pin. The UART log prints `signal YES/NO`, `ADC reads`, `errors`, `raw` minimum and maximum, detector `hops`, `level`, `conf`, and detected `BPM`. The quiet GPIO19 measurements supplied by the user varied by less than 150 ADC counts over two seconds; the firmware now requires three consecutive 8 ms windows with at least 220 counts of signal swing before running beat detection. It clears the old tempo when the signal disappears, so idle pin noise cannot keep broadcasting beats. BOOT or UART `t` sends a 10-second 120 BPM link test without audio.

[`usb_s3_beat_conductor/usb_s3_beat_conductor.ino`](usb_s3_beat_conductor/usb_s3_beat_conductor.ino) makes the S3 appear as a 48 kHz, 16-bit stereo USB speaker. It converts the incoming samples to 16 kHz mono, runs the same beat detector as the StickS3, and sends beat frames on ESP-NOW channel 1 **directly to the CYD**. Flash the [S3 GPIO48 firmware](firmware/USB-S3-Beat-Conductor-v1.1-GPIO48.bin) (or [GPIO38 firmware](firmware/USB-S3-Beat-Conductor-v1.1-GPIO38.bin) if your onboard RGB LED uses that pin) and the updated [CYD firmware](firmware/CYD-Beat-Console-v1.2.0.bin) at address **0x0**, or build the sketches from source. The older CYD v1.0.5 binary ignores direct conductor frames. No W10, Heltec, or LoRa radio is needed for this two-board setup.

Use a **generic ESP32-S3 N16R8 board whose USB connector is wired to native USB D−/D+ on GPIO19/20**. A connector wired only to a CH340/CP210x serial chip cannot receive USB audio. Flash with **esp32 by Espressif 3.3.10+**, board **ESP32S3 Dev Module**, **USB Mode: USB-OTG (TinyUSB)**, **USB CDC On Boot: Disabled**, **Flash Size: 16 MB**, **Flash Mode: QIO**, **PSRAM: OPI PSRAM**. Upload in bootloader mode if necessary; diagnostics use UART0 at 115200 because native USB is occupied by audio.

Connect the S3 native USB port to an iPhone with a data-capable USB-C cable, then select **Beat Commander Audio Conductor** as the audio output if it is not selected automatically. Power the CYD separately. Play music and watch the CYD show **USB S3**, BPM, and beat flashes; the S3 UART log shows `USB audio`, confidence, and sent beats. Keep both boards on the same ESP-NOW channel and `LINK_NET_ID` (the sketch defaults match). Beat transmission starts after the detector has a confident tempo and stops on silence or missing USB audio. The four-count starts from the detected beat grid; the algorithm cannot reliably identify a musical downbeat from every track.

The S3's onboard RGB LED flashes with each sent beat: amber on beat 1, colored pulses on beats 2-4, a new hue each bar, and a 180 ms fade. The BOOT-button 120 BPM link test flashes it too. Source builds can set `BEAT_LED_GPIO` to 48 or 38; the ready-made binaries cover both pins.

**Quick link check:** tap the S3's BOOT button after it has started, or send `t` to its 115200 baud UART serial port. It sends a 120 BPM test beat for 10 seconds without needing USB audio. If the CYD flashes and its `rx` counter rises, the ESP-NOW link is working. The S3's log reports `mounted` (USB host configured), `speaker` (audio stream opened), and `chunks` (audio buffers received). If `chunks` stays at 0, the issue is before beat detection: check the native USB port, cable, and iPhone audio output selection.

**To hear the music:** a bare N16R8 has no audio output. Set `DAC_ENABLED` to `1` in the sketch and attach a stereo I²S DAC (for example PCM5102A): GPIO4 → BCLK, GPIO5 → LRCK, GPIO6 → DIN, plus 3.3 V and GND as required by the DAC module. Connect the DAC's analog output to powered speakers or a mixer. With the default `DAC_ENABLED 0`, the S3 analyzes USB audio but does not play it through. Check the DAC module's voltage and pin labels before wiring.

| W10 conductor (mic, LoRa TX) | Heltec V3 follower (LoRa RX) |
|---|---|
| ![Conductor](docs/conductor.jpg) | ![Follower](docs/follower.jpg) |

## Flash it from the browser (no Arduino IDE)

### ⚡ One click: [Flash Station](https://anubisdarkwatch.github.io/flash/)

Open **[anubisdarkwatch.github.io/flash](https://anubisdarkwatch.github.io/flash/)** in Chrome or Edge on desktop,
find your board's card, click **INSTALL** and pick its port. It always has the newest version, and each card has
that board's download-mode tip. When it asks about erasing, choose **no** to keep your saved settings.

### Manual (any `.bin`, any version)

Every board in the system has a ready-made `.bin`:

1. Plug the board in over USB, open https://espressif.github.io/esptool-js/, click **Connect** and pick its port.
2. Set the flash address to **0x0**, choose the file, click **Program**.
3. When it finishes, unplug and replug (or press RST).

| Board | Role | File |
|---|---|---|
| Meshnology W10 | Conductor (mic, LoRa TX) | [`firmware/W10-Beat-Conductor-v1.1.1.bin`](firmware/W10-Beat-Conductor-v1.1.1.bin) |
| Heltec WiFi LoRa 32 V3 | Follower (flashes on the beat) | [`firmware/Beat-Follower-HeltecV3-v1.1.bin`](firmware/Beat-Follower-HeltecV3-v1.1.bin) |
| Heltec WiFi LoRa 32 V3 | Bridge for the booth console | [`firmware/Beat-Bridge-HeltecV3-v1.1.bin`](firmware/Beat-Bridge-HeltecV3-v1.1.bin) |
| CYD (ESP32-2432S028R) | Booth console / direct USB S3 beat display | [`firmware/CYD-Beat-Console-v1.2.0.bin`](firmware/CYD-Beat-Console-v1.2.0.bin) |
| Generic ESP32-S3 N16R8 | USB audio conductor for CYD, GPIO48 LED | [`firmware/USB-S3-Beat-Conductor-v1.1-GPIO48.bin`](firmware/USB-S3-Beat-Conductor-v1.1-GPIO48.bin) |
| Generic ESP32-S3 N16R8 | Same firmware, GPIO38 LED | [`firmware/USB-S3-Beat-Conductor-v1.1-GPIO38.bin`](firmware/USB-S3-Beat-Conductor-v1.1-GPIO38.bin) |
| Generic ESP32-S3 N16R8 | Experimental analog USB-C pin capture, GPIO48 LED | [`firmware/Analog-USB-C-S3-Beat-Conductor-v0.2-GPIO48.bin`](firmware/Analog-USB-C-S3-Beat-Conductor-v0.2-GPIO48.bin) |
| Generic ESP32-S3 N16R8 | Same experimental firmware, GPIO38 LED | [`firmware/Analog-USB-C-S3-Beat-Conductor-v0.2-GPIO38.bin`](firmware/Analog-USB-C-S3-Beat-Conductor-v0.2-GPIO38.bin) |
| Generic ESP32-S3 N16R8 | PERF-1 USB MIDI host, GPIO48 onboard + GPIO18 strip (60 LEDs) | [`firmware/PERF1-S3-MIDI-Conductor-v1.2-GPIO48-Strip18-60LED.bin`](firmware/PERF1-S3-MIDI-Conductor-v1.2-GPIO48-Strip18-60LED.bin) |
| Generic ESP32-S3 N16R8 | PERF-1 USB MIDI host, GPIO38 onboard + GPIO18 strip (60 LEDs) | [`firmware/PERF1-S3-MIDI-Conductor-v1.2-GPIO38-Strip18-60LED.bin`](firmware/PERF1-S3-MIDI-Conductor-v1.2-GPIO38-Strip18-60LED.bin) |
| M5Stack M5Dial | Handheld takeover controller | [`firmware/M5Dial-Beat-Dial-v1.0.bin`](firmware/M5Dial-Beat-Dial-v1.0.bin) |
| M5Stack StickS3 | Pocket conductor (mic) / beat badge | [`firmware/StickS3-Pocket-Conductor-v1.0.5.bin`](firmware/StickS3-Pocket-Conductor-v1.0.5.bin) |
| Seeed XIAO ESP32-S3 | Beat Gateway (USB MIDI clock + feed for Beat Hub) | [`firmware/Beat-Gateway-XIAO-S3-v1.3.bin`](firmware/Beat-Gateway-XIAO-S3-v1.3.bin) |
| LilyGo T-Dongle S3 | Beat Gateway Dongle: MIDI clock + Beat Hub feed, with a screen | [`firmware/TDongle-Beat-Gateway-v1.1.bin`](firmware/TDongle-Beat-Gateway-v1.1.bin) |
| LilyGo T-Dongle C5 | Beat Light: USB beat light + Beat Hub feed (no MIDI) | [`firmware/TDongle-Beat-Light-C5-v1.1.bin`](firmware/TDongle-Beat-Light-C5-v1.1.bin) |
| ESP32-C5 DevKit / Waveshare C5 | Beat Strip: drives an LED strip on the beat | [`firmware/C5-Beat-Strip-v1.1-GPIO19.bin`](firmware/C5-Beat-Strip-v1.1-GPIO19.bin) |
| Seeed XIAO ESP32-C5 | Beat Strip (strip on D0) | [`firmware/C5-Beat-Strip-v1.0-xiao.bin`](firmware/C5-Beat-Strip-v1.0-xiao.bin) |
| M5Stack Tab5 | Beat Command Center (big touch console, uses the Beat Gateway as its radio) | [`firmware/Tab5-Beat-Command-v1.0.5.bin`](firmware/Tab5-Beat-Command-v1.0.5.bin) |
| M5Stack Cardputer ADV (or Cardputer) | Beat Commander terminal (keyboard booth controller) | [`firmware/Cardputer-Beat-Terminal-v1.0.1.bin`](firmware/Cardputer-Beat-Terminal-v1.0.1.bin) |
| LilyGo T-Deck Plus / T-Deck (915 MHz) | Beat Deck: keyboard controller with its own LoRa radio, visuals, optional bridge | [`firmware/TDeck-Beat-Deck-v1.2.bin`](firmware/TDeck-Beat-Deck-v1.2.bin) |
| Piglet (XIAO ESP32-C5 + OLED) | Beat badge: pocket follower + coverage checker | [`firmware/Piglet-Beat-Badge-C5-v1.0.3.bin`](firmware/Piglet-Beat-Badge-C5-v1.0.3.bin) |
| LilyGo TTGO T-Display | Beat Visualizer (6 beat-synced visual modes) | [`firmware/Beat-Visualizer-v1.4-ttgo.bin`](firmware/Beat-Visualizer-v1.4-ttgo.bin) |
| CYD (ESP32-2432S028R) | Beat Visualizer, badge-sized 2.8" | [`firmware/Beat-Visualizer-v1.4-cyd.bin`](firmware/Beat-Visualizer-v1.4-cyd.bin) |
| CYD 3.2" IPS (ESP32-2432S032C) | Beat Visualizer, 3.2" IPS + capacitive touch | [`firmware/Beat-Visualizer-v1.4-cyd32.bin`](firmware/Beat-Visualizer-v1.4-cyd32.bin) |
| Rabbit-Labs Phantom (ESP32-2432S024) | Beat Visualizer, 2.4" handheld (resistive or capacitive) | [`firmware/Beat-Visualizer-v1.4-phantom.bin`](firmware/Beat-Visualizer-v1.4-phantom.bin) |

If **Connect** fails, put the board in download mode and try again: hold **BOOT** (Heltec: **PRG**) while
plugging in, or hold it, tap **RST**, then release. On the W10, turn the LoRa beat link on with a 1 s hold of
BOOT once it's running.

## What it does

- **Beat detection from the mic** (ES8311 codec): kick-drum band + full-band onset detection every 8 ms
- **Tempo by autocorrelation** over the last ~6 s, checked against beat-period harmonics so off-beat bass doesn't fool it; locks in ~10 s
- **Phase lock**: folds recent onsets at the beat period so flashes land on the kick, not the off-beat
- **Tap tempo** on BOOT overrides the mic for 15 s; the first tap sets beat 1
- **Screen**: full-panel color flash on each beat, big BPM readout, 1-2-3-4 bar counter (downbeat in amber), color shift every bar, live onset scope + level meter
- **RGB LED** (WS2812 on GPIO48) flashes with the beat
- **LoRa beat link** (off by default): 7-byte packet per beat on 916.0 MHz, SF7 / BW500, 10 dBm
- **Relay** (v1.1): put a StickS3 (or any booth controller) next to the speaker and the W10 transmits its beats on
  LoRa for every follower. See [Relay: StickS3 at the mic, W10 on the air](#relay-sticks3-at-the-mic-w10-on-the-air)

Tested against synthetic tracks from 90 to 174 BPM (kick only, kick + off-beat bass + hats, noisy mixes):
tempo within 0.5 BPM, flashes within ~10 ms of the kick. On a real 128 BPM kick track it reads 127.4–128.1.

## Relay: StickS3 at the mic, W10 on the air

Can't get the W10 near the speaker? Put the [StickS3 Pocket Conductor](#pocket-conductor-m5sticks3) there instead
(CONDUCT, **ON AIR**). Its beats reach the W10 over ESP-NOW, and the W10 transmits them on LoRa to every follower:

```
speaker ─► StickS3 (mic) ──ESP-NOW──► W10 ──LoRa 916.0 MHz──► followers, badge, T-Deck...
```

- No Heltec bridge needed: the W10 does the bridge's job (it confirms each beat, so the StickS3, CYD and Dial
  show them as booth beats, and it sends the bridge heartbeat).
- While booth beats are coming in, the W10's own mic steps aside, so the two never collide. Its screen shows
  **REMOTE** and an amber **RELAY** pill, and flashes on the StickS3's beats.
- **Failover:** when the StickS3's beats stop for 2 s (out of range, battery, pocket), the W10 carries on from its own
  mic on the same beat grid, if its **LORA TX** is on. With LORA TX off it's a pure relay and goes quiet instead.
- Any booth controller works the same way: the M5Dial, CYD console, Cardputer, Tab5 or T-Dongle gateway.
- If a Heltec bridge is also running, the W10 leaves the relaying to it (it hears the bridge's heartbeat) and only
  steps aside, so nothing is transmitted twice.

The ESP-NOW hop adds about 1-2 ms. Keep the StickS3 within Wi-Fi range of the W10 (a room or a small venue;
avoid putting it inside a metal rack).

## Controls

| Action | Result |
|---|---|
| Tap **BOOT** on the beat | Tap tempo (overrides the mic for 15 s, first tap = beat 1) |
| Hold **BOOT** 1 s | LoRa beat broadcast on / off |

## Beat packet (for follower boards)

```c
struct __attribute__((packed)) BeatPacket {
  uint8_t  m0, m1;   // 'B', 'T'
  uint8_t  seq;      // increments every beat
  uint16_t bpm10;    // BPM x 10, little-endian
  uint8_t  beat;     // 0..3 position in the bar (0 = downbeat)
  uint8_t  hue;      // current color (0..255), changes every bar
};
```

LoRa: 916.0 MHz, SF7, BW 500 kHz, CR 4/5, preamble 8, explicit header, CRC on, default private sync word.
A follower just listens on those settings and flashes when a packet arrives (~10 ms airtime).
916.0 MHz sits clear of the Meshtastic US default (906.875) and the US LoRaWAN channel plan.

## Follower: Heltec WiFi LoRa 32 V3

![Heltec V3 beat screen](docs/heltec-screens.png)

`heltec_v3_beat_follower/heltec_v3_beat_follower.ino` turns a Heltec V3 into a follower that flashes in sync with the conductor.

- Listens on 916.0 MHz, SF7 / BW500, and flashes the white LED and the OLED (full-screen invert) on every received beat; the downbeat is brighter
- **Beat screen** on the OLED, seven pages: **BEAT** (big BPM, bar count, tempo sweep, 1-2-3-4 boxes), **PULSE**
  (rings out of the centre), **BARS** (equalizer with falling caps), **TUNNEL** (spinning squares), **WARP**
  (starfield that surges on the beat), **SCOPE** (waveform that punches on the beat) and **STATS** (RSSI, SNR, beats
  in, lost, BPM). **PRG** press = next page, hold 1 s = **AUTO** (new visual every 8 bars); saved
- Status **LIVE**, **HOLD** or **WAIT**; while waiting, the visuals idle along so the screen is never blank
- **Flywheel:** if packets drop, it keeps flashing at the last tempo for 4 s (status **HOLD**)
- Optional WS2812 strip on GPIO47 (set `STRIP_LEN`, needs Adafruit NeoPixel), colored by the conductor's hue, which changes every bar
- The OLED runs from its own task, so drawing never delays a beat; the panel flash lands within ~1 ms

Tested on a desk with no losses: 300+ packets, RSSI around −75 dBm, SNR around +10 dB.

Build: board **ESP32S3 Dev Module**, USB CDC On Boot **Disabled** (the V3 uses a USB-UART bridge), PSRAM **Disabled**,
library **LovyanGFX**. Serial monitor at 115200. Previews: `tools/build_preview.sh <M5GFX folder> <out folder> preview_oled.cpp`.

Heltec V3 pins: LoRa NSS 8, SCK 9, MOSI 10, MISO 11, RST 12, BUSY 13, DIO1 14 (TCXO on DIO3 at 1.8 V);
OLED SDA 17, SCL 18, RST 21, power via Vext (GPIO36, LOW = on); LED 35.

Ready-made Heltec V3 devices such as the [Meshnology N33 / N35](https://meshnology.com/collections/all) (cased, with battery) run this follower as-is.
Any SX1262 board can follow: copy the radio settings and the `BeatPacket` struct.

## Booth console: CYD + Heltec V3 bridge

A 2.8" touchscreen for the DJ booth. It shows the beat link live (huge BPM, a full-panel flash in the
conductor's colour on every beat, the 1-2-3-4 bar, RSSI / SNR / losses) and can **take over the beat**:
tap tempo, fine-tune and nudge from the touchscreen, and every follower in the room keeps flashing.

| Bridge found, waiting for the W10 | Live on the beat link | Console, Heltec follower and W10 conductor |
|---|---|---|
| ![Beat Console ready](docs/console-ready.jpg) | ![Beat Console live](docs/console-live.jpg) | ![Console with follower and conductor](docs/console-rig.jpg) |

Every screen (rendered from the firmware's own drawing code):

![Beat Console screens](docs/console-screens.png)

```
W10 conductor ──LoRa 916.0 MHz──► Heltec V3 bridge ──ESP-NOW──► CYD console
                                         ▲                          │
       followers ◄──LoRa (takeover)──────┴────────ESP-NOW (FIRE)────┘
```

The CYD (ESP32-2432S028R) has no LoRa radio, so a Heltec V3 does the radio work. The bridge needs no
screen: a V3 with a dead OLED is perfect for this. If its OLED works, the bridge shows the same beat screen as the
follower (its STATS page shows beats heard, beats sent for the booth and booth gear seen). No soldering anywhere,
just two USB cables.

### Flash it (browser, no installs)

Use the steps in [Flash it from the browser](#flash-it-from-the-browser-no-arduino-ide) with
`CYD-Beat-Console-v1.2.0.bin` on the CYD and `Beat-Bridge-HeltecV3-v1.1.bin` on the Heltec V3.

**First boot of the CYD:**
1. **Display check.** CYD batches use different panels, so the image can come out mirrored or with red and
   blue swapped. Tap **BOOT** to step through the four settings until "<< THIS SIDE IS LEFT" reads normally and
   the AMBER / RED / BLUE blocks match their labels, then hold **BOOT** for 2 s to save.
2. **Touch calibration.** Tap three crosses with a fingernail or stylus.

Both are remembered. To redo them, hold **BOOT** (display) or touch the screen (calibration) during the boot splash.
The console uses the resistive-touch CYD (ESP32-2432S028R, XPT2046).

### Using it

| Control | Action |
|---|---|
| **TAP** | Tap tempo. The first tap is the downbeat. Tapping while following takes over the beat. |
| **−** / **+** | 0.1 BPM per press, hold for 1 BPM steps (takeover only) |
| **◀** / **▶** | Move the beat 10 ms earlier / later (takeover only) |
| **TAKE** / **RELEASE** | Take the beat from the W10 at its current tempo and phase, or hand it back |
| Hold the header 2 s | Rotate 180° (remembered) |
| Tap the large BPM / listening area | Open full-screen visuals |
| Tap a visual | Next effect (all eleven visualizer modes) |
| Hold a visual for 1 s, then release | Return to the BPM console |
| Tap AUTO at the top right of a visual | Toggle automatic effect changes every eight bars |

Status pill: **WAIT** (nothing heard yet), **LIVE** (following a conductor), **HOLD** (packets dropped, flywheel
keeps time for 4 s), **TAKE** (the console is driving), **NO LINK** (no direct S3 beat or bridge heard).
With the USB S3, the CYD shows **USB S3** as its source and receives beats directly over ESP-NOW.

**Combined console and visuals (v1.2.0):** flash [CYD firmware](firmware/CYD-Beat-Console-v1.2.0.bin) at **0x0**. The visual page uses the same received BPM, beat position, beat timing and hue as the console, including direct S3 beats; it does not start a demo beat when the link stops. Effects: PULSE, TUNNEL, BARS, WARP, PLASMA, BPM, KALEIDO, GRID, PYRAMID, FIREWORKS and RAIN. Effect and AUTO settings are saved, and the board starts on the console page. Build with **esp32 3.3.10**, **ESP32 Dev Module**, **Huge APP**, and **LovyanGFX**. The full-screen canvas uses 8-bit color to fit the CYD's RAM; the existing band renderer sends it to the display in 16-bit color.

**Takeover:** TAKE continues the W10's beat grid without a skip, so the room doesn't see a glitch. Then switch
the W10's **LORA TX** off (hold BOOT 1 s on it). If the console still hears the W10, a red banner says so:
two transmitters on the link collide. RELEASE hands back; followers flywheel until the W10 is on air again.
The bridge carries the W10's sequence numbers on, so followers don't count false losses.

With a bridge, takeover flashes 11 ms after sending (`LINK_AIR_MS`), matching LoRa airtime for the followers.
With no bridge, TAKE runs locally on the CYD and flashes immediately.

The CYD's RGB LED flashes with the beat too; the Heltec's white LED blinks per beat heard and double-blinks
per beat sent.

### Build from source

Arduino IDE 2.x, **esp32 by Espressif** core 2.x or 3.x. No extra libraries for either sketch.

| Sketch | Board | Settings |
|---|---|---|
| `cyd_beat_console/` | ESP32 Dev Module | defaults |
| `heltec_v3_espnow_bridge/` | ESP32S3 Dev Module | USB CDC On Boot **Disabled**, PSRAM **Disabled** |

Options at the top of each sketch:
- `LINK_NET_ID` / `LINK_ESPNOW_CH` in `link.h` (keep every copy identical) to run two rigs side by side.
- `PANEL_ST7789 1` for the "2-USB" CYD variant; `PANEL_INVERT` if colours look negative. Mirroring and colour order are set on the device (display check), with `PANEL_MIRROR` / `PANEL_BGR` as the defaults.
- `LINK_POWER_DBM` on the bridge (takeover TX power, default 10 dBm like the W10).
- `DEBUG_TOUCH 1` prints raw touch readings.

The console draws in 24-row bands (about 15 KB of RAM for the whole UI) with the Orbitron and Share Tech Mono
brand fonts baked in as 4-bit anti-aliased bitmaps (`tools/mkfonts_console.py`, fonts SIL OFL 1.1).
PC previews of every screen: `g++ -std=c++17 -I cyd_beat_console tools/preview_console.cpp -o preview && ./preview outdir`.

Both boards print a log at 115200: the bridge one CSV line per beat (`RX,seq,bpm,beat,hue,rssi,snr` /
`TX,...`), the console a status line every 2 s.

### Link frame (ESP-NOW, broadcast)

`LinkFrame` in `link.h`, 18 bytes: magic `'A','D'`, version 1, net id, type (`R` relay, `H` heartbeat,
`F` fire, `A` TX ack), then seq, bpm×10, beat, hue, RSSI, SNR×4, flags, count.

## Handheld controller: M5Dial

![Beat Dial screens](docs/dial-screens.png)

The M5Stack M5Dial (round 240×240 touchscreen, rotary knob, 13.56 MHz RFID reader) joins the same ESP-NOW link as
the CYD console, through the same Heltec V3 bridge. It needs no radio of its own. While following, the ring
flashes in the conductor's colour with beat 1 at the top. After TAKE, the Dial keeps time itself and the bridge
transmits each beat on LoRa.

**Flash it:** use [the browser steps](#flash-it-from-the-browser-no-arduino-ide) with `M5Dial-Beat-Dial-v1.0.bin`.
The M5Dial uses the ESP32-S3's built-in USB, so it normally connects without touching any buttons.

**First boot: enroll.** Scan the tags (or implant) that are allowed to take the beat, up to 4, then press the knob.
Press the knob without scanning anything to run with the lock off. Hold the knob while powering up to enroll again.

| Control | Action |
|---|---|
| Scan an enrolled tag | TAKE: the Dial takes the beat at the W10's current tempo and phase |
| Press the knob (following) | Lock on: opens a 6 s scan window. Lock off: TAKE |
| Turn | 0.1 BPM per click, 0.5 BPM when spun fast (takeover only) |
| Press + turn | Move the beat 10 ms earlier / later per click (takeover only) |
| Tap the screen | Tap tempo, first tap = downbeat (takeover, or following with the lock off) |
| Press the knob (takeover) | RELEASE: hand the beat back. The lock applies again. |

Status pill: **WAIT**, **LIVE** (following the W10), **BOOTH** (following another controller's takeover),
**HOLD**, **TAKE**, **NO LINK**. While the Dial has the beat, a red **W10 ON AIR TOO** banner means the W10 or
another controller is still transmitting.

The tag lock checks the tag's UID only. It's a booth convenience to stop stray hands, not real security: UIDs can be cloned.
The reader is polled only while following, so scanning never delays a beat during takeover. Implants with a small
coil (NExT, xNT) need to sit flat right on the screen, near the middle.

**Works with the CYD console.** During takeover, the Dial re-broadcasts each beat the bridge confirms
(a `R` relay frame with the `LINK_FLAG_ECHO` flag), so a CYD in follow mode keeps showing the booth's tempo. The CYD
shows 0 dBm for those beats, since they didn't come over LoRa. If both controllers take over at once, each one
shows the conflict banner.

**Build from source:** Arduino IDE 2.x, esp32 by Espressif 3.x, board **M5Dial**, library **M5Dial** (installs
M5Unified and M5GFX). Options at the top of `m5dial_beat_dial.ino`: `ENC_PER_CLICK`, `BRIGHTNESS`, `MAX_TAGS`,
`CLICK_SOUND`. Serial log at 115200 prints each scanned tag's UID. The screens are in `m5dial_beat_dial/ui.h`.
`tools/build_preview.sh <M5GFX folder> <out folder> preview_dial.cpp` renders them on a Linux PC (needs SDL2 headers).

## Pocket conductor: M5StickS3

![Pocket Conductor screens](docs/stick-screens.png)

The W10's beat engine running on the M5Stack StickS3's onboard mic, with no LoRa radio needed. **ON AIR** sends
each beat over ESP-NOW to the Heltec V3 bridge, which transmits it on the beat link. Followers, the CYD
console and the M5Dial all follow the StickS3 the same way they follow the W10. Like the Dial, it re-broadcasts each beat the bridge confirms,
so a CYD in follow mode shows its tempo. Switch to **FOLLOW** and it becomes a pocket badge that flashes in the
conductor's colour.

**Flash it:** use [the browser steps](#flash-it-from-the-browser-no-arduino-ide) with
`StickS3-Pocket-Conductor-v1.0.5.bin`.

| Button | CONDUCT | FOLLOW |
|---|---|---|
| Front button (KEY1), tap | Tap tempo: taps override the mic for 15 s, first tap = beat 1 | |
| Front button, hold 1 s | **ON AIR** on / off | |
| Side button (KEY2), tap | Mic gain: club / loud room / quiet room / max (remembered) | |
| Side button, hold 1 s | Switch to FOLLOW | Switch to CONDUCT |

It starts **LOCAL** (beat on screen only). ON AIR goes out through the Heltec bridge, or through the W10 itself
([relay](#relay-sticks3-at-the-mic-w10-on-the-air), W10 v1.1+, which also steps its own mic aside). With an older W10
firmware, turn its **LORA TX** off before going ON AIR. If the bridge still hears another conductor, the pill turns
to **CLASH**. The panel at the bottom shows the onset
scope (bright ticks = detected hits) and the mic level. If the level bar sits near the end, lower the gain. If it barely
moves, raise it.

The bottom line also shows which mic path is running: **M5** (M5Unified's mic driver) or **I2S** (a direct
ES8311 + I2S set-up, used if the first fails). If both fail, CONDUCT shows **MIC FAILED** with the step codes
and the driver's own error name, which are also printed on the serial log at 115200. With no working mic it
starts in FOLLOW so it still syncs with the rig.

**Build from source:** Arduino IDE 2.x, esp32 by Espressif 3.x, board **ESP32S3 Dev Module** with Flash Size 8MB,
PSRAM **Disabled**, USB CDC On Boot **Enabled**. Library **M5Unified** (installs M5GFX). The beat engine is in
`sticks3_pocket_conductor/beat_engine.h`, copied from the W10 sketch. Screens are in `ui.h`; preview them with
`tools/build_preview.sh <M5GFX folder> <out folder> preview_stick.cpp`.

## Beat Visualizer: TTGO T-Display, CYD, 3.2" IPS CYD and Rabbit-Labs Phantom

![Beat Visualizer modes](docs/visualizer.gif)

Turns any colour screen into a beat-synced light. It follows the beat link over ESP-NOW (through the Heltec bridge),
and every beat, the conductor's tempo and its colour drive eleven modes:

| Mode | What it does |
|---|---|
| **PULSE** | rings burst out from the centre on every beat, bigger on beat 1 |
| **TUNNEL** | rotating squares fly at you, speeding up on the beat, flipping direction every bar |
| **BARS** | a spectrum that jumps on each beat (bass-heavy on beat 1) and falls back in time |
| **WARP** | starfield that kicks into warp on the beat |
| **PLASMA** | a flowing colour field that pumps on the beat |
| **BPM** | the tempo, big, with the beat in the bar |
| **KALEIDO** | mirrored shards in 12-fold symmetry, a new pattern on every beat, spin flips every bar (v1.4) |
| **GRID** | synthwave floor rushing towards you, a striped sun on the horizon that swells on beat 1 (v1.4) |
| **PYRAMID** | a glowing pyramid throwing rays from its capstone on every beat, the ankh above on bigger screens (v1.4) |
| **FIREWORKS** | a burst on every beat, the big one on beat 1, with gravity and fading trails (v1.4) |
| **RAIN** | columns of falling hieroglyphs that surge on the beat (v1.4) |

![The five v1.4 modes](docs/visualizer-new-modes.png)

**AUTO** (on by default) moves to the next mode every 8 bars. With no beat link it runs a 120 BPM demo,
so it's never dark. The colour follows the conductor's hue, which changes every bar.

**TTGO T-Display** (1.14"): left button = next mode, hold = AUTO on/off; right button = brightness, hold = turn 180°.
**CYD** (2.8"), **3.2" IPS CYD** (ESP32-2432S032C / C_I) and **Rabbit-Labs Phantom** (2.4", ESP32-2432S024R or C):
the touch screen (finger or stylus) does everything, by how long you hold:

| Touch | Does |
|---|---|
| tap | next mode |
| hold 1.5 s | AUTO on / off |
| hold 4 s | next colour / mirror / invert setting (CYD panels differ: repeat until it looks right) |
| hold 10 s | **power off** (screen, LED and radio off, deep sleep). Wake it with **RST** or by re-plugging power |

While you hold, the top label says what letting go will do. On the 2.8" the BOOT button also works (press = AUTO,
hold 2 s = next setting); on the 3.2" _I model GPIO0 reads "pressed" all the time, so BOOT is ignored there.
The RGB LED on the back blips in the beat colour. Settings are remembered.
The 3.2"'s IPS panel (BGR, inverted, as in Sunton/Freenove's own setup) gives the deepest blacks of the three.
Its side button is the battery switch on the charger chip and isn't connected to the ESP32. On battery, the charger
switches itself off about 30 s after a power-off (the load is too small), and the side button turns it back on.
The **Phantom** is a 2.4" CYD in a handheld case (same display pins, backlight on 27). One build covers both touch
versions: it looks for the capacitive CST820 first and falls back to the resistive XPT2046 on the display's bus.
Its side button is its own power switch (double-click = off); the 10 s hold works too. Pin details thanks to
[SquachWatch-CYD](https://github.com/skizzophrenic/SquachWatch-CYD).

**Flash it:** [Flash Station](https://anubisdarkwatch.github.io/flash/) (Beat Visualizer cards), or the `.bin`s above at 0x0.
**Build from source:** esp32 by Espressif 3.x, board **ESP32 Dev Module**, partition **Huge APP**, library **LovyanGFX**.
Pick the board with `#define BOARD_TTGO` / `BOARD_CYD` / `BOARD_CYD32` / `BOARD_PHANTOM` at the top of `beat_visualizer.ino`. All the visuals are in
`viz.h`, which draws into a sprite of any size, so new screens only need a display config.
Previews and the GIF: `tools/build_preview.sh <M5GFX folder> <out folder> preview_viz.cpp`.

## Beat badge: Piglet (XIAO ESP32-C5)

![Piglet badge screens](docs/piglet-screens.png)

Turns a [Piglet](https://github.com/Hamspiced/piglet) wardriver (XIAO ESP32-C5, 128x64 OLED, one button) into a
pocket beat badge. It follows the beat over ESP-NOW through the Heltec bridge: big BPM, bar count, four beat boxes,
and the whole screen flashes on beat 1. The **STATS** page shows the bridge's signal where you're standing, so you
can walk the venue and check the booth link reaches everywhere.

| Button | Following | In TAKE |
|---|---|---|
| press | switch page (BEAT / STATS) | tap tempo |
| hold 2 s | take over (a bar fills under the header) | release |

Flashing it replaces the Piglet firmware; reflash the Piglet to go back. The SD card and `wardriver.cfg` aren't touched.
**Flash it:** [Flash Station](https://anubisdarkwatch.github.io/flash/) (Beat Badge card), or the `.bin` above at 0x0.
If it won't connect, hold the button while plugging in USB.

**Build from source:** board package **M5Stack** 3.3.x, board **M5StampC5** (same ESP32-C5 chip as the XIAO C5),
library M5GFX. Pins: OLED SDA 23 / SCL 24 (0x3C, falls back to 0x3D), button GPIO0. 1-bit fonts:
`tools/mkfonts_piglet.py`. Previews: `tools/build_preview.sh <M5GFX folder> <out folder> preview_piglet.cpp`.

## Beat Commander terminal: M5Stack Cardputer ADV

![Cardputer terminal screens](docs/cardputer-screens.png)

A keyboard booth controller on the Cardputer ADV (the original Cardputer works too). It talks ESP-NOW itself,
so it needs nothing but the Heltec bridge. It follows the beat like the CYD and the Dial, and can take command:

| Key | Does |
|---|---|
| `T` or the G0 button | take over / release |
| `R` | release |
| `SPACE` | tap tempo (takes over) |
| type `128.5` then `ENTER` | take over at exactly that BPM (60-200). `DEL` fixes a digit, `` ` `` (esc) cancels |
| `-` / `=` | tempo -/+ 0.1 BPM (with shift or fn: -/+ 1) |
| `[` / `]` | nudge the beat 10 ms early / late |
| `H` | help |

The screen shows the BPM, bar count, beat bars in the conductor's colour, and the source / RSSI / lost / sent /
nudge. The status pill reads LIVE, BOOTH, HOLD, TAKE or CLASH (another conductor is still on air).
In TAKE it mirrors each bridge confirmation for the consoles, like the Dial and the StickS3.

**Flash it:** [Flash Station](https://anubisdarkwatch.github.io/flash/) (Beat Commander terminal card), or the `.bin`
above at 0x0. If it won't connect: hold **G0**, plug in USB, let go, then INSTALL.

**Build from source:** Arduino IDE 2.x, board package **M5Stack** 3.x, board **M5Cardputer**, libraries
**M5Cardputer** (1.1+, handles both keyboards), M5Unified, M5GFX. Fonts: `tools/mkfonts_cardputer.py`.
Previews: `tools/build_preview.sh <M5GFX folder> <out folder> preview_cardputer.cpp`.

## Beat Deck: LilyGo T-Deck Plus (915 MHz)

![T-Deck Beat Deck screens](docs/tdeck-screens.png)

The T-Deck Plus has its own SX1262 LoRa radio, so the Beat Deck hears the conductor **directly** on 916.0 MHz and
also follows the ESP-NOW beat link. When it takes command it transmits on LoRa itself (no Heltec bridge needed) and
mirrors every beat on ESP-NOW, so LoRa followers, the CYD, Dial, Cardputer, badge and visualizers all follow it.
The plain T-Deck (915 MHz) runs the same firmware.

| Control | Does |
|---|---|
| trackball click or `T` | take over / release |
| `R` | release |
| `SPACE` | tap tempo (takes over) |
| `alt` + digits, then `ENTER` | take over at exactly that BPM (60-200). The digits are the alt symbols on the letter keys. `DEL` fixes, `X` cancels |
| trackball up / down | tempo +/- 0.1 BPM (`alt` `+` / `-`: +/- 1) |
| trackball left / right | nudge the beat 10 ms early / late |
| `V` or a tap on the screen | console / full-screen visuals |
| `M` / `A` | next visual / auto visuals (new one every 8 bars) |
| `G` | bridge role on / off (saved) |
| `B` / `D` | brightness / display fix (colour order, flip, invert: press until it looks right) |
| `H` | help |

The console shows the BPM, bar count, beat bars in the conductor's colour, and source (CONDUCTOR, BOOTH, DECK,
FLYWHEEL), how the beat arrived (LORA or ESP-NOW), RSSI, lost, sent and nudge. The visuals page is the same eleven
modes as the Beat Visualizer, with a purple frame while you have the beat.

**Bridge role** (`G`): the Deck then does the Heltec bridge's job too: it relays every LoRa beat to ESP-NOW,
transmits other controllers' takeover beats on LoRa and confirms them, and sends the bridge heartbeat. Turn it on
only when there's no Heltec bridge running, or the two will transmit on top of each other.

**Flash it:** [Flash Station](https://anubisdarkwatch.github.io/flash/) (Beat Deck card), or the `.bin` above at 0x0.
If it won't connect: turn it on while holding the **trackball** pressed in (that's BOOT), plug in USB, then INSTALL.
Replaces the T-Deck's current firmware (Meshtastic etc.); reflash that to go back.

**Build from source:** Arduino IDE 2.x, esp32 by Espressif 3.x, board **ESP32S3 Dev Module**, Flash 16MB,
PSRAM **OPI PSRAM**, partition **16M Flash (3MB APP/9.9MB FATFS)**, USB CDC On Boot **Enabled**. Library:
LovyanGFX. Fonts: `tools/mkfonts_viz.py` + `tools/mkfonts_tdeck.py`.
Previews: `tools/build_preview.sh <M5GFX folder> <out folder> preview_tdeck.cpp`.

## Beat Command Center: M5Stack Tab5

![Beat Command Center screens](docs/tab5-screens.png)

The booth console on the Tab5's 5" 1280x720 touchscreen. It follows the beat link like the CYD and the Dial,
and can take the beat over.

- **Center:** a beat ring (beat 1 at the top) that flashes in the conductor's colour, a huge BPM, the bar count,
  and a 2-minute tempo graph.
- **Left:** big pads: **TAKE / RELEASE**, **TAP** (tapping while following takes over), tempo **-/+** 0.1 BPM (hold to
  repeat), and **nudge** the beat 10 ms early or late.
- **Right:** source (W10, booth controller or TAB5), RSSI / SNR, beats in, lost, sent, nudge, gateway.
- **Header:** LIVE / BOOTH / HOLD / TAKEOVER / CLASH / NO GATEWAY, gateway and bridge lights, battery.
  Hold the header for 2 s to turn the screen upside down (remembered).

**Radio:** the Tab5's Wi-Fi chip can't do ESP-NOW, so it uses the [Beat Gateway](#beat-gateway-xiao-esp32-s3--beat-hub)
(v1.1 or newer) as its radio. Plug the XIAO into the Tab5's **USB-A** port with a USB-A to USB-C cable; the Tab5 powers it.
Every frame on the link reaches the Tab5 as a line of text, and in TAKE the Tab5 sends one beat per line back. The
Heltec bridge then puts each beat on LoRa as usual. The XIAO keeps its own jobs too, so a laptop or Pi isn't needed for this.

**Flash it:** [Flash Station](https://anubisdarkwatch.github.io/flash/) (Beat Command Center card), or the `.bin` above at
0x0. If it won't connect, hold the Tab5's reset button for about 2 s until the green LED blinks fast (download mode),
then try again.

**Build from source:** Arduino IDE 2.x, board package **M5Stack** 3.3.x, board **M5Tab5**. Libraries **M5Unified**
(0.2.23+) and **M5GFX** (0.2.30+). The brand fonts are baked in as anti-aliased VLW fonts
(`tools/mkfonts_tab5.py`). Preview the screens on a Linux PC with
`tools/build_preview.sh <M5GFX folder> <out folder> preview_tab5.cpp`.

## Beat Gateway: XIAO ESP32-S3 + Beat Hub

![Beat Hub page](docs/beat-hub.jpg)

A Seeed XIAO ESP32-S3 that listens to the beat link over ESP-NOW and plugs into a computer by USB. It hears
whatever is driving the rig: the W10 (relayed by the Heltec bridge) or a booth controller (StickS3, M5Dial, CYD)
during a takeover. No radio module or wiring, just the bare XIAO.

**On its own, it's a USB MIDI device called "Beat Gateway".** Plug it into a laptop, Pi or MIDI-capable gear and
pick it as the MIDI clock source in Ableton, Traktor, Rekordbox, Resolume, a drum machine and so on. No driver, no app.
- 24 clock ticks per beat, phase-locked to every beat on the link.
- **Start** on the first downbeat, so the bar lines up. **Stop** when the beat has been gone for 8 beats.
- If a beat is missed it keeps time on its own, so the clock never stutters.
- One note per beat on MIDI channel 16 (note 36 on beat 1, 37 on beats 2-4), handy for triggering lights or clips.
- LED: a flash on every beat, longer on beat 1. A short blip every 2 s means it's waiting for a beat.

**Add the Beat Hub script** (`pi_beat_hub/`) on the computer it's plugged into, and you also get:
- **Ableton Link.** Every Link app on the same network follows the conductor's tempo and bar.
- **OSC** to any targets you list: `/beat <1-4> <bpm>`, `/beat/1` to `/beat/4`, `/beat/bpm`, `/beat/bar`.
- **A live page** at `http://<computer>:8090` (above) and JSON at `/api/state`.
- **TrackID** (v2.1+) can take its BPM from the conductor instead of its mic. Set `[conductor] url` in its config.

### Set it up
1. **Flash** the XIAO from [Flash Station](https://anubisdarkwatch.github.io/flash/) (Beat Gateway card), or with the
   `.bin` above at 0x0. A brand-new XIAO connects straight away. To re-flash later, if the flasher can't connect:
   hold **B** (BOOT), tap **R** (RESET), let go of B, then INSTALL.
2. **Plug it in** wherever you want the beat. For MIDI clock alone you're done.
3. **Beat Hub on a Raspberry Pi** (Zero 2W, 3B, 4, 5; 64-bit Raspberry Pi OS for Ableton Link):
   ```
   sudo apt-get install -y git
   git clone https://github.com/anubisdarkwatch/Beat-Commander.git
   cd Beat-Commander/pi_beat_hub && sudo bash install.sh
   ```
   It starts on boot and finds the gateway by its USB name. Edit `config.ini` for OSC targets, then
   `sudo systemctl restart beat-hub`.
4. **Beat Hub on a laptop** (Windows, Mac, Linux): `pip install pyserial aalink`, then
   `python beat_hub.py` in the `pi_beat_hub` folder. Copy `config.example.ini` to `config.ini` first if you want OSC.

On a Pi, the gateway's MIDI port also shows up in ALSA (`aconnect -l`), so you can route its clock to a USB MIDI
interface with `aconnect`.

### Serial feed
USB serial, one line per event (any baud rate):

| Line | Meaning |
|---|---|
| `B,<beat 1-4>,<bpm>,<AIR\|BOOTH>,<us since the beat>,<seq>,<hue>,<rssi>` | A beat. AIR = the W10 on LoRa, BOOTH = a controller. |
| `S,<LOCK\|FREE\|WAIT>,<bpm>,<source>,<beats>,<RUN\|STOP>,<bridge>` | Status, once a second |
| `R,<seq>,<bpm>,<beat 0-3>,<hue>,<rssi>,<snr>,<echo>` / `A,...` / `F,...` / `H,<radio>,<heard>` | v1.1: every raw frame heard (relay, ack, fire, bridge hello) |
| `# ...` | Info |

Commands: `?` (status), `notes on|off`, `transport on|off` (MIDI Start/Stop), `channel 1-16`. They're saved.
v1.1 adds `F,<bpm x10>,<beat 0-3>,<hue>`: put a beat on the link, as a takeover controller does. The bridge transmits
it on LoRa, and the gateway mirrors the bridge's confirmation for consoles. The Tab5 Command Center drives the rig this way.

**Build from source:** Arduino IDE 2.x, esp32 by Espressif 3.x, board **XIAO_ESP32S3**, USB Mode
**USB-OTG (TinyUSB)**, USB CDC On Boot **Disabled**. No extra libraries. Latency figures (how long after the beat each
frame type arrives) are at the top of `xiao_beat_gateway.ino`.

## T-Dongle S3 Beat Gateway + T-Dongle C5 Beat Light

![T-Dongle beat screens](docs/tdongle-screens.png)

LilyGo's USB-stick boards with a 0.96" screen and an RGB LED. Both plug straight into a USB-A port, no cable.

**T-Dongle S3 = Beat Gateway Dongle.** Everything the [XIAO Beat Gateway](#beat-gateway-xiao-esp32-s3--beat-hub) does
(USB MIDI clock, the serial feed for Beat Hub and TrackID, the Tab5's radio), plus a screen and an LED that flashes in
the conductor's colour. Plug it into the DJ laptop for MIDI clock, or into the Pi so Beat Hub is one small stick.
It shows up as "Beat Gateway", so Beat Hub and the Tab5 find it like the XIAO.

**T-Dongle C5 = Beat Light.** The C5 can't be a USB MIDI device, so it's a pocket beat light: plug it into any USB
charger or power bank and the LED and screen pulse on every beat. Handy as cheap beat beacons around the booth.
It also prints the same serial feed, so Beat Hub (v1.1+) can use it for Ableton Link and OSC, just without MIDI clock.

| Button (BOOT) | Does |
|---|---|
| press | next page: **BEAT** (BPM, LIVE / BOOTH / HOLD / WAIT, MIDI clock, bar, beat boxes), **STATS** (source, RSSI, beats, link frames, bridge), then the visuals **PULSE**, **TUNNEL**, **BARS**, **WARP**, **PLASMA**, **KALEIDO**, **GRID**, **PYRAMID**, **FIREWORKS**, **RAIN** (v1.1) |
| hold 1.5 s | **AUTO** on / off: a new visual every 8 bars |
| hold 4 s | display fix (colour order / flip / invert): repeat until it looks right |

The screen tells you what letting go will do. Pages and settings are saved. With no beat, the visuals run a 120 BPM
demo and the LED blips amber every 2 s.

**Flash it:** [Flash Station](https://anubisdarkwatch.github.io/flash/) (Beat Gateway Dongle / Beat Light cards), or
the `.bin`s above at 0x0. If it won't connect: hold the button on the dongle while plugging it in, then INSTALL.

**Build from source:** `tdongle_beat_gateway/`, library LovyanGFX. **S3:** esp32 by Espressif 3.x, board
**ESP32S3 Dev Module**, Flash 16MB, USB Mode **USB-OTG (TinyUSB)**, USB CDC On Boot **Disabled**. **C5:** board package
**M5Stack** 3.x, board **M5StampC5** (any ESP32-C5 board works), USB CDC On Boot **Enabled**, build with
`-DBOARD_TDONGLE_C5`. Fonts: `tools/mkfonts_viz.py` + `tools/mkfonts_dongle.py`. Previews:
`tools/build_preview.sh <M5GFX folder> <out folder> preview_dongle.cpp`.

## Beat Strip: ESP32-C5 + an LED strip

![Beat Strip patterns over two bars](docs/strip-patterns.png)

An ESP32-C5 board (Espressif DevKit, Waveshare C5 board, or Seeed XIAO ESP32-C5) drives a **5V WS2812B or SK6812
RGB** LED strip on the beat, in the conductor's colour, with a bigger hit on beat 1. This is the system's stage
lighting: a strip along the booth front or behind the decks. Above: each pattern over two bars, one column per LED.

| Pattern | Looks like |
|---|---|
| **PULSE** | the whole strip hits on the beat and fades |
| **CHASE** | a comet runs the strip once per beat, changing direction every bar |
| **BURST** | a ring flies out from the centre each beat |
| **BAR** | four sections light up 1-2-3-4 through the bar |
| **SPARKLE** | random LEDs catch every beat, more on beat 1 |
| **RAINBOW** | a rainbow that steps along on every beat |

The DevKit GPIO19 build stays dark while waiting for beats; the XIAO build breathes amber softly. After a link interruption it follows the existing eight-beat flywheel before returning to waiting. The board's own LED flashes too. It also prints the Beat Gateway's serial
feed, so plugged into the Pi it can feed Beat Hub (Ableton Link + OSC, no MIDI clock).

**S3 direct beat build:** [C5 GPIO19 firmware v1.1](firmware/C5-Beat-Strip-v1.1-GPIO19.bin), merged image for flash address **0x0**. It receives the S3's direct ESP-NOW beat frames on channel 1 and flashes the strip using their BPM, beat position and hue. The first boot after upgrading sets GPIO19, PULSE and AUTO off even if older settings exist; later serial changes remain saved. Default strip length is 60 LEDs (`leds <n>` changes it), brightness 50%, current limit 900 mA. PULSE fades without the old constant brightness floor. Status identifies direct beats as `S3`. Build: **esp32 3.3.10**, **ESP32C5 Dev Module**, **USB CDC On Boot Enabled**, **Huge APP**.

**Wiring** (three wires, no soldering if your board has header pins; JST-to-Dupont jumpers fit most strips):

| Strip | Board |
|---|---|
| 5V (red) | 5V / VBUS |
| GND (white or black) | GND |
| DIN, data (green) | **GPIO19** on DevKit / Waveshare with v1.1; **GPIO1 / D0** on XIAO. Change with `pin <gpio>`. |

Up to about 60 LEDs run off the board's USB from a charger or power bank: the firmware caps the current (900 mA by
default, `limit <mA>`). For longer strips, power the strip from its own 5V supply and join its GND to the board's GND.
Get **5V RGB** strips (not 12V, not RGBW).

| BOOT button | Does |
|---|---|
| press | next pattern (the first LEDs light up to show its number) |
| hold 1.5 s | AUTO: a new pattern every 8 bars (green = on, red = off) |
| hold 4 s | brightness 25 / 50 / 75 / 100 % |

Serial commands (Flash Station's console, or any serial monitor): `?`, `leds <n>` (default 60), `pin <gpio>`,
`bright <1-100>`, `limit <mA>`, `pattern <pulse|chase|burst|bar|sparkle|rainbow>`, `auto on|off`, `test` (red,
green, blue, white quarters: checks the wiring and the LED count). Everything is saved.

**Flash it:** [Flash Station](https://anubisdarkwatch.github.io/flash/), **Beat Strip** card for your board (DevKit /
Waveshare, or XIAO). **Build from source:** `c5_beat_strip/`, board package M5Stack 3.x (or esp32 3.3+), board
**M5StampC5** (any ESP32-C5), USB CDC On Boot **Enabled**, no extra libraries; add `-DBOARD_XIAO_C5` for the XIAO.
Pattern preview: `g++ -O2 -o preview_strip tools/preview_strip.cpp && ./preview_strip`.

## Hardware notes (W10)

| Function | Pins |
|---|---|
| I2C (codec, MCP23017, sensors) | SDA = GPIO8, SCL = GPIO7 |
| I2S to ES8311 | MCLK = 1, BCK = 2, LRCK = 4, DOUT = 5, DIN (mic) = 3 |
| Shared SPI (LoRa + LCD) | SCK = 12, MOSI = 13, MISO = 11 |
| LoRa NSS / NRST / BUSY | GPIO14 / MCP23017 GPA3 / GPB2 |
| LCD (ST7789 240×240) | CS = 10, DC = 16, BL = 6, RST = MCP23017 GPA1 |
| WS2812 RGB LED | GPIO48 |
| Speaker amp enable | MCP23017 GPA7 (kept off: mic only) |

Mic setup: ES8311 analog mic, PGA max, 30 dB mic gain, 16 kHz / 16-bit I2S. Audio runs in its own
FreeRTOS task on core 0 so screen redraws never drop samples.

## Build

Arduino IDE 2.x, **esp32 by Espressif** core 3.x, library **Adafruit GFX**.

Board **ESP32S3 Dev Module**: USB CDC On Boot **Enabled**, PSRAM **OPI PSRAM**.

Open `w10_beat_conductor/w10_beat_conductor.ino` and upload. Serial monitor (115200) prints the BPM,
tempo estimate, confidence, onsets per second and mic level once a second.

## Tips

- Works best with real bass: it listens for kicks. Thin clicks through laptop speakers barely register.
- Slow tracks (< ~90 BPM) may lock at double time, which still flashes in time. Tap BOOT to force it.
- If you transmit under an amateur license, add your callsign ID to the beat link.

## Roadmap

- Latency compensation on followers (~10 ms LoRa airtime)
- Followers for XIAO ESP32S3 + Wio-SX1262 and other SX1262 boards
- Bluetooth MIDI clock input from DJ software
- Tilt-controlled effects via the onboard QMI8658 IMU

## License

MIT, see [LICENSE](LICENSE).

## Credits

Built by Anubis Darkwatch with Claude.

Hardware provided by [Meshnology](https://meshnology.com): thanks for supplying the [Meshnology W10 LoRa AIoT Dev Kit](https://meshnology.com/products/meshnology-w10-lora-aiot-dev-kit-esp32-s3-long-range-iot-development-board) for this project. Pin mapping and peripheral details come from Meshnology's W10 documentation and demos.
