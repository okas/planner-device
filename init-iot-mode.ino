/* Init Mode --- */

const char *STA_WIFI_KEY = "hellohello";

enum initState : byte
{
  idle = 1,
  succeed = 2,
  failed = 3,
  working = 4
};

initState _currentState;

/* --- Init Mode */

bool startInitMode()
{
  _currentState = idle;
  boolean result = softAPInit();
  if (result)
  {
    wsInit();
  }
  return result;
}

bool softAPInit()
{
  // ToDo if websocket do not receive any connections within... 1-2 minutes, then go back tu
  // Wifi reconnection loop. Maybe in void loop() ?
  Serial.printf("\nSetting up soft-AP fo IoT initialization ... \n");
  boolean result = WiFi.softAP(wifiHostname, STA_WIFI_KEY, 9, false, 1);
  if (result)
  {
    Serial.printf("SoftAP SSID: %s\n", wifiHostname);
    Serial.println("Ready");
  }
  else
  {
    Serial.println("SoftAP startup failed! Hopefully restart helps...");
  }
  return result;
}

void wsInit()
{
  Serial.println("Setting up WebSocket server for IoT device initialization ... ");
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
  if (payloadTokens[0] == "get-initState")
  {
    wsGetInitState(num, payloadTokens, _currentState == idle);
  }
  else if (payloadTokens[0] == "set-initValues")
  {
    wsSetInitValues(num, payloadTokens);
  }
  else if (payloadTokens[0] == "get-currentConfig")
  {
    wsGetInitState(num, payloadTokens, true);
  }
}

void wsGetInitState(uint8_t num, vector<string> payloadTokens, bool includeCurrentConfig)
{
  String payload = wsResponseBase(payloadTokens[0]);
  payload += _currentState;
  payload += "\n";
  if (includeCurrentConfig)
  {
    wsAddConfigParams(payload);
  }
  webSocket.sendTXT(num, payload);
}

void wsSetInitValues(uint8_t num, vector<string> payloadTokens)
{
  _currentState = working;
  const char *newIotClientId = payloadTokens[3].c_str();
  bool clientIdNeedsChange = strlen(newIotClientId) < 1 || strcmp(newIotClientId, iotDeviceId) != 0;
  Serial.printf("* - newIotClientId: \"%s\"\n", newIotClientId);
  Serial.print("* - clientIdNeedsChange: ");
  Serial.print(clientIdNeedsChange);
  Serial.println();
  if (clientIdNeedsChange)
  {
    wsMqttClientIdChange(newIotClientId);
    setTopicBase();
  }
  wsActivateOutputs(payloadTokens);
  bool isWiFiStaConnected = wifiStationInit(payloadTokens[1].c_str(),
                                        payloadTokens[2].c_str());
  _currentState = isWiFiStaConnected ? succeed : failed;

  if (webSocket.connectedClients(true))
  {
    wsSetValuesSucceed(num, payloadTokens[0]);
  }
  if (_currentState == succeed)
  {
    mqttInit();
  }
}

void wsMqttClientIdChange(const char *clientId)
{
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

bool wsSetValuesSucceed(uint8_t num, string &subject)
{
  String payload = wsResponseBase(subject);
  payload += _currentState;
  bool ret = webSocket.sendTXT(num, payload);
  return ret;
}

String wsResponseBase(string &subject)
{
  String payload;
  payload.reserve(subject.size() + 100);
  payload += subject.c_str();
  payload += "-R\n";
  return payload;
}

void wsAddConfigParams(String &payload)
{
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
  }
}
