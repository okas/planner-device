/* Init Mode --- */

const char *STA_WIFI_KEY = "hellohello";
const float leaveInitTimeout = 30;

/* --- Init Mode */

bool startInitMode()
{
  _initState = InitState_t::idle;
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
    Serial.printf("SoftAP startup failed! Status code: %d.\n", WiFi.status());
  }
  return result;
}

void wsInit()
{
  Serial.println("Setting up WebSocket server for IoT device initialization ... ");
  webSocket.onEvent(webSocketEvent);
  webSocket.begin();
}

bool endInitMode()
{
  webSocket.close();
  WiFi.softAPdisconnect();
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t lenght)
{
  switch (type)
  {
  case WStype_DISCONNECTED: // if the websocket is disconnected
  {
    Serial.printf(" - - In %fs leaving Initialization Mode.\n", leaveInitTimeout);
    initMode_ticker.once(leaveInitTimeout, leaveIotInitMode);
    break;
  }
  case WStype_CONNECTED: // if a new websocket connection is established
  {
    if (initMode_ticker.active())
    {
      Serial.println(" - - Cancel timer of \"Leaving Initialization Mode\".");
      initMode_ticker.detach();
    }
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
    break;
  }
  case WStype_TEXT: // if new text data is received
  {
    wsTXTMessageHandler(num, (char *)payload, lenght);
    break;
  }
  }
}

void wsTXTMessageHandler(uint8_t num, char *payload, size_t lenght)
{
  const size_t capacity = 2 * JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(3) + 40;
  StaticJsonDocument<capacity> payloadDoc;
  deserializeJson(payloadDoc, payload, lenght);
  const char *subject = payloadDoc[0];
  if (strcmp(subject, "get-initState") == 0)
  {
    wsGetInitState(num, "get-initState-R", _initState == InitState_t::idle);
  }
  else if (strcmp(subject, "set-initValues") == 0)
  {
    wsSetInitValues(num, "set-initValues-R", payloadDoc[1]);
  }
  else if (strcmp(subject, "get-currentConfig") == 0)
  {
    wsGetInitState(num, "get-currentConfig-R", true);
  }
}

void wsGetInitState(uint8_t num, const char *responseSubject, bool includeCurrentConfig)
{
  const size_t capacity = 2 * JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(6) + includeCurrentConfig
                              ? 300
                              : 30;
  JsonDocument responseDoc = wsCreateResponse(capacity, responseSubject);
  if (includeCurrentConfig)
  {
    wsAddConfigParams(responseDoc[1]);
  }
  wsResponseSendTXT(num, responseDoc);
}

void wsAddConfigParams(JsonObject obj)
{
  obj["iotDeviceId"] = WiFi.macAddress();
  obj["ssid"] = WiFi.SSID();
  obj["psk"] = WiFi.psk();
  obj["iotType"] = IOT_TYPE;
  JsonArray outputs = obj.createNestedArray("outputs");
  for (OutputDevice_t &device : outDevices)
  {
    outputs.add(device.usage);
  }
}

void wsSetInitValues(uint8_t num, const char *responseSubject, JsonObject payloadObj)
{
  _initState = InitState_t::working;
  /* TODO: If I/O states change then probably new MQTT topic must be set up.
  * Old implementation used specialised valu fir MQTT client id, that was part of subscribed topic as well.
  */
  wsActivateOutputs(payloadObj["outputs"]);
  bool isWiFiStaConnected = wifiStationInit(payloadObj["ssid"], payloadObj["psk"]);
  _initState = isWiFiStaConnected ? InitState_t::succeed : InitState_t::failed;
  if (webSocket.connectedClients(true))
  {
    wsSetValuesSucceed(num, responseSubject);
  }
  if (_initState == InitState_t::succeed)
  {
    mqttInit();
  }
}

void wsActivateOutputs(JsonArray outputValues)
{
  size_t lenUsage = sizeof(OutputDevice_t::usage);
  for (size_t i = 0; i < lenOutputs; i++)
  {
    OutputDevice_t &device = outDevices[i];
    const char *val = outputValues[i] | "";
    size_t lenVal = strlen(val);
    memset(device.usage, '\0', lenUsage);
    if (lenVal > 0)
    {
      strncpy(device.usage, val, lenVal < lenUsage ? lenVal : lenUsage);
      /* Ensure 0-terminated */
      device.usage[lenUsage - 1] = '\0';
    }
    EEPROM.put(device.addressUsage, device.usage);
  }
  EEPROM.commit();
}

bool wsSetValuesSucceed(uint8_t num, const char *responseSubject)
{
  const size_t capacity = JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(1) + 30;
  JsonDocument responseDoc = wsCreateResponse(capacity, responseSubject);
  return wsResponseSendTXT(num, responseDoc);
}

JsonDocument wsCreateResponse(const size_t docSize, const char *responseSubject)
{
  DynamicJsonDocument responseDoc(docSize);
  responseDoc.add(responseSubject);
  JsonObject data = responseDoc.createNestedObject();
  data["state"] = (byte)_initState; // otherwise inferes wrong type (boolean)
  return responseDoc;
}

bool wsResponseSendTXT(uint8_t num, JsonDocument responseDoc)
{
  const size_t size = measureJson(responseDoc) + 1; // make room for \u0 as well.
  char buffer[size];
  serializeJson(responseDoc, buffer, size);
  return webSocket.sendTXT(num, buffer, size - 1); // cut off \u0 from data to be sent.
}
