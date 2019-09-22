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

struct OutputDevice_t
{
  const uint8_t pin;
  float state;
  unsigned int addressState;
  bool active;
  unsigned int addressActive;
};

enum IOTState_t : byte
{
  started,
  operating,
  initMode
};

enum InitState_t : byte
{
  stopped = 0,
  idle = 1,
  succeed = 2,
  failed = 3,
  working = 4
};

IOTState_t _iotState;
InitState_t _initState;
Ticker initMode_ticker;

OutputDevice_t outDevices[] = {{.pin = 5}, {.pin = 4}};
const size_t lenOutputs = sizeof(outDevices) / sizeof(OutputDevice_t);

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
    _iotState = IOTState_t::operating;
  }
  else
  {
    // TODO, indicate problem and its kind (WiFi o misconfig), but do not start InitMode on every ocasion!
    // WiFi.disconnect();
    // startLEDBlinker();
    // gotoIotInitMode();
  }
}

bool gotoIotInitMode()
{
  Serial.println(" - - Going to Initialization Mode.");
  startLEDBlinker();
  if (mqttClient.connected())
  {
    mqttClient.disconnect();
  }
  initMode_ticker.once(60, leaveIotInitMode);
  bool ret = startInitMode();
  if (ret)
  {
    _iotState = IOTState_t::initMode;
  }
  else
  {
    Serial.println(" - - Failed to start Initialization Mode.");
    stopLEDBlinker(true);
  }
  return ret;
}

void leaveIotInitMode()
{
  Serial.println(" - - Leaving the Initialization Mode.");
  initMode_ticker.detach();
  if (_initState == InitState_t::succeed || _initState == InitState_t::idle && mqttClient.connected())
  {
    _iotState = IOTState_t::operating;
  }
  else
  {
    _iotState = IOTState_t::started;
  }
  _initState = InitState_t::stopped;
  endInitMode();
  stopLEDBlinker(true);
  // TODO restart ESP here in case of _iotState == IOTState_t::started?
}

void setupOutputDevices()
{
  for (OutputDevice_t &item : outDevices)
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
