#include <Arduino.h>
#include <cmath>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>
#include <stdlib.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <MQTT.h>
#include <WebSocketsServer.h>
#include <Ticker.h>
/* NB! ArduinoJson Assistant do not use this! */
#define ARDUINOJSON_USE_LONG_LONG 1
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
  started = 0,
  initMode = 1,
  initialized = 2,
  operating = 3
};

enum InitState_t : byte
{
  stopped = 0,
  idle = 1,
  succeed = 2,
  failed = 3,
  working = 4
};

unsigned int _AddressEEPROMInitState;
unsigned int _AddressIoTState;
uint8_t _isEEPROMInit;
IOTState_t _iotState;
InitState_t _initState;
Ticker initMode_ticker;

OutputDevice_t outDevices[] = {{.pin = 5}, {.pin = 4}};
const size_t lenOutputs = sizeof(outDevices) / sizeof(OutputDevice_t);

char wifiHostname[11];

WiFiClient espClient;
MQTTClient mqttClient(1024);
WebSocketsServer webSocket(81);

void setup()
{
  Serial.begin(115200);
  eepromInitialize();
  if (_iotState == IOTState_t::initialized)
  {
    hwWriteStatesFromRAM();
  }
  strncpy(iotNodeId, getWiFiMACHex(), sizeof(iotNodeId) - 1);
  strncpy(wifiHostname, getWifiHostname(), sizeof(wifiHostname) - 1);
  setupInitButton();
  WiFi.hostname(wifiHostname);
  // Decide when exactly need to go to the Init mode.
  if (_iotState == IOTState_t::initialized && wifiStationConnect())
  { /* Normal, initialization is done, and WiFi work. */
    Serial.printf("~ ~ ~ ~ ~ GOTO OPERATING MODE: _iotState: %d\n", _iotState);
    gotoOperatingMode();
  }
  else if (_iotState == IOTState_t::started || _iotState == IOTState_t::initMode)
  { /* IoT node need initialization. */
    Serial.printf("~ ~ ~ ~ ~ GOTO INIT MODE: _iotState: %d\n", _iotState);
    gotoIotInitMode();
  }
  else
  { /* WiFi connection failure, but initialized */
    Serial.printf("~ ~ ~ ~ ~ GOTO IDLE!!: _iotState: %d\n", _iotState);

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
  hwOutputsTurnOffActive();
  startLEDBlinker();
  if (_iotState == IOTState_t::initialized || _iotState == IOTState_t::operating)
  {
    initMode_ticker.once(60, leaveIotInitMode);
  }
  bool ret = startInitMode();
  if (ret)
  {
    eepromIoTStateStore(_iotState = IOTState_t::initMode);
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
  eepromIoTStateStore(_iotState);
  endInitMode();
  stopLEDBlinker(true);
  if (_iotState == IOTState_t::initialized)
  {
    Serial.printf("~ ~ ~ ~ ~ GOTO OPERATING MODE: _iotState: %d\n", _iotState);
    gotoOperatingMode();
  }
}

void loop()
{
  iot_start_init_loop();
  mqttClient.loop();
  delay(10); // <- fixes some issues with WiFi stability
  /* TODO
  Add Conditional loop for nly Init Mode time */
  webSocket.loop();
}
