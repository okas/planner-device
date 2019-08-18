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

/* Helpers -- */

union UnionFloatByte {
  float f;
  byte b[sizeof f];
};

float bufferToFloat(byte *buffer, unsigned int length)
{
  UnionFloatByte temp;
  for (size_t i = 0; i < length; i++)
  {
    temp.b[i] = buffer[i];
  }
  return temp.f;
}

vector<string> strsplit(char *phrase, char *delimiter)
{
  string s = phrase;
  vector<string> ret;
  size_t start = 0;
  size_t end = 0;
  size_t len = 0;
  string token;
  do
  {
    end = s.find(delimiter, start);
    len = end - start;
    token = s.substr(start, len);
    ret.emplace_back(token);
    start += len + strlen(delimiter);
  } while (end != string::npos);
  return ret;
}

size_t getActiveOutputCount()
{
  size_t result = 0;
  for (Output_t device : outDevices)
  {
    if (device.active)
    {
      result++;
    }
  }
  return result;
}

/* -- Helpers */

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

bool wifiStationConnect(const char *ssid, const char *psk)
{
  if (strcmp(ssid, WiFi.SSID().c_str()) == 0 && strcmp(psk, WiFi.psk().c_str()) == 0)
  {
    return wifiStationConnect();
  }
  else
  {
    WiFi.disconnect();
    WiFi.begin(ssid, psk);
    return wifiStationConnectVerifier();
  }
}

bool wifiStationConnect()
{
  if (strlen(WiFi.SSID().c_str()) == 0 || strlen(WiFi.psk().c_str()) == 0)
  {
    Serial.printf("\nNo WiFi SSID or PSK stored, end connecting.\n");
    return false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  return wifiStationConnectVerifier();
}

bool wifiStationConnectVerifier()
{
  Serial.printf("\nConnecting to Wifi");
  int i = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(" .");
    if (++i == 10)
    {
      Serial.printf("\nfailed to connect in %d attempts to SSID \"%s\"\n", i, WiFi.SSID().c_str());
      WiFi.disconnect();
      return false;
    }
  }
  Serial.printf("\nConnected to SSID \"%s\"\n", WiFi.SSID().c_str());
  return true;
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
