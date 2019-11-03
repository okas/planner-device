/* Init Mode --- */

const char *STA_WIFI_KEY = "hellohello";
const float leaveInitTimeout = 30;
const char *staDet = "stateDetails";

/* --- Init Mode */

// 2 member: `state` and `stateDetails`.
const size_t wsCalcResponseBaseSize(size_t dataMemberCount = 2);
JsonDocument wsCreateResponse(const size_t, const char *, bool details = true);

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
    if (_initState == InitState_t::succeed)
    {
    Serial.printf(" - - In %fs leaving Initialization Mode.\n", leaveInitTimeout);
    initMode_ticker.once(leaveInitTimeout, leaveIotInitMode);
    }
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
  const size_t detailsCount = 1; // TODO Subject to change if array of messages need to be sent.
  const size_t capacity = wsGetInitStateJsonCapacity(includeCurrentConfig, detailsCount);
  JsonDocument responseDoc = wsCreateResponse(capacity, responseSubject);
  // TODO Either add some meaningful or nothing as a Status Detail.
  responseDoc[1][staDet].createNestedObject()["IoT_Node"] = "# not sure ... #";
  if (includeCurrentConfig)
  {
    wsAddConfigParams(responseDoc[1]);
  }
  wsSendTxtJsonResponse(num, responseDoc);
}

void wsAddConfigParams(JsonObject obj)
{
  obj["iotDeviceId"] = (const char *)iotNodeId;
  obj["ssid"] = WiFi.SSID().c_str();
  obj["psk"] = WiFi.psk().c_str();
  obj["iotType"] = IOT_TYPE;
  JsonArray outputs = obj.createNestedArray("outputs");
  for (OutputDevice_t &device : outDevices)
  {
    outputs.add((const char *)device.usage);
  }
}

void wsSetInitValues(uint8_t num, const char *responseSubject, JsonObject payloadObj)
{
  _initState = InitState_t::working;
  changeOutputStates();
  if (!wifiStationInit(payloadObj["ssid"], payloadObj["psk"]))
  {
    wsSendStateDetails(num, responseSubject, "WiFi", "< test state >");
    return;
  }
  int mqttState;
  if (!(mqttState = mqttIoTInit()))
  {
    wsSendStateDetails(num, responseSubject, "MQTT", mqttHelpGetStateTxt(mqttState));
    return;
  }

  //TODO this line cab be only called after async response from MQTT API
  // _initState = stageSucceed ? InitState_t::succeed : InitState_t::failed;
  if (webSocket.connectedClients(true))
  {
    wsSendState(num, responseSubject);
  }
  // TODO store ID' from API/MQTT as well.
  wsStoreConfigToEEPROM(payloadObj["outputs"]);
}

void wsStoreConfigToEEPROM(JsonArray outputValues)
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
    device.id = i + 1; // TODO mock value!!
    EEPROM.put(device.addressId, device.id);
  }
  EEPROM.put(_AddressIoTState, _iotState = IOTState_t::initialized);
  EEPROM.commit();
}

bool wsSendState(uint8_t num, const char *responseSubject)
{
  /* Size w/ only `state` member. */
  const size_t capacity = wsCalcResponseBaseSize(1);
  JsonDocument responseDoc = wsCreateResponse(capacity, responseSubject, false);
  return wsSendTxtJsonResponse(num, responseDoc);
}

bool wsSendStateDetails(uint8_t num, const char *responseSubject, const char *step, const char *descr)
{
  const size_t detailsCount = 1; // TODO Subject to change if array of messages need to be sent.
  const size_t capacity = wsCalcResponseBaseSize() + wsCalcStateDetailsSize(detailsCount);
  JsonDocument responseDoc = wsCreateResponse(capacity, responseSubject);
  if (strlen(step))
  {
    responseDoc[1][staDet].createNestedObject()[step] = descr;
  }
  return wsSendTxtJsonResponse(num, responseDoc);
}

JsonDocument wsCreateResponse(const size_t docSize, const char *responseSubject, bool details)
{
  DynamicJsonDocument responseDoc(docSize);
  responseDoc.add(responseSubject);
  JsonObject data = responseDoc.createNestedObject();
  data["state"] = (byte)_initState; // otherwise inferes wrong type (boolean)
  if (details)
  {
    data.createNestedArray(staDet);
  }
  return responseDoc;
}

bool wsSendTxtJsonResponse(uint8_t num, JsonDocument responseDoc)
{
  const size_t size = measureJson(responseDoc) + 1; // make room for \0 as well.
  char buffer[size];
  serializeJson(responseDoc, buffer, size);
  return webSocket.sendTXT(num, buffer, size - 1); // cut off \0 from data to be sent.
}

/**
 * Base response size, with WS subject part. Corresponds to:
 * ["request-R",{"state":"","stateDetails":[]]}]
 */
const size_t wsCalcResponseBaseSize(size_t dataMemberCount)
{
  return JSON_ARRAY_SIZE(2) + JSON_OBJECT_SIZE(dataMemberCount);
}

/**
 * Response's data object `stateDetails` array member's dynamic size. Corresponds to:
 * [{"name":"value"},{"name":"value"}]
 */
const size_t wsCalcStateDetailsSize(size_t detailsCount)
{
  return JSON_ARRAY_SIZE(detailsCount) + detailsCount * JSON_OBJECT_SIZE(1);
}

/**
 * Calculates necessary JSON doc size for serialization.
 * NB! Assumes all string members as `const char*`!
 * If any of string members are other type then add thos lenght+1 to this result.
 * Source: https://arduinojson.org/v6/faq/why-is-the-output-incomplete/
 */
const size_t wsGetInitStateJsonCapacity(bool includeCurrentConfig, int detailsCount)
{
  /* Data part has `state` and `stateDetails` by default;
   * but dynamically count in Config members;
   * dynamically add Details size. */
  size_t result = wsCalcResponseBaseSize(includeCurrentConfig ? 7 : 2) + wsCalcStateDetailsSize(detailsCount);
  /* Add stateDetails:[] and its members. */
  if (includeCurrentConfig)
  { /* Dynamically add IoT Config outputs:[] */
    result += JSON_ARRAY_SIZE(lenOutputs);
  }
  return result;
}
