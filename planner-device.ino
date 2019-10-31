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
#include <ArduinoJson.h>

using namespace std;

const char *IOT_TYPE = "generic-2out";
char iotNodeId[13];

struct OutputDevice_t
{
  const uint8_t pin;
  uint64_t id;
  float state;
  char usage[20];
  unsigned int addressId;
  unsigned int addressState;
  unsigned int addressUsage;
};

enum IOTState_t : byte
{
  started,
  initMode,
  initialized,
  operating
};

enum InitState_t : byte
{
  stopped = 0,
  idle = 1,
  succeed = 2,
  failed = 3,
  working = 4
};

unsigned int _AddressIoTState;
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
  strncpy(iotNodeId, getWiFiMACHex(), sizeof(iotNodeId) - 1);
  setupInitButton();
  changeOutputStates();
  strncpy(wifiHostname, getWifiHostname(), sizeof(wifiHostname) - 1);
  WiFi.hostname(wifiHostname);
  // Decide when exactly need to go to the Init mode.
  if (_iotState == IOTState_t::initialized && wifiStationConnect())
  { /* Normal, initialization is done, and WiFi work. */
    gotoOperatingMode();
  }
  else if (_iotState == IOTState_t::started)
  { /* IoT node need initialization. */
    gotoIotInitMode();
  }
  else
  { /* WiFi connection failure, but initialized */

    // WiFi.disconnect();
    // startLEDBlinker();
    // gotoIotInitMode();
  }
}

void gotoOperatingMode()
{
  mqttNormalInit();
  // TODO, what to do, when it fails to pass MQTT Notrlam mode init?
  _iotState = IOTState_t::operating;
}

bool gotoIotInitMode()
{
  Serial.println(" - - Going to Initialization Mode.");
  startLEDBlinker();
  if (mqttClient.connected())
  {
    mqttClient.disconnect();
  }
  if (_iotState == IOTState_t::initialized || _iotState == IOTState_t::operating)
  {
    initMode_ticker.once(60, leaveIotInitMode);
  }
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
  _iotState = _initState == InitState_t::succeed ? IOTState_t::initialized : IOTState_t::started;
  _initState = InitState_t::stopped;
  endInitMode();
  stopLEDBlinker(true);
  // TODO restart ESP here in case of _iotState == IOTState_t::started?
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
    mqttNormalInit();
  }
  mqttClient.loop();
  webSocket.loop();
}
