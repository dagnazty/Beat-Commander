// Beat Commander analog USB-C accessory experiment for generic ESP32-S3 N16R8.
// Samples ADC2_CH8 on GPIO19, the native USB D- / analog left-channel contact.
// This uses NO USB protocol and NO external ADC module. It only works if the cable
// really places an appropriately biased, grounded analog signal on that contact.
// GPIO20 / D+ is the right channel and is not used by this mono beat detector.
// UART USB-C port: power/flash/logs. Native USB-C port: analog input only.
// Arduino-ESP32 3.3.10, ESP32S3 Dev Module, USB CDC On Boot disabled.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_adc/adc_oneshot.h>
#include "link.h"
#include "beat_engine.h"

#ifndef BEAT_LED_GPIO
#define BEAT_LED_GPIO 48
#endif
static constexpr uint8_t INPUT_GPIO = 19;
static constexpr adc_channel_t INPUT_CHANNEL = ADC_CHANNEL_8;
static constexpr uint32_t SAMPLE_RATE = 8000;
static constexpr uint32_t SAMPLE_US = 1000000 / SAMPLE_RATE;
// Idle GPIO19 varied by <150 ADC counts in the supplied no-music log.
static constexpr int MIN_AUDIO_P2P = 220;
static constexpr uint8_t DIRECT_BEAT_FLAG = 0x04;
static constexpr uint8_t BCAST[6] = {255,255,255,255,255,255};
struct Hop { int16_t samples[HOP]; uint32_t timeMs; };
static QueueHandle_t audioQ = nullptr;
static adc_oneshot_unit_handle_t adc = nullptr;
static volatile uint32_t readsOk = 0, readErrors = 0, droppedHops = 0, lastReadMs = 0;
static volatile uint32_t lastSignalMs = 0;
static volatile int rawMin = 4095, rawMax = 0;
static uint32_t beatsSent = 0, flashAt = 0, testUntil = 0, testNext = 0;
static uint8_t beatSeq = 0, hue = 0, flashHue = 0, testPos = 0;
static bool flashDownbeat = false, radioReady = false, locked = false;

static void wheel(uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b) {
  uint8_t region=h/43, step=(h-region*43)*6, up=step, down=255-step;
  switch(region) {
    case 0:r=255;g=up;b=0;break;
    case 1:r=down;g=255;b=0;break;
    case 2:r=0;g=255;b=up;break;
    case 3:r=0;g=down;b=255;break;
    case 4:r=up;g=0;b=255;break;
    default:r=255;g=0;b=down;break;
  }
}
static void updateLed(uint32_t now) {
  static uint32_t lastDraw=0;static bool lit=false;
  if(now-lastDraw<16)return;
  lastDraw=now;uint32_t age=now-flashAt;
  if(!flashAt || age>=180){if(lit)rgbLedWrite(BEAT_LED_GPIO,0,0,0);lit=false;return;}
  lit=true;uint8_t r,g,b;
  if(flashDownbeat){r=255;g=150;b=28;}else wheel(flashHue,r,g,b);
  uint16_t brightness=(180-age)*(flashDownbeat?190:115)/180;
  rgbLedWrite(BEAT_LED_GPIO,r*brightness/255,g*brightness/255,b*brightness/255);
}
static bool startRadio() {
  WiFi.mode(WIFI_STA);WiFi.disconnect();
  esp_wifi_set_channel(LINK_ESPNOW_CH,WIFI_SECOND_CHAN_NONE);
  if(esp_now_init()!=ESP_OK)return false;
  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_now_peer_info_t peer={};memcpy(peer.peer_addr,BCAST,6);peer.channel=LINK_ESPNOW_CH;
  return esp_now_add_peer(&peer)==ESP_OK;
}
static void sendBeat(uint16_t bpm10,uint8_t pos) {
  if(pos==0)hue+=40;
  LinkFrame f=linkFrame(LF_FIRE);
  f.bpm10=bpm10;f.beat=pos;f.hue=hue;f.seq=beatSeq++;f.flags=DIRECT_BEAT_FLAG;
  if(radioReady && esp_now_send(BCAST,(uint8_t*)&f,sizeof(f))==ESP_OK)++beatsSent;
  flashAt=millis();flashHue=hue;flashDownbeat=pos==0;
}
static void fireBeat(uint32_t now) {
  beatNum++;uint8_t pos=((beatNum%4)+4)%4;
  sendBeat((uint16_t)(bpm*10.0f+0.5f),pos);
  nextBeat+=(uint32_t)(60000.0f/bpm);
  if((int32_t)(now-nextBeat)>=0)nextBeat=now+(uint32_t)(60000.0f/bpm);
}
static void captureTask(void *) {
  Hop hop={};int sampleIndex=0;
  int32_t dc=0;bool dcReady=false;int lastRaw=0;
  int hopMin=4095,hopMax=0,signalStreak=0;
  int64_t next=esp_timer_get_time();
  for(;;) {
    int64_t now=esp_timer_get_time();
    if(now<next)continue;
    // Keep sampling on the 8 kHz grid; resync after a long Wi-Fi interruption.
    if(now-next>2000)next=now;
    next+=SAMPLE_US;
    int raw=0;
    esp_err_t err=adc_oneshot_read(adc,INPUT_CHANNEL,&raw);
    if(err==ESP_OK) {
      lastRaw=raw;readsOk++;lastReadMs=millis();
      if(raw<rawMin)rawMin=raw;if(raw>rawMax)rawMax=raw;
      if(raw<hopMin)hopMin=raw;if(raw>hopMax)hopMax=raw;
    } else {
      readErrors++;raw=lastRaw;
    }
    if(!dcReady){dc=raw<<10;dcReady=true;}
    dc+=((raw<<10)-dc)>>10;
    int32_t centered=(raw-(dc>>10))*16;
    if(centered>32767)centered=32767;
    if(centered< -32768)centered=-32768;
    // The shared W10 beat engine expects 16 kHz; duplicate each 8 kHz sample.
    hop.samples[sampleIndex++]=(int16_t)centered;
    hop.samples[sampleIndex++]=(int16_t)centered;
    if(sampleIndex==HOP) {
      if(hopMax-hopMin>=MIN_AUDIO_P2P) {
        if(signalStreak<3)++signalStreak;
        if(signalStreak>=3)lastSignalMs=millis();
      } else signalStreak=0;
      hopMin=4095;hopMax=0;
      hop.timeMs=millis();
      if(xQueueSend(audioQ,&hop,0)!=pdTRUE)++droppedHops;
      sampleIndex=0;
    }
    if((readsOk+readErrors)%512==0)vTaskDelay(1); // let the idle task run
  }
}
void setup() {
  Serial.begin(115200); // USB-UART port; native USB is analog GPIO19 in this build
  pinMode(0,INPUT_PULLUP);rgbLedWrite(BEAT_LED_GPIO,0,0,0);
  audioQ=xQueueCreate(16,sizeof(Hop));
  if(!audioQ){Serial.println("Audio queue allocation failed");return;}
  adc_oneshot_unit_init_cfg_t unit={};unit.unit_id=ADC_UNIT_2;
  esp_err_t err=adc_oneshot_new_unit(&unit,&adc);
  if(err!=ESP_OK){Serial.printf("ADC2 init failed: %d\n",err);return;}
  adc_oneshot_chan_cfg_t chan={};chan.atten=ADC_ATTEN_DB_12;chan.bitwidth=ADC_BITWIDTH_12;
  err=adc_oneshot_config_channel(adc,INPUT_CHANNEL,&chan);
  if(err!=ESP_OK){Serial.printf("GPIO19 ADC setup failed: %d\n",err);return;}
  radioReady=startRadio();
  Serial.printf("Analog USB-C GPIO%u beat conductor | ESP-NOW %s | channel %d\n",
      INPUT_GPIO,radioReady?"ready":"FAILED",LINK_ESPNOW_CH);
  Serial.println("BOOT or UART 't': 10-second 120 BPM CYD link test");
  if(xTaskCreatePinnedToCore(captureTask,"auxCapture",4096,nullptr,3,nullptr,0)!=pdPASS)
    Serial.println("Audio capture task failed");
  nextBeat=millis()+500;
}
void loop() {
  uint32_t now=millis();static bool bootWasDown=false;
  bool bootDown=digitalRead(0)==LOW,serialTest=false;
  while(Serial.available())if(Serial.read()=='t')serialTest=true;
  if(serialTest || (bootDown && !bootWasDown)) {
    testUntil=now+10000;testNext=now;testPos=0;
    Serial.println("TEST: 120 BPM to CYD for 10 seconds");
  }
  bootWasDown=bootDown;
  now=millis();
  bool signal=lastSignalMs && now-lastSignalMs<1000;
  Hop hop;
  while(audioQ && xQueueReceive(audioQ,&hop,0)==pdTRUE)
    if(signal)processHop(hop.samples,HOP,hop.timeMs);
  now=millis();bool fresh=lastReadMs && now-lastReadMs<300;
  static bool wasSignal=false;
  if(!signal && wasSignal) {
    conf=0;estN=0;bpm=120;level=0;memset(oss,0,sizeof(oss));ossPos=0;
    ossMean=0;ossVar=1e-4f;prevLow=-100;prevFull=-100;lp=0;phaseAdj=0;
  }
  wasSignal=signal;
  bool hasBeat=signal && fresh &&
      conf>=0.08f && estN>=3 && bpm>=70 && bpm<=190;
  if(hasBeat && !locked) {
    locked=true;beatNum=-1;nextBeat=now+(uint32_t)(60000.0f/bpm);
  } else if(!hasBeat && locked)locked=false;
  if((int32_t)(testUntil-now)>0) {
    if((int32_t)(now-testNext)>=0) {
      sendBeat(1200,testPos);testPos=(testPos+1)&3;testNext+=500;
    }
  } else if(locked) {
    int32_t adj=phaseAdj;if(adj){phaseAdj=0;nextBeat+=adj;}
    if((int32_t)(now-nextBeat)>=0)fireBeat(now);
  }
  updateLed(now);
  static uint32_t logMs=0;
  if(now-logMs>=2000) {
    logMs=now;int lo=rawMin,hi=rawMax;rawMin=4095;rawMax=0;
    Serial.printf("AUX GPIO19 | signal %s | ADC reads %lu errors %lu | raw %d..%d | hops %lu dropped %lu | level %.3f conf %.2f BPM %.1f | beats %lu\n",
      signal?"YES":"NO",
      (unsigned long)readsOk,(unsigned long)readErrors,lo,hi,(unsigned long)hops,
      (unsigned long)droppedHops,level,conf,locked?bpm:0.0f,(unsigned long)beatsSent);
  }
  delay(1);
}
