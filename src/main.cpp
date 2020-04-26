#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <StreamString.h>
#include <OneWire.h>
#include <EEPROM.h>
#include "SSD1306Wire.h"

#define LED_PIN 15
#define TEMP_PIN 14
#define RELAY_PIN 13
#define BUTTON_PIN 12
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define MyApiKey "e6b21115-e8a2-404f-980a-0b0833cf4ad7"
#define MySSID "JustANet"
#define MyWifiPassword "wifi4you"
#define HEARTBEAT_INTERVAL 30000 // 30 seconds0.068
#define C 0.063 //temperature coefficient


ESP8266WiFiMulti WiFiMulti;
WebSocketsClient webSocket;
SSD1306Wire display(0x3c, SDA, SCL);
Adafruit_NeoPixel strip(1, LED_PIN, NEO_GRB + NEO_KHZ800);
OneWire  ds1(TEMP_PIN);

long prevTime = 0;
int temp, prevTemp = 255;
boolean kettleIsOn = false, lightIsOn = true, switching = false, heating = false, pressed = false, ledDir=false;
byte xtemp, r, g, b, br, ledBr=255;
uint64_t heartbeatTimestamp = 0, ledColorTime = 0;
uint16_t ledColor = 0;
byte data1[2];
bool isConnected = false;

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length);
int getTemp();
void sendTemp(int value);
void updateDisplay();


void setup() {
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_16);
  display.clear();
  display.drawString(30, 15, "Loading");
  display.display();
  pinMode(BUTTON_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(RELAY_PIN, LOW);
  EEPROM.begin(6);
  xtemp = EEPROM.read(0);
  if (xtemp > 100) temp = 100;
  strip.begin();
  r = EEPROM.read(1);
  g = EEPROM.read(2);
  b = EEPROM.read(3);
  br = EEPROM.read(4);
  lightIsOn = (EEPROM.read(5) > 0);
  strip.setBrightness(br);
  if (lightIsOn) strip.setPixelColor(0, r, g, b);
  else strip.setPixelColor(0, 0, 0, 0);
  strip.show();
  WiFiMulti.addAP(MySSID, MyWifiPassword);
  // Waiting for Wifi connect
  while (WiFiMulti.run() != WL_CONNECTED) {
    delay(500);
  }
  webSocket.begin("iot.sinric.com", 80, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setAuthorization("apikey", MyApiKey);
  webSocket.setReconnectInterval(5000);
}


void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  if(type == WStype_TEXT){
        DynamicJsonDocument json(1024);
        deserializeJson(json, (char*) payload);
        String deviceId = json ["deviceId"];
        String action = json ["action"];

        if (deviceId == "5e5b96aea23b266b59a9b423"){ // Device ID of the kettle
          if (action == "action.devices.commands.OnOff") { // On or Off
              String value = json ["value"]["on"];
              kettleIsOn = (value == "true");
              if(kettleIsOn && ((xtemp - temp) < 6)) {
                heating = true;
                switching = true;
              }
          } else if (action == "action.devices.commands.ThermostatTemperatureSetpoint") {
              int itemp = int(json["value"]["thermostatTemperatureSetpoint"]);
              xtemp = (byte) itemp;
              if (xtemp > 100) xtemp = 100;
              EEPROM.write(0, xtemp);
              EEPROM.commit();
          } else if (action == "action.devices.commands.ThermostatSetMode") {
              String value = json["value"]["thermostatMode"];
              if(value == "heat") kettleIsOn = true;
              if(value == "off") kettleIsOn = false;
          }
      } else if(deviceId == "5e5bbc21a23b266b59a9bb7b"){ //device ID of the kettle light
          if (action == "action.devices.commands.OnOff") { // On or Off
              String value = json ["value"]["on"];
              if(value == "true"){
                lightIsOn = true;
                EEPROM.write(5, 255);
                EEPROM.commit();
              } else{
                lightIsOn = false;
                EEPROM.write(5, 0);
                EEPROM.commit();
              }
              
          } else if (action == "action.devices.commands.BrightnessAbsolute") {
              br = byte(int(json["value"]["brightness"]))*2+55;
              strip.setBrightness(br);
              EEPROM.write(4, br);
              EEPROM.commit();
          } else if (action == "action.devices.commands.ColorAbsolute") {
              uint32_t v = int(json["value"]["color"]["spectrumRGB"]);
              r = v >> 16;
              g = v >> 8 & 0xFF;
              b = v & 0xFF;
              EEPROM.write(1, r);
              EEPROM.write(2, g);
              EEPROM.write(3, b);
              EEPROM.commit();
          }
      }
  }
}

void loop() {
  temp = getTemp();
  webSocket.loop();
  if(prevTemp != temp){
    sendTemp(temp);
    prevTemp = temp;
  }
  if (isConnected) {
    uint64_t now = millis();
    if ((now - heartbeatTimestamp) > HEARTBEAT_INTERVAL) { // Send heartbeat in order to avoid disconnections during ISP resetting IPs over night
      heartbeatTimestamp = now;
      webSocket.sendTXT("H");
    }
  }
  if (digitalRead(BUTTON_PIN) == LOW) { //if the button is pressed
    pressed = true;
  } else {
    if (pressed) {
      kettleIsOn = !kettleIsOn;
      if(kettleIsOn && ((xtemp - temp) < 5)) {
        heating = true;
        switching = true;
      }
      pressed = false;
    }
  }
  if (kettleIsOn) {
    if (heating) {
        if ((xtemp - temp) < 5) {
          heating = false;
          switching = true;
        }
      } else if ((xtemp - temp) > 14) {
        heating = true;
        switching = true;
      }
  } else {
    if (heating) {
      switching = true;
      heating = false;
    }
  }
  if (switching) {
    if (heating) {
      digitalWrite(RELAY_PIN, HIGH);
    } else {
      digitalWrite(RELAY_PIN, LOW);
    }
    switching = false;
  }
    if (lightIsOn) {
      if (heating) {
        uint64_t now = millis();
        if((now - ledColorTime) > 20){
          strip.setBrightness(br);
          strip.setPixelColor(0, strip.gamma32(strip.ColorHSV(ledColor, 255, 255)));
          strip.show();
          ledColorTime = millis();
          ledColor += 256;
        }
      } else if (kettleIsOn){
        uint64_t now = millis();
        if((now - ledColorTime) > 10){
          strip.setBrightness(ledBr);
          strip.setPixelColor(0, r, g, b);
          strip.show();
          ledColorTime = millis();
          if(ledBr >= br) ledDir = false;
          if(ledBr <= 25) ledDir = true;
          if(ledDir){
            if(ledBr >= 223) ledBr = 255;
            else ledBr += 32;
          } else{
            if(ledBr <= 57) ledBr = 25;
            else ledBr -= 32;
          }
        }
      } else{
        strip.setBrightness(br);
        strip.setPixelColor(0, r, g, b);
        strip.show();
      }
    } else {
      strip.setPixelColor(0, 0, 0, 0);
      strip.show();
    }
  updateDisplay();
  if (!lightIsOn || !heating) delay(500);
}

int getTemp() {
  ds1.reset();
  ds1.write(0xCC);
  ds1.write(0x44);
  ds1.reset();
  ds1.write(0xCC);
  ds1.write(0xBE);
  data1[0] = ds1.read();
  data1[1] = ds1.read();
  int tempp = ((data1[1] << 8) | data1[0]) * C;
  if (tempp > 100) tempp = 100;
  return tempp;
}

void updateDisplay() {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(20, 0, "heat till: " + (String) xtemp + "°C");
  display.drawString(20, 16, "temp: ");
  display.setFont(ArialMT_Plain_24);
  display.drawString(50, 16, (String) temp + "°C");
  display.setFont(ArialMT_Plain_10);
  if (kettleIsOn) {
    display.drawString(20, 42, "turned on");
    if (heating) display.drawString(20, 52, "heating");
  } else display.drawString(20, 42, "turned off");
  display.display();
}


void sendTemp(int value) {
  DynamicJsonDocument root(1024);
  root["action"] = "SetTemperatureSetting";
  root["deviceId"] = "5e5b96aea23b266b59a9b423";
  JsonObject valueObj = root.createNestedObject("value");
  JsonObject temperatureSetting = valueObj.createNestedObject("temperatureSetting");
  temperatureSetting["scale"] = "CELSIUS";
  temperatureSetting["ambientTemperature"] = float(value);
  StreamString databuf;
  serializeJson(root, databuf);
  webSocket.sendTXT(databuf);
}
