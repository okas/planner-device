#include <Arduino.h>
#include <cmath>
#include <string>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WebSocketsServer.h>

using namespace std;

/* EEPROM addresses */
unsigned int devStateAddr;
unsigned int mqttClientIdAddr;

float deviceState;
const uint8_t pin_DEVICE = 5;

const char *STA_WIFI_KEY = {"hellohello"};
const char *MQTT_SERVER = "broker.hivemq.com";
const unsigned int MQTT_PORT = 1883;
const char *mqttUser = "";
const char *mqttPassword = "";
// char mqttClientId[31] = {"6530917364161053000"};
char mqttClientId[31];

const char *topicApiPresent = "saartk/api/present";
#define CMND "/cmnd/"
const char *cmndState = CMND "state/";
const char *cmndSetState = CMND "set-state/";
string topicBase;

WiFiClient espClient;
PubSubClient mqttClient;

WebSocketsServer webSocket(81);

union UnionFloatByte {
  float f;
  byte b[sizeof f];
};

/* Helpers -- */

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

/* -- Helpers */

void setup()
{
  Serial.begin(115200);
  initStatesFromEEPROM();
  setupHardware();
  if (!wifiStationConnect())
  {
    // ToDo if websocket do not receive any connections within... 1-2 minutes, then go back tu
    // Wifi reconnection loop. Maybe in void loop() ?
    Serial.print("Setting up soft-AP ... ");
    boolean result = WiFi.softAP(WiFi.hostname().c_str(), STA_WIFI_KEY, 9, false, 1);
    if (result == true)
    {
      Serial.println("Ready");
      webSocketInit();
    }
    else
    {
      Serial.println("Failed!");
      return;
    }
  }
  else
  {
    if (setTopicBase())
    {
      mqttInit();
    }
    else
    { // ToDo go to WIFI SOFT AP mode to gather mqtt client id.
    }
  }
}

void webSocketInit()
{
  Serial.println("Setting WebSocket server for IoT device initialization ... ");
  webSocket.onEvent(webSocketEvent);
  webSocket.begin();
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
  payload += mqttClientId;
  webSocket.sendTXT(num, payload);
}

void wsSetInitValues(uint8_t num, vector<string> payloadTokens)
{
  const char *newMqttClientId = payloadTokens[3].c_str();
  Serial.printf("* - newMqttClientId: \"%s\"\n", newMqttClientId);
  bool clientIdNeedsChange = strlen(newMqttClientId) < 1 || strcmp(newMqttClientId, mqttClientId) != 0;
  Serial.print("* - clientIdNeedsChange: ");
  Serial.print(clientIdNeedsChange);
  Serial.println();
  if (clientIdNeedsChange)
  {
    wsMqttClientIdChange(newMqttClientId);
  }
  bool isConnected = wifiStationConnect(payloadTokens[1].c_str(),
                                        payloadTokens[2].c_str());
  wsRespondSetInitValuesState(num, payloadTokens[0], isConnected);
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
  memset(mqttClientId, 0, sizeof(mqttClientId));
  strncat(mqttClientId, clientId, sizeof(mqttClientId));
  Serial.printf("* - mqttClientId AFTER memcpy: \"%s\"\n", mqttClientId);
  Serial.printf("* - mqttClientIdAddr for memcpy: \"%d\"\n", mqttClientIdAddr);
  EEPROM.put(mqttClientIdAddr, mqttClientId);
  EEPROM.commit();
  setTopicBase();
  Serial.printf("* - topicBase AFTER setTopicBase(): \"%s\"\n", topicBase.c_str());
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

void setupHardware()
{
  pinMode(pin_DEVICE, OUTPUT);
  analogWrite(pin_DEVICE, round(deviceState * 1024));
}

void initStatesFromEEPROM()
{
  size_t eepromSize = calcEEPROMAddresses();
  EEPROM.begin(eepromSize);
  EEPROM.get(devStateAddr, deviceState);
  if (isnan(deviceState))
  { // init to get rid of 0xFF
    deviceState = 0.0f;
    EEPROM.put(devStateAddr, deviceState);
  }
  EEPROM.get(mqttClientIdAddr, mqttClientId);
  size_t len = sizeof(mqttClientId) / sizeof(*mqttClientId);
  for (size_t i = 0; i < len; i++)
  {
    if ((byte)mqttClientId[i] == 255)
    { // init to get rid of any 0xFF
      memset(mqttClientId, 0, len);
      EEPROM.put(mqttClientIdAddr, mqttClientId);
      break;
    }
  }
}

size_t calcEEPROMAddresses()
{
  devStateAddr = 0;
  mqttClientIdAddr = devStateAddr + sizeof(deviceState);
  size_t size = mqttClientIdAddr + sizeof(mqttClientId) / sizeof(*mqttClientId);
  return size;
}

bool setTopicBase()
{
  if (strlen(mqttClientId) == 0)
  {
    Serial.println("- - provided MQTT ClientId is empty, cannot set topic base!");
    return false;
  }
  if (topicBase.length() > 0)
  {
    topicBase.clear();
  }
  topicBase = "saartk/device/lamp/";
  topicBase += mqttClientId;
  Serial.printf("- - topicBase [result] is : \"%s\"\n", topicBase.c_str());
  return true;
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

void mqttInit()
{
  mqttClient.setClient(espClient);
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(onMessage);
  mqttConnect();
  mqttPublishPresent();
  mqttSubscribe();
}

void mqttConnect()
{
  string lastWillTopic = topicBase;
  lastWillTopic += "/lost";
  Serial.printf("- - lastWillTopic is : \"%s\"\n", lastWillTopic.c_str());
  while (!mqttClient.connected())
  {
    Serial.println(" Connecting to MQTT...");
    if (mqttClient.connect(mqttClientId, mqttUser, mqttPassword, lastWillTopic.c_str(), 0, false, ""))
    {
      Serial.println(" MQTT connected!");
    }
    else
    {
      Serial.printf(R"( failed with state "%d")", mqttClient.state());
      Serial.println();
      delay(2000);
    }
  }
}

void mqttPublishPresent()
{
  string topicPresent = topicBase;
  topicPresent += "/present";
  char payload[30];
  snprintf(payload, sizeof payload, R"({"state":%.3f})", deviceState);
  mqttClient.publish(topicPresent.c_str(), payload);
  Serial.printf("- - topicPresent is : \"%s\"\n", topicPresent.c_str());
  Serial.printf("- - payload is : \"%s\"\n", payload);
}

void mqttSubscribe()
{
  string topicCommand = topicBase;
  topicCommand += cmndState;
  topicCommand += "+";
  Serial.printf(" topicCommand 1: \"%s\"\n", topicCommand.c_str());
  mqttClient.subscribe(topicCommand.c_str());
  topicCommand.clear();
  topicCommand = topicBase;
  topicCommand += cmndSetState;
  topicCommand += "+";
  Serial.printf(" topicCommand 2: \"%s\"\n", topicCommand.c_str());
  mqttClient.subscribe(topicCommand.c_str());
  mqttClient.subscribe(topicApiPresent);
}

void onMessage(char *topic, byte *payload, unsigned int length)
{
  logMessage(topic, payload, length);
  commandMessageHandler(topic, payload, length);
}

void logMessage(char *topic, byte *payload, unsigned int length)
{
  Serial.println(">>>>> Message:");
  Serial.printf(" topic: \"%s\"\n", topic);
  Serial.printf(" length, payload: \"%d\"\n", length);
  printBuffer(" payload bytes: ", payload, length);
  Serial.println("<<<<<");
}

void printBuffer(const char *msg, byte *buffer, unsigned int length)
{
  Serial.printf("%s<", msg);
  for (unsigned int i = 0; i < length; i++)
  {
    if (i)
    {
      Serial.print(' ');
    }
    Serial.print(buffer[i]);
  }
  Serial.println(">");
}

void commandMessageHandler(char *topic, byte *payload, unsigned int length)
{
  string strTopic = topic;
  if (strstr(topic, cmndSetState))
  {
    setDeviceState(payload, length);
  }
  else if (strstr(topic, topicApiPresent))
  {
    return; // ToDo: api present...
  }
  else if (!strstr(topic, cmndState))
  {
    Serial.printf("- - Bad topic, unknown command! End handler.\n");
    return;
  }
  publishResponseDeviceState(strTopic);
}

void setDeviceState(byte *buffer, unsigned int length)
{
  UnionFloatByte temp;
  for (int i = 0; i < length; i++)
  {
    temp.b[i] = buffer[i];
  }
  deviceState = temp.f;
  analogWrite(pin_DEVICE, round(deviceState * 1024));
  EEPROM.put(devStateAddr, deviceState);
  EEPROM.commit();
  Serial.printf("- - set value: \"%f\"\n", deviceState);
}

void publishResponseDeviceState(string &strTopic)
{
  const char *srch = "/cmnd/";
  string responseTopic = strTopic.replace(strTopic.find(srch) + 1, strlen(srch) - 2, "resp");
  byte buffer[sizeof(deviceState)];
  *(float *)(buffer) = deviceState; // convert float to bytes
  Serial.printf("- - responseTopic: \"%s\".\n", responseTopic.c_str());
  printBuffer("- - responsePayload bytes: ", buffer, sizeof(buffer));
  mqttClient.publish(responseTopic.c_str(), buffer, sizeof(buffer));
}

void loop()
{
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
