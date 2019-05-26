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

using namespace std;

char iotDeviceId[31];
const uint8_t outputPins[] = {5, 4};
const size_t lenOutPins = sizeof(outputPins) / sizeof(uint8_t);
float outputStates[lenOutPins];

/* EEPROM addresses */
unsigned int iotDeviceIdAddr;
unsigned int outputStateAddrs[lenOutPins];

const char *STA_WIFI_KEY = "hellohello";
const char *MQTT_SERVER = "broker.hivemq.com";
const unsigned int MQTT_PORT = 1883;
const char *mqttUser = "";
const char *mqttPassword = "";

char topicBase[60];
const char *cmndState = "state";
const char *cmndSetState = "set-state";
const char *topicApiPresent = "saartk/api/present";

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

/* -- Helpers */

void setup()
{
  Serial.begin(115200);
  initStatesFromEEPROM();
  setupHardware();
  if (wifiStationConnect() && setTopicBase())
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
  Serial.print("Setting up soft-AP fo IoT initialization ... ");
  boolean result = WiFi.softAP(WiFi.hostname().c_str(), STA_WIFI_KEY, 9, false, 1);
  if (result == true)
  {
    Serial.print("Soft-AP IP address = ");
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
  payload += iotDeviceId;
  for (size_t i = 0; i < lenOutPins; i++)
  {
    payload += "\n";
    payload += i;
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
  memset(iotDeviceId, 0, sizeof(iotDeviceId));
  strncat(iotDeviceId, clientId, sizeof(iotDeviceId));
  EEPROM.put(iotDeviceIdAddr, iotDeviceId);
  EEPROM.commit();
  // ToDo  before, disconnect MQTT clients to notify device lost to API.
  setTopicBase();
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
  for (size_t i = 0; i < lenOutPins; i++)
  {
    pinMode(outputPins[i], OUTPUT);
    analogWrite(outputPins[i], round(outputStates[i] * 1024));
  }
}

void initStatesFromEEPROM()
{
  size_t eepromSize = calcEEPROMAddresses();
  EEPROM.begin(eepromSize);
  /* Init Actuator states from EEPROM to variable (array) */
  for (size_t i = 0; i < lenOutPins; i++)
  {
    EEPROM.get(outputStateAddrs[i], outputStates[i]);
    if (isnan(outputStates[i]))
    { /* init to get rid of 0xFF */
      outputStates[i] = 0.0f;
      EEPROM.put(outputStateAddrs[i], outputStates[i]);
    }
  }
  EEPROM.get(iotDeviceIdAddr, iotDeviceId);
  for (byte b : iotDeviceId)
  {
    if (b == 255)
    { /* init to get rid of any 0xFF */
      memset(iotDeviceId, 0, sizeof(iotDeviceId));
      EEPROM.put(iotDeviceIdAddr, iotDeviceId);
      break;
    }
  }
  /* In case .put() was called */
  EEPROM.commit();
}

size_t calcEEPROMAddresses()
{
  iotDeviceIdAddr = 0;
  size_t size = sizeof(iotDeviceId);
  size_t outStateValueSize = sizeof(decay<decltype(outputStates)>::type);
  for (size_t i = 0; i < lenOutPins; i++)
  {
    outputStateAddrs[i] = size;
    size += outStateValueSize;
  }
  return size;
}

bool setTopicBase()
{
  if (strlen(iotDeviceId) == 0)
  {
    Serial.println("- - provided MQTT ClientId is empty, cannot set topic base!");
    return false;
  }
  memset(topicBase, 0, sizeof(topicBase));
  strcpy(topicBase, "saartk/device/lamp/");
  strcat(topicBase, iotDeviceId);
  Serial.printf("- - topicBase [result] is : \"%s\"\n", topicBase);
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
  mqttSubscriber();
}

void mqttConnect()
{
  const char *lost = "/lost";
  char lastWillTopic[strlen(topicBase) + strlen(lost) + 1];
  strcpy(lastWillTopic, topicBase);
  strcat(lastWillTopic, lost);
  Serial.printf("- - lastWillTopic is : \"%s\"\n", lastWillTopic);
  while (!mqttClient.connected())
  {
    Serial.println(" Connecting to MQTT...");
    if (mqttClient.connect(iotDeviceId, mqttUser, mqttPassword, lastWillTopic, 0, false, ""))
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
  const char *present = "/present";
  char topicPresent[strlen(topicBase) + strlen(present) + 1];
  strcpy(topicPresent, topicBase);
  strcat(topicPresent, present);
  /* 15 is legth of JSON stucture as of current implementation.
     16 is max byte length of pin object. */
  char payload[15 + (16 * lenOutPins)];
  mqttGeneratePresentPayload(payload);
  mqttClient.publish(topicPresent, payload);
  Serial.printf("- - topicPresent is : \"%s\"\n", topicPresent);
  Serial.printf("- - payload is : \"%s\"\n", payload);
}

void mqttGeneratePresentPayload(char *payload)
{
  strcpy(payload, R"({"outputs":[)");
  for (size_t i = 0; i < lenOutPins; i++)
  {
    if (i > 0)
    {
      strcat(payload, ",");
    }
    char dev[16];
    sprintf(dev, R"({"%d":%.3f})", i, outputStates[i]);
    strcat(payload, dev);
  }
  strcat(payload, "]}");
}

void mqttSubscriber()
{
  Serial.printf(" subscribing to following topics: \n");
  for (size_t i = 0; i < lenOutPins; i++)
  {
    mqttSubscribeOutputToCommand(i, cmndState);
    mqttSubscribeOutputToCommand(i, cmndSetState);
  }
  Serial.printf("  api related: \"%s\"\n", topicApiPresent);
  mqttClient.subscribe(topicApiPresent);
}

void mqttSubscribeOutputToCommand(size_t outputIndex, const char *command)
{
  char topicCommand[256];
  generateSubscriptionForOutput(topicCommand, outputIndex, command);
  mqttClient.subscribe(topicCommand);
  Serial.printf("  command topic: \"%s\"\n", topicCommand);
}

void generateSubscriptionForOutput(char *buffer, size_t outputIndex, const char *command)
{
  char indexBuff[33];
  itoa(outputIndex, indexBuff, 10);
  strcpy(buffer, topicBase);
  strcat(buffer, "/");
  strcat(buffer, indexBuff);
  strcat(buffer, "/cmnd/");
  strcat(buffer, command);
  strcat(buffer, "/");
  strcat(buffer, "+");
}

void onMessage(char *topic, byte *payload, unsigned int length)
{
  logMessage(topic, payload, length);
  commandMessageHandler(topic, payload, length);
}

void logMessage(char *topic, byte *payload, size_t length)
{
  Serial.println(">>>>> Message:");
  Serial.printf(" topic: \"%s\"\n", topic);
  Serial.printf(" length, payload: \"%d\"\n", length);
  printBuffer(" payload bytes: ", payload, length);
  Serial.println("<<<<<");
}

void printBuffer(const char *msg, byte *buffer, size_t length)
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

void commandMessageHandler(char *topic, byte *payload, size_t length)
{
  const vector<string> topicTokens = strsplit(topic, "/");
  if (topicTokens[1] == "api")
  {
    // ToDo: api present...
  }
  else if (topicTokens[6] == cmndState)
  {
    int8_t idIdx = findOutputIndex(topicTokens);
    publishResponseDeviceState(idIdx, topicTokens);
  }
  else if (topicTokens[6] == cmndSetState)
  {
    cmndSetStateHandler(topicTokens, payload, length);
  }
  else
  {
    Serial.printf("- - Bad topic, unknown command! End handler.\n");
    return;
  }
}

void cmndSetStateHandler(const vector<string> topicTokens, byte *buffer, size_t length)
{
  int8_t idIdx = findOutputIndex(topicTokens);
  if (idIdx < -1)
  {
    Serial.println("Topic don't contain known output index. End new setting the state!");
    return;
  }
  float state = bufferToFloat(buffer, length);
  if (idIdx == -1)
  {
    for (size_t i = 0; i < lenOutPins; i++)
    {
      setStateAndSave(i, state);
    }
  }
  else
  {
    setStateAndSave(idIdx, state);
  }
  publishResponseDeviceState(idIdx, topicTokens);
}

int8_t findOutputIndex(const vector<string> topicTokens)
{
  string x = topicTokens[4];
  if (x.length() == 0)
  { /* all devices/indexes */
    return -1;
  }
  if (x == "0")
  {
    return 0;
  }
  unsigned int idx = atoi(x.c_str());
  if (idx < 0 || idx > lenOutPins - 1)
  {
    return -2;
  }
  return (int8_t)idx;
}

void setStateAndSave(int8_t outputIdx, float state)
{
  outputStates[outputIdx] = state;
  analogWrite(outputPins[outputIdx], round(outputStates[outputIdx] * 1024));
  EEPROM.put(outputStateAddrs[outputIdx], outputStates[outputIdx]);
  EEPROM.commit();
  Serial.printf("- - set GPIO PIN value: \"%d\"=\"%f\"\n", outputPins[outputIdx], outputStates[outputIdx]);
}

void publishResponseDeviceState(int8_t outputIdx, const vector<string> topicTokens)
{
  // ToDo handle JSON responses as well
  char *responseTopic = createResponseTopic(topicTokens);
  byte payload[sizeof(outputStates[outputIdx])];
  *(float *)(payload) = outputStates[outputIdx]; // convert float to bytes
  Serial.printf("- - responseTopic: \"%s\".\n", responseTopic);
  printBuffer("- - responsePayload bytes: ", payload, sizeof(payload));
  mqttClient.publish(responseTopic, payload, sizeof(payload));
}

char *createResponseTopic(const vector<string> topicTokens)
{
  char result[sizeof(topicTokens) + topicTokens.size()];
  for (size_t i = 0; i < topicTokens.size(); i++)
  {
    if (i > 0)
    {
      strcat(result, "/");
    }
    strcat(result, i != 5 ? topicTokens[i].c_str() : "resp");
  }
  return result;
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
