#include <Arduino.h>
#include <cmath>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>
#include <stdlib.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WebSocketsServer.h>
#include <Ticker.h>

using namespace std;

const char *IOT_TYPE = "generic-2out";

struct Output_t
{
  const uint8_t pin;
  float state;
  unsigned int addressState;
  bool active;
  unsigned int addressActive;
};

char iotDeviceId[31];
unsigned int iotDeviceIdAddres;
Output_t outDevices[] = {{.pin = 5}, {.pin = 4}};
const size_t lenOutputs = sizeof(outDevices) / sizeof(Output_t);

char wifiHostname[11];

WiFiClient espClient;
PubSubClient mqttClient;
WebSocketsServer webSocket(81);

void setup()
{
  Serial.begin(115200);
  eepromInitialize();
  setupInitButton();
  setupOutputDevices();
  setWifiHostname();
  if (getActiveOutputCount() && wifiStationConnect() && setTopicBase())
  {
    mqttInit();
  }
  else
  {
    WiFi.disconnect();
    gotoIotInitMode();
  }
}

bool gotoIotInitMode()
{
  if (mqttClient.connected())
  {
    mqttClient.disconnect();
  }
  startInitMode();
}

void setupOutputDevices()
{
  for (Output_t &item : outDevices)
  {
    if (!item.active)
    {
      continue;
    }
    pinMode(item.pin, OUTPUT);
    analogWrite(item.pin, round(item.state * 1024));
  }
}

void setWifiHostname()
{
  uint8_t mac[6];
  WiFi.macAddress(mac);
  strcpy(wifiHostname, "ESP_");
  for (size_t i = 3; i < 6; i++)
  {
    char b[3];
    sprintf(b, "%02X", mac[i]);
    strcat(wifiHostname, b);
  }
  WiFi.hostname(String(wifiHostname));
}

void loop()
{
  iot_start_init_loop();
  int mqttState = mqttClient.state();
  switch (mqttState)
  {
  case MQTT_CONNECTED:
  case MQTT_DISCONNECTED:
    break;
  default:
    mqttInit();
  }
  mqttClient.loop();
  webSocket.loop();
}
