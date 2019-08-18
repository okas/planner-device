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
const char *STA_WIFI_KEY = "hellohello";

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
  setupHardware();
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

void gotoIotInitMode()
{
  // ToDo if websocket do not receive any connections within... 1-2 minutes, then go back tu
  // Wifi reconnection loop. Maybe in void loop() ?
  Serial.printf("\nSetting up soft-AP fo IoT initialization ... \n");
  boolean result = WiFi.softAP(wifiHostname, STA_WIFI_KEY, 9, false, 1);
  if (result == true)
  {
    Serial.printf("SoftAP SSID: %s\n", wifiHostname);
    Serial.print("Soft-AP IP address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("Ready");
    Serial.println("Setting up WebSocket server for IoT device initialization ... ");
    webSocket.onEvent(webSocketEvent);
    webSocket.begin();
  }
  else
  {
    Serial.println("SoftAP startup failed! Hopefully restart helps...");
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t lenght)
{ // When a WebSocket message is received
  switch (type)
  {
  case WStype_DISCONNECTED: // if the websocket is disconnected
  {
    /* ToDo Disconnect softAP if Station is connect sucessfully.
     * On same conditions teardown websocket too.
     */

    // Serial.printf("[%u] Disconnected!\n", num);
    // Serial.println("- - now closing soft AP..");
    // WiFi.softAPdisconnect();
    break;
  }
  case WStype_CONNECTED: // if a new websocket connection is established
  {
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
    break;
  }
  case WStype_TEXT: // if new text data is received
  {
    Serial.printf("[%u] got Text: %s\n", num, payload);
    vector<string> payloadTokens = strsplit((char *)payload, "\n");
    wsTXTMessageHandler(num, payloadTokens);
    break;
  }
  }
}

void wsTXTMessageHandler(uint8_t num, vector<string> payloadTokens)
{
  const char *sbjGetHostname = "get-currentState";
  const char *sbjSetInitValues = "set-initValues";
  if (payloadTokens[0] == sbjGetHostname)
  {
    wsGetCurrentState(num, payloadTokens[0]);
  }
  else if (payloadTokens[0] == sbjSetInitValues)
  {
    wsSetInitValues(num, payloadTokens);
  }
}

void wsGetCurrentState(uint8_t num, string &subject)
{
  String payload = wsResponseBase(subject);
  payload += WiFi.hostname();
  payload += "\n";
  payload += WiFi.SSID();
  payload += "\n";
  payload += WiFi.psk();
  payload += "\n";
  payload += iotDeviceId;
  payload += "\n";
  payload += IOT_TYPE;
  for (Output_t &item : outDevices)
  {
    payload += "\n";
    payload += item.active;
    Serial.print("** - item.active: ");
    Serial.print(item.active);
    Serial.println();
  }
  webSocket.sendTXT(num, payload);
}

void wsSetInitValues(uint8_t num, vector<string> payloadTokens)
{
  const char *newIotClientId = payloadTokens[3].c_str();
  bool clientIdNeedsChange = strlen(newIotClientId) < 1 || strcmp(newIotClientId, iotDeviceId) != 0;
  Serial.printf("* - newIotClientId: \"%s\"\n", newIotClientId);
  Serial.print("* - clientIdNeedsChange: ");
  Serial.print(clientIdNeedsChange);
  Serial.println();
  if (clientIdNeedsChange)
  {
    wsMqttClientIdChange(newIotClientId);
    // ToDo  before, disconnect MQTT clients to notify device lost to API.
    setTopicBase();
  }
  wsActivateOutputs(payloadTokens);
  bool isConnected = wifiStationConnect(payloadTokens[1].c_str(),
                                        payloadTokens[2].c_str());
  wsRespondSetInitValuesState(num, payloadTokens[0], isConnected);
  // ToDo output set changes?
  if (clientIdNeedsChange)
  {
    mqttInit();
  }
}

void wsMqttClientIdChange(const char *clientId)
{
  if (mqttClient.connected())
  {
    mqttClient.disconnect();
  }
  memset(iotDeviceId, 0, sizeof(iotDeviceId));
  strncpy(iotDeviceId, clientId, sizeof(iotDeviceId));
  EEPROM.put(iotDeviceIdAddres, iotDeviceId);
  EEPROM.commit();
}

void wsActivateOutputs(vector<string> payloadTokens)
{
  size_t i = 4;
  for (Output_t &item : outDevices)
  {
    item.active = atoi(payloadTokens[i++].c_str()) ? true : false;
    EEPROM.put(item.addressActive, item.active);
  }
  EEPROM.commit();
}

void wsRespondSetInitValuesState(uint8_t num, string &subject, bool isWifiConnected)
{
  String payload = wsResponseBase(subject);
  payload += (uint)isWifiConnected;
  webSocket.sendTXT(num, payload);
}

String wsResponseBase(string &subject)
{
  String payload;
  payload.reserve(subject.size() + 40);
  payload += subject.c_str();
  payload += "-R\n";
  return payload;
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

void setupHardware()
{
  setupInitButton();
  setupOutputDevices();
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
